#pragma once

#include <optional>
#include <utility>
#include <algorithm>
#include "stroke_outline/Vec2.hpp"
#include "math/Eps.hpp"

namespace stroke_outline
{
	constexpr bool points_are_too_close(Vec2 p1, Vec2 p2)
	{
		return (p1 - p2).length_squared() <= EPSILON.point * EPSILON.point;
	}

	// Two lines in parametric form:
	// - Line A through p1, p2, direction r = p2 - p1:  A(t) = p1 + t·r
	// - Line B through p3, p4, direction s = p4 - p3:  B(u) = p3 + u·s
	// intersect => A(t) = B(u)
	// => p1 + t·r = p3 + u·s
	// => t·r - u·s = p3 - p1
	// => (t·r - u·s) x s = (p3 - p1) x s
	// => t·(r x s) - u·(s x s) = (p3 - p1) x s
	// => t·(r x s) = (p3 - p1) x s
	inline std::optional<Vec2> line_intersection(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4)
	{
		const Vec2 r = p2 - p1;
		const Vec2 s = p4 - p3;
		const double r_cross_s = r.cross(s);
		// Parallel or collinear: compare the angle between the lines (|sin| < eps),
		// not the raw cross product, which grows with the square of their length.
		if (std::abs(r_cross_s) <= EPSILON.param * r.length() * s.length())
			return std::nullopt;

		const double t = (p3 - p1).cross(s) / r_cross_s;
		return p1 + t * r;
	}

	inline std::optional<std::pair<double, double>> line_segment_intersection(
		Vec2 p1,
		Vec2 p2,
		Vec2 p3,
		Vec2 p4,
		double eps = EPSILON.param)
	{
		const Vec2 a = p2 - p1;
		const Vec2 d = p4 - p3;
		const Vec2 c = p3 - p1;
		const double a_len_sq = a.length_squared();
		const double d_len_sq = d.length_squared();
		const double tiny = std::max(EPSILON.solver, eps * eps);

		// Degenerate: one or both segments are points.
		const bool a_is_point = a_len_sq <= tiny;
		const bool d_is_point = d_len_sq <= tiny;

		if (a_is_point && d_is_point)
		{
			if (points_are_too_close(p1, p3))
				return std::make_pair(0.0, 0.0);
			return std::nullopt;
		}
		else if (a_is_point)
		{
			const double t = std::clamp((p1 - p3).dot(d) / d_len_sq, 0.0, 1.0);
			if (points_are_too_close(p3 + d * t, p1))
				return std::make_pair(0.0, t);
			return std::nullopt;
		}
		else if (d_is_point)
		{
			const double s = std::clamp((p3 - p1).dot(a) / a_len_sq, 0.0, 1.0);
			if (points_are_too_close(p1 + a * s, p3))
				return std::make_pair(s, 0.0);
			return std::nullopt;
		}

		// Both are real segments: transversal solve.
		const double denom = a.cross(d);
		// Scale-relative parallel test (|sin θ| < eps). Safe: a, d are non-degenerate here.
		if (denom * denom < eps * eps * a_len_sq * d_len_sq)
			return std::nullopt;  // parallel/collinear — overlap handling intentionally
								  // dropped.

		const double s = c.cross(d) / denom;
		const double t = c.cross(a) / denom;
		if (s >= -eps && s <= 1.0 + eps && t >= -eps && t <= 1.0 + eps)
			return std::make_pair(std::clamp(s, 0.0, 1.0), std::clamp(t, 0.0, 1.0));
		return std::nullopt;
	}
}
