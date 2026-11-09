#pragma once

#include <cassert>
#include <utility>
#include <vector>
#include "stroke_outline/Vec2.hpp"
#include "math/AABB.hpp"

namespace stroke_outline::bezier
{
	struct QuadraticBezier
	{
		Vec2 p0, p1, p2;
	};

	struct CubicBezier
	{
		Vec2 p0, p1, p2, p3;
	};

	constexpr Vec2 quadratic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, double t)
	{
		assert(t >= 0.0 && t <= 1.0);
		t = std::clamp(t, 0.0, 1.0);
		const double u = 1.0 - t;

		Vec2 p = u * u * p0;
		p += 2.0 * u * t * p1;
		p += t * t * p2;

		return p;
	}

	constexpr Vec2 cubic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double t)
	{
		assert(t >= 0.0 && t <= 1.0);
		t = std::clamp(t, 0.0, 1.0);
		const double u = 1.0 - t;
		const double tt = t * t;
		const double uu = u * u;

		Vec2 p = uu * u * p0;
		p += 3.0 * uu * t * p1;
		p += 3.0 * u * tt * p2;
		p += tt * t * p3;

		return p;
	}

	constexpr Vec2 quadratic_bezier_derivative(Vec2 p0, Vec2 p1, Vec2 p2, double t)
	{
		// assert(t >= 0.0 && t <= 1.0);
		// t = std::clamp(t, 0.0, 1.0);
		const double u = 1.0 - t;
		return 2.0 * u * (p1 - p0) + 2.0 * t * (p2 - p1);
	}

	constexpr Vec2 cubic_bezier_derivative(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double t)
	{
		// assert(t >= 0.0 && t <= 1.0);
		// t = std::clamp(t, 0.0, 1.0);
		const double u = 1.0 - t;
		return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
	}

	constexpr std::pair<QuadraticBezier, QuadraticBezier>
		split_quadratic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, double t)
	{
		assert(t >= 0.0 && t <= 1.0);
		t = std::clamp(t, 0.0, 1.0);

		const Vec2 a = Vec2::lerp(p0, p1, t);
		const Vec2 b = Vec2::lerp(p1, p2, t);
		const Vec2 c = Vec2::lerp(a, b, t);

		return { QuadraticBezier { p0, a, c }, QuadraticBezier { c, b, p2 } };
	}

	constexpr std::pair<CubicBezier, CubicBezier>
		split_cubic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double t)
	{
		assert(t >= 0.0 && t <= 1.0);
		t = std::clamp(t, 0.0, 1.0);

		const Vec2 a = Vec2::lerp(p0, p1, t);
		const Vec2 b = Vec2::lerp(p1, p2, t);
		const Vec2 c = Vec2::lerp(p2, p3, t);

		const Vec2 d = Vec2::lerp(a, b, t);
		const Vec2 e = Vec2::lerp(b, c, t);

		const Vec2 f = Vec2::lerp(d, e, t);

		return { CubicBezier { p0, a, d, f }, CubicBezier { f, e, c, p3 } };
	}

	std::vector<CubicBezier> arc_to_cubic_beziers(
		Vec2 center,
		double radius,
		double start_angle_radian,
		double end_angle_radian,
		bool counterclockwise);

	double arc_length_quadratic(Vec2 p0, Vec2 p1, Vec2 p2);
	double arc_length_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3);

	std::vector<QuadraticBezier>
		offset_quadratic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, double offset_distance);
	std::vector<CubicBezier>
		offset_cubic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double offset_distance);

	AABB compute_quadratic_bounds(Vec2 p0, Vec2 p1, Vec2 p2);
	AABB compute_cubic_bounds(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3);

	// optional API
	// inline constexpr double DEFAULT_FLATTEN_TOLERANCE = 0.5;
	// std::optional<std::vector<Vec2>> flatten_quadratic(
	// 	Vec2 p0,
	// 	Vec2 p1,
	// 	Vec2 p2,
	// 	double tolerance = DEFAULT_FLATTEN_TOLERANCE);
	// std::optional<std::vector<Vec2>> flatten_cubic(
	// 	Vec2 p0,
	// 	Vec2 p1,
	// 	Vec2 p2,
	// 	Vec2 p3,
	// 	double tolerance = DEFAULT_FLATTEN_TOLERANCE);
}
