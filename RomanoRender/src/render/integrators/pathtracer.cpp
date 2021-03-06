#include "pathtracer.h"


vec3 pathtrace(int s, std::vector<vec2>& sampler, const Ray& r, vec3 color, std::vector<Material>& mats, Render_Settings& settings, std::vector<Light*>& lights, int depth[], std::vector<int>& light_path, int samples[], Stats& stat)
{
    // defining all the randoms and pseudo randoms we'll need for the samples
    const float random_float = generate_random_float_fast(s + samples[0]);
    const int sampler_id = (int)(random_float * (sampler.size() - 1));
    const vec2 sample = sampler[sampler_id];

    // ray terminated
    if (depth[0] == 0 || depth[1] == 0 || depth[2] == 0) return vec3(0.0f);

    // initialize embree context
    RTCIntersectContext context;
    rtcInitIntersectContext(&context);

    float near_clipping = 0.0f;
    float far_clipping = 10000.0f;

    RTCRayHit new_ray = r.rayhit;

    vec3 new_color(0.0f);

    rtcIntersect1(settings.scene, &context, &new_ray);

    if (new_ray.hit.geomID != RTC_INVALID_GEOMETRY_ID)
    {
        int hit_mat_id = new_ray.hit.geomID;

        if (mats[hit_mat_id].islight)
        {
            if (dot(mats[hit_mat_id].normal, r.direction) < 0 && depth[0] == 10) return mats[hit_mat_id].diffuse_color;
            else if (light_path.size() > 1 && light_path[0] == 1 && light_path[1] == 3) return mats[hit_mat_id].diffuse_color * 10.0f;
            else return vec3(0.0f);
        }

        new_color = mats[hit_mat_id].diffuse_color;

        const float hit_diff_roughness = mats[hit_mat_id].diffuse_roughness;
        const float hit_diff_weight = mats[hit_mat_id].diffuse_weight;
        const float hit_roughness = std::max(0.005f, mats[hit_mat_id].reflectance_roughness);
        const float hit_refraction = mats[hit_mat_id].refraction;
        const float hit_metallic = mats[hit_mat_id].metallic;
        float hit_reflectance = mats[hit_mat_id].reflectance;
        const float hit_sss = mats[hit_mat_id].sss;

        const vec3 hit_reflectance_color = mats[hit_mat_id].reflectance_color;
        const vec3 hit_ior = mats[hit_mat_id].ior;
        const vec3 hit_sss_color = mats[hit_mat_id].sss_color;

        const vec3 hit_normal = vec3(new_ray.hit.Ng_x, new_ray.hit.Ng_y, new_ray.hit.Ng_z).normalize();
        const vec3 hit_pos = vec3(new_ray.ray.org_x, new_ray.ray.org_y, new_ray.ray.org_z) + new_ray.ray.tfar * vec3(new_ray.ray.dir_x, new_ray.ray.dir_y, new_ray.ray.dir_z);

        vec3 kd(1.0f);
        vec3 ks(0.5f);
        vec3 radiance(0.0f);
        vec3 specular(0.0f);
        vec3 refrac(0.0f);
        vec3 trans(0.0f);
        vec3 indirect(0.0f);
        vec3 ggx(0.0f);

        // direct Lighting
        for (auto& light : lights)
        {
            for (int i = 0; i < samples[1]; i++)
            {
                // use to convert parent ptr to subtype ptr to get subtype specific members using branched dynamic cast
                Point_Light* ptlight = nullptr;
                Distant_Light* distlight = nullptr;
                Square_Light* sqlight = nullptr;
                Dome_Light* domelight = nullptr;

                float distance = 10000.0f;
                float area_shadow = 1.0f;
                int hdri_id = 1;

                vec3 ray_dir = light->return_ray_direction(hit_pos, sample);

                // point light
                if (ptlight = dynamic_cast<Point_Light*>(light))
                {
                    ray_dir = ptlight->return_ray_direction(hit_pos, sample);
                    distance = dist(hit_pos, ptlight->position) - 0.001f;
                }

                // distant light
                else if (distlight = dynamic_cast<Distant_Light*>(light)) ray_dir = distlight->return_ray_direction(hit_pos, sample);

                // dome light
                else if (domelight = dynamic_cast<Dome_Light*>(light)) ray_dir = domelight->return_ray_direction(hit_pos, sample);

                // square light
                else if (sqlight = dynamic_cast<Square_Light*>(light))
                {
                    const vec3 light_sample_pos = sqlight->return_ray_direction(hit_pos, sample);
                    ray_dir = (light_sample_pos - hit_pos).normalize();

                    if (dot(ray_dir, sqlight->normal) > 0) continue;
                    else
                    {
                        distance = dist(hit_pos, light_sample_pos) - 0.001f;
                        float d = dot(sqlight->normal, ray_dir);
                        area_shadow = -d;
                    }
                }

                Ray shadow(hit_pos, ray_dir, 0.001f, distance);

                const float NdotL = std::max(0.f, dot(hit_normal, ray_dir));

                rtcOccluded1(settings.scene, &context, &shadow.ray);

                if (shadow.ray.tfar > 0.0f)
                {
                    radiance += light->return_light_throughput(distance) * NdotL * area_shadow;

                    if (hit_reflectance > 0.0f || hit_refraction > 0.0f)
                    {
                        const vec3 H = (ray_dir + -r.direction).normalize();
                        const float NdotH = Saturate(dot(hit_normal, H));
                        const float LdotH = Saturate(dot(ray_dir, H));
                        const float NdotV = Saturate(dot(hit_normal, -r.direction));

                        const float D = ggx_normal_distribution(NdotH, hit_roughness);
                        const float G = schlick_masking_term(NdotL, NdotV, hit_roughness);
                        const vec3 F = schlick_fresnel(hit_ior, LdotH);

                        ggx += (D * G * F / (4 * std::max(0.001f, NdotV)));

                    }
                }
            }
        }

        vec3 mix(1.0f);
        const float f0 = fresnel_reflection_coef(hit_ior.x, hit_normal, r.direction) * hit_reflectance;
        kd = (1.0f - clamp(abs(f0), 0.00f, 1.0f)) * (1.0f - hit_metallic) * (1.0f - hit_refraction) * hit_diff_weight;

        // refraction
        if (hit_refraction > 0.0f)
        {
            float offset;

            if (dot(r.direction, hit_normal) > 0) offset = 0.001f;
            if (dot(r.direction, hit_normal) < 0) offset = -0.001f;

            vec3 H = ggx_microfacet(hit_normal, mats[hit_mat_id].refraction_roughness, sample);

            bool has_reflected = false;

            vec3 new_ray_dir = refract(r.direction, H.normalize(), hit_ior.x, has_reflected);

            if (has_reflected) hit_reflectance = 1.0f;

            Ray new_ray(hit_pos + hit_normal * offset, new_ray_dir);

            int new_depth[] = { depth[0], depth[1], depth[2] - 1, depth[3] };

            light_path.push_back(3);

            refrac += pathtrace(s, sampler, new_ray, color, mats, settings, lights, new_depth, light_path, samples, stat) * mats[hit_mat_id].refraction_color;
        }

        // subsurface scattering
        if (hit_sss > 0.0f)
        {
            int new_depth[] = { depth[0], depth[1], depth[2], depth[3] - 1 };

            const vec3 radius = mats[hit_mat_id].sss_radius;
            const float scale = mats[hit_mat_id].sss_scale;
            const float absorption = 1 - mats[hit_mat_id].sss_abs;
            const int steps = mats[hit_mat_id].sss_steps;
            bool transmitted = false;
            float step = 0.0f;

            float pdf = 1.0f;

            vec3 position = hit_pos + hit_normal * -0.001f;
            vec3 direction = sample_ray_in_hemisphere(-hit_normal, sample);

            float step_size = scale;

            vec3 start_position = position;
            vec3 end_position(0.0f);
            vec3 end_normal(0.0f);


            for (int i = 1; i < steps + 1; i++)
            {
                //if (generate_random_float() > 0.3f) break;

                Ray ray(position, direction, 0.0f, step_size);

                rtcIntersect1(settings.scene, &context, &ray.rayhit);

                if (ray.rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                {
                    transmitted = true;
                    step = ray.rayhit.ray.tfar;
                    end_position = position + direction * step;
                    end_normal = vec3(ray.rayhit.hit.Ng_x, ray.rayhit.hit.Ng_y, ray.rayhit.hit.Ng_z);

                    break;
                }

                float random = generate_random_float_fast(s + samples[0]);
                step_size = (random * 2) * scale / i;

                position += direction * step_size;

                direction = sample_ray_in_hemisphere(direction, sample);
            }

            float walked_distance = dist(start_position, end_position);
            float t = 1 - exp(-walked_distance);

            if (transmitted)
            {
                vec3 sss_light_contribution(0.0f);

                for (auto light : lights)
                {
                    for (int i = 0; i < samples[1]; i++)
                    {
                        // used to convert parent ptr to subtype ptr to get subtype specific members using branched dynamic cast
                        Point_Light* ptlight = nullptr;
                        Distant_Light* distlight = nullptr;
                        Square_Light* sqlight = nullptr;
                        Dome_Light* domelight = nullptr;

                        float distance = 10000.0f;
                        float area_shadow = 1.0f;
                        int hdri_id = 1;

                        vec3 ray_dir = light->return_ray_direction(hit_pos, sample);

                        // point light
                        if (ptlight = dynamic_cast<Point_Light*>(light))
                        {
                            ray_dir = ptlight->return_ray_direction(hit_pos, sample);
                            distance = dist(hit_pos, ptlight->position) - 0.001f;
                        }

                        // distant light
                        else if (distlight = dynamic_cast<Distant_Light*>(light)) ray_dir = distlight->return_ray_direction(hit_pos, sample);

                        // dome light
                        else if (domelight = dynamic_cast<Dome_Light*>(light)) ray_dir = domelight->return_ray_direction(hit_pos, sample);

                        // square light
                        else if (sqlight = dynamic_cast<Square_Light*>(light))
                        {
                            const vec3 light_sample_pos = sqlight->return_ray_direction(hit_pos, sample);
                            ray_dir = (light_sample_pos - hit_pos).normalize();

                            if (dot(ray_dir, sqlight->normal) > 0) continue;
                            else
                            {
                                distance = dist(hit_pos, light_sample_pos) - 0.001f;
                                float d = dot(sqlight->normal, ray_dir);
                                area_shadow = -d;
                            }
                        }

                        Ray new_ray(end_position + end_normal * 0.001f, end_normal);

                        float NdotL = std::max(0.f, dot(hit_normal, ray_dir));

                        Ray shadow(new_ray.origin, ray_dir, 0.0f, distance);

                        rtcOccluded1(settings.scene, &context, &shadow.ray);

                        if (shadow.ray.tfar > 0.0f)
                        {
                            sss_light_contribution += light->return_light_throughput(distance) * inv_pi;
                        }
                    }
                }

                vec3 transmitted_color = vec3(hit_sss_color.x * fit(t * absorption, 0.0f, radius.x, 1.0f, 0.0f),
                    hit_sss_color.y * fit(t * absorption, 0.0f, radius.y, 1.0f, 0.0f),
                    hit_sss_color.z * fit(t * absorption, 0.0f, radius.z, 1.0f, 0.0f));

                trans += sss_light_contribution * transmitted_color;
            }
        }

        // diffuse / reflection
        if (hit_refraction < 1.0f)
        {
            float random = 1.0f;
            float mix_reflectance = 0.0f;

            if (hit_reflectance > 0.0f)
            {
                random = random_float;
                mix_reflectance = fit01(hit_reflectance, 0.0f, 0.5f);
            }

            vec3 new_ray_dir(0.0f);
            int new_depth[] = { depth[0], depth[1], depth[2] };

            if (random > mix_reflectance)
            {
                new_ray_dir = sample_ray_in_hemisphere(hit_normal, sample);
                light_path.push_back(1);
                new_depth[0] -= 1;
                mix = new_color * kd;
            }
            else
            {
                vec3 H = ggx_microfacet(hit_normal, hit_roughness, sample);
                new_ray_dir = reflect(r.direction, H.normalize());
                light_path.push_back(2);
                new_depth[1] -= 1;
                mix = (hit_reflectance * clamp(f0, 0.04f, 1.0f) + hit_metallic) * hit_reflectance_color;
            }

            Ray new_ray(hit_pos + hit_normal * 0.001f, new_ray_dir);

            indirect += pathtrace(s, sampler, new_ray, color, mats, settings, lights, new_depth, light_path, samples, stat);
        }


        color += kd * (new_color * inv_pi * radiance) + (refrac * hit_refraction) + ggx * hit_reflectance * hit_reflectance_color + indirect * mix + trans * hit_sss;
    }


    // background for dome lights

    else
    {
        for (auto light : lights)
        {
            Dome_Light* domelight = nullptr;

            if (domelight = dynamic_cast<Dome_Light*>(light))
            {
                int id = 0;

                //if (depth[0] >= 6 || depth[0] >= 6 && depth[1] < 6 || depth[0] >= 6 && depth[2] < 10) id = -1;

                //else if (light_path.size() > 1 && light_path[0] == 1 && light_path[1] == 3) id = -1;

                if (depth[0] >= 6 && !domelight->visible) return vec3(0.0f);

                const float d = 0.0f;
                return domelight->return_light_throughput(d);
            }
        }
    }



    // nan filtering
    if (std::isnan(color.x) || std::isnan(color.y) || std::isnan(color.z))
    {
        //std::cout << color << "\n";
        color = vec3(0.5f);
    }

    // clamping
    if (color.x > 1.0f || color.y > 1.0f || color.z > 1.0f)
    {
        color = vec3(std::min(color.x, 8.0f), std::min(color.y, 8.0f), std::min(color.z, 8.0f));
    }

    return color;
}