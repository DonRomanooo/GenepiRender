#pragma once


#include "utils/vec3.h"
#include "utils/maths_utils.h"
#include "utils/matrix.h"
#include "utils/sampling_utils.h"


#ifndef LIGHT
#define LIGHT


enum class Light_Type
{
	Point,
	Distant,
	Square,
	Dome
};


class Light
{
public:
	std::string name;

	virtual ~Light() {}

	virtual vec3 return_ray_direction(const vec3& hit_position, const vec2& sample) = 0;
	virtual vec3 return_light_throughput(const float& d) = 0;
};


// point light
class Point_Light : public Light
{
public:
	vec3 position;
	vec3 color;
	float intensity;

	Point_Light() { name = "Point Light"; }

	Point_Light(vec3 position, float intensity, vec3 color) :
		position(position),
		intensity(intensity),
		color(color)
	{}

	vec3 return_light_throughput(const float& d) override;

	vec3 return_ray_direction(const vec3& hit_position, const vec2& sample = vec2(0.0f)) override;
};


// distant light
class Distant_Light : public Light
{
public:
	vec3 orientation;
	vec3 color;
	float intensity;
	float angle;

	Distant_Light() { name = "Distant Light"; }

	Distant_Light(vec3 orientation, vec3 color, float intensity, float angle) : 
		orientation(orientation),
		color(color),
		intensity(intensity),
		angle(angle)
	{}

	vec3 return_light_throughput(const float& d = 0.0f) override;

	vec3 return_ray_direction(const vec3& hit_position, const vec2& sample) override;
};


// square light
class Square_Light : public Light
{
public:
	vec3 color = vec3(1.0f);
	float intensity = 1.0f;
	vec3 normal;
	mat44 transform_mat = mat44();
	vec2 size = vec2(1.0f, 1.0f);

	Square_Light() { name = "Square Light"; }

	Square_Light(vec3 color, float intensity) :
		color(color),
		intensity(intensity)
	{}

	vec3 return_light_throughput(const float& d) override;

	vec3 return_ray_direction(const vec3& hit_position, const vec2& sample) override;
};


// dome light
class Dome_Light : public Light
{
public:
	vec3 color = vec3(1.0f);
	float intensity = 1.0f;
	bool visible = true;

	Dome_Light() { name = "Dome Light"; }

	Dome_Light(vec3 color, float intensity) :
		color(color),
		intensity(intensity)
	{}

	vec3 return_light_throughput(const float& d = 0.0f) override;

	vec3 return_ray_direction(const vec3& hit_normal, const vec2& sample) override;

	vec3 return_hdri_background();
};


#endif