// Offsetting based on bezierjs's Bezier.simple, utils.angle, Bezier.scale and
// Bezier.reduce, Copyright (c) 2023 Pomax (MIT license); see THIRD_PARTY_NOTICES.
#include <optional>
#include <numbers>
#include <cmath>
#include <initializer_list>
#include <algorithm>
#include <array>
#include "bezier/bezier.hpp"
#include "math/Eps.hpp"
#include "math/geom.hpp"
#include "./utils.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	std::optional<Vec2> compute_normal(Vec2 tangent)
	{
		const auto normalized_tangent = tangent.normalize();
		if (!normalized_tangent)
			return std::nullopt;

		const Vec2 normal = perpendicular(*normalized_tangent);
		return normal;
	}

	double angle(Vec2 p1, Vec2 p2, Vec2 p3)
	{
		const Vec2 d1 = p1 - p2;
		const Vec2 d2 = p3 - p2;
		return std::atan2(d1.cross(d2), d1.dot(d2));
	}

	bool is_simple(Vec2 t0, Vec2 t1)
	{
		const auto n0 = compute_normal(t0);
		if (!n0)
			return false;
		const auto n1 = compute_normal(t1);
		if (!n1)
			return false;

		return std::abs(std::acos(n0->dot(*n1))) < std::numbers::pi / 3.0;
	}

	bool is_quadratic_simple(Vec2 p0, Vec2 p1, Vec2 p2)
	{
		const Vec2 t0 = quadratic_bezier_derivative(p0, p1, p2, 0.0);
		const Vec2 t1 = quadratic_bezier_derivative(p0, p1, p2, 1.0);
		return is_simple(t0, t1);
	}

	bool is_cubic_simple(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
	{
		const double angle0 = angle(p0, p3, p1);
		const double angle1 = angle(p0, p3, p2);
		if ((angle0 > 0.0 && angle1 < 0.0) || (angle0 < 0.0 && angle1 > 0.0))
			return false;
		const Vec2 t0 = cubic_bezier_derivative(p0, p1, p2, p3, 0.0);
		const Vec2 t1 = cubic_bezier_derivative(p0, p1, p2, p3, 1.0);
		return is_simple(t0, t1);
	}

	bool is_valid_split_t(double t)
	{
		return std::isfinite(t) && t > EPSILON.param && t < 1.0 - EPSILON.param;
	}

	inline bool is_tiny(Vec2 v)
	{
		return v.length_squared() < EPSILON.solver;
	}

	bool is_quadratic_too_small_to_subdivide(QuadraticBezier curve)
	{
		const Vec2 chord = curve.p2 - curve.p0;
		const Vec2 d0 = curve.p1 - curve.p0;
		const Vec2 d1 = curve.p2 - curve.p1;
		return is_tiny(chord) || (is_tiny(d0) && is_tiny(d1));
	}

	bool is_cubic_too_small_to_subdivide(CubicBezier curve)
	{
		const Vec2 chord = curve.p3 - curve.p0;
		const Vec2 d0 = curve.p1 - curve.p0;
		const Vec2 d1 = curve.p3 - curve.p2;
		return is_tiny(chord) || (is_tiny(d0) && is_tiny(d1));
	}

	bool is_quadratic_degenerate(Vec2 p0, Vec2 p1, Vec2 p2)
	{
		const Vec2 d0 = quadratic_bezier_derivative(p0, p1, p2, 0.0);
		const Vec2 d1 = quadratic_bezier_derivative(p0, p1, p2, 1.0);
		return is_tiny(p2 - p0) || (is_tiny(d0) && is_tiny(d1));
	}

	bool is_cubic_degenerate(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
	{
		const Vec2 d0 = cubic_bezier_derivative(p0, p1, p2, p3, 0.0);
		const Vec2 d1 = cubic_bezier_derivative(p0, p1, p2, p3, 1.0);
		return is_tiny(p3 - p0) || (is_tiny(d0) && is_tiny(d1));
	}

	// Every point of `points` lies on the line through `from` and `to` (compared
	// by angle, so at any scale). Such a curve is geometrically straight even if
	// its control points aren't on its endpoints.
	bool is_on_chord(Vec2 from, Vec2 to, std::initializer_list<Vec2> points)
	{
		const Vec2 chord = to - from;
		const double chord_len = chord.length();
		if (!(chord_len > 0.0))
			return false;
		return std::ranges::all_of(points, [&](Vec2 p) {
			const Vec2 v = p - from;
			return std::abs(chord.cross(v)) <= EPSILON.param * chord_len * v.length();
		});
	}

	std::vector<double> unique_sorted_ts(std::vector<double> ts)
	{
		std::erase_if(ts, [](double t) { return !std::isfinite(t); });
		std::sort(ts.begin(), ts.end());
		std::vector<double> result;
		result.reserve(ts.size());
		for (const double t: ts)
			if (result.empty() || std::abs(t - result.back()) >= EPSILON.param)
				result.push_back(t);
		return result;
	}

	template <class C, class SplitFn>
	std::vector<C>
		split_curve_at_ts(C curve, const std::vector<double>& split_points, SplitFn split_fn)
	{
		std::vector<C> curves;
		C current = curve;
		double last_t = 0.0;

		for (const double t: split_points)
		{
			if (!is_valid_split_t(t))
				continue;
			const double local_t = (t - last_t) / (1.0 - last_t);
			const auto [left, right] = split_fn(current, local_t);
			curves.push_back(left);
			current = right;
			last_t = t;
		}
		curves.push_back(current);
		return curves;
	}

	template <class C, class SplitFn, class IsSafeFn, class StopFn>
	void subdivide_until_safe(
		C curve,
		std::vector<C>& safe_curves,
		SplitFn split_fn,
		IsSafeFn is_safe_fn,
		StopFn should_stop_fn)
	{
		std::vector<C> stack = { curve };

		while (!stack.empty())
		{
			C segment = stack.back();
			stack.pop_back();

			if (is_safe_fn(segment))
			{
				safe_curves.push_back(segment);
				continue;
			}
			if (should_stop_fn(segment))
				continue;

			const auto [left, right] = split_fn(segment, 0.5);
			// Push right first so left is processed first (stable order).
			stack.push_back(right);
			stack.push_back(left);
		}
	}

	std::optional<std::pair<Vec2, Vec2>>
		offset_line_segment(Vec2 p0, Vec2 p1, double offset_distance)
	{
		const Vec2 tangent = p1 - p0;
		// if (is_tiny(tangent))
		// 	return std::nullopt;

		const auto normal = compute_normal(tangent);
		if (!normal)
			return std::nullopt;

		const Vec2 offset_vector = *normal * offset_distance;
		const Vec2 offset_p0 = p0 + offset_vector;
		const Vec2 offset_p1 = p1 + offset_vector;
		return std::make_pair(offset_p0, offset_p1);
	}

	std::optional<std::array<Vec2, 3>> offset_endpoints(
		Vec2 start,
		Vec2 start_derivative,
		Vec2 end,
		Vec2 end_derivative,
		double offset_distance)
	{
		const auto start_normal = compute_normal(start_derivative);
		if (!start_normal)
			return std::nullopt;
		const auto end_normal = compute_normal(end_derivative);
		if (!end_normal)
			return std::nullopt;

		const Vec2 start_point = start + *start_normal * 10.0;
		const Vec2 end_point = end + *end_normal * 10.0;
		const auto anchor = line_intersection(start_point, start, end_point, end);
		if (!anchor)
			return std::nullopt;
		const Vec2 new_start = start + *start_normal * offset_distance;
		const Vec2 new_end = end + *end_normal * offset_distance;
		return std::array<Vec2, 3> { *anchor, new_start, new_end };
	}

	std::vector<double> quadratic_split_points(Vec2 p0, Vec2 p1, Vec2 p2)
	{
		std::vector<double> ts;
		const auto extrema_x = find_derivative_extrema_for_quadratic(p0.x, p1.x, p2.x);
		if (extrema_x)
			ts.push_back(*extrema_x);
		const auto extrema_y = find_derivative_extrema_for_quadratic(p0.y, p1.y, p2.y);
		if (extrema_y)
			ts.push_back(*extrema_y);
		return unique_sorted_ts(std::move(ts));
	}

	std::vector<double> cubic_split_points(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
	{
		std::vector<double> ts;
		const auto extrema_x = find_derivative_extrema_for_cubic(p0.x, p1.x, p2.x, p3.x);
		for (int i = 0; i < extrema_x.count; ++i)
			ts.push_back(extrema_x.t[i]);
		const auto extrema_y = find_derivative_extrema_for_cubic(p0.y, p1.y, p2.y, p3.y);
		for (int i = 0; i < extrema_y.count; ++i)
			ts.push_back(extrema_y.t[i]);
		return unique_sorted_ts(std::move(ts));
	}

	std::optional<QuadraticBezier>
		offset_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, double offset_distance)
	{
		if (is_quadratic_degenerate(p0, p1, p2))
			return std::nullopt;

		const Vec2 d0 = quadratic_bezier_derivative(p0, p1, p2, 0.0);
		const Vec2 d1 = quadratic_bezier_derivative(p0, p1, p2, 1.0);
		const auto offset_endpoints_result = offset_endpoints(p0, d0, p2, d1, offset_distance);
		if (!offset_endpoints_result)
			return std::nullopt;
		const auto [o, new_p0, new_p2] = *offset_endpoints_result;
		const Vec2 p0_derivative_end = new_p0 + d0;
		const auto new_p1 = line_intersection(new_p0, p0_derivative_end, o, p1);
		if (!new_p1)
			return std::nullopt;
		return QuadraticBezier { new_p0, *new_p1, new_p2 };
	}

	std::optional<CubicBezier>
		offset_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double offset_distance)
	{
		if (is_cubic_degenerate(p0, p1, p2, p3))
			return std::nullopt;

		const Vec2 d0 = cubic_bezier_derivative(p0, p1, p2, p3, 0.0);
		const Vec2 d3 = cubic_bezier_derivative(p0, p1, p2, p3, 1.0);
		const auto offset_endpoints_result = offset_endpoints(p0, d0, p3, d3, offset_distance);
		if (!offset_endpoints_result)
			return std::nullopt;
		const auto [o, new_p0, new_p3] = *offset_endpoints_result;
		const Vec2 p0_derivative_end = new_p0 + d0;
		const Vec2 p3_derivative_end = new_p3 + d3;
		const auto new_p1 = line_intersection(new_p0, p0_derivative_end, o, p1);
		if (!new_p1)
			return std::nullopt;
		const auto new_p2 = line_intersection(new_p3, p3_derivative_end, o, p2);
		if (!new_p2)
			return std::nullopt;
		return CubicBezier { new_p0, *new_p1, *new_p2, new_p3 };
	}
}

namespace stroke_outline::bezier
{
	std::vector<QuadraticBezier>
		offset_quadratic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, double offset_distance)
	{
		// A straight curve's offset is the curve moved sideways. (The general
		// construction intersects the end normals, which are parallel here, so
		// it would fail and drop the curve.)
		if (is_on_chord(p0, p2, { p1 }))
		{
			const auto shifted = offset_line_segment(p0, p2, offset_distance);
			if (!shifted)
				return {};
			const Vec2 by = shifted->first - p0;
			return { QuadraticBezier { p0 + by, p1 + by, p2 + by } };
		}
		if (is_quadratic_degenerate(p0, p1, p2))
			return {};

		const auto split_points = quadratic_split_points(p0, p1, p2);
		const QuadraticBezier source_curve { p0, p1, p2 };
		const auto curves =
			split_curve_at_ts(source_curve, split_points, [](QuadraticBezier curve, double t) {
				return split_quadratic_bezier(curve.p0, curve.p1, curve.p2, t);
			});
		std::vector<QuadraticBezier> safe_curves;
		for (const auto& curve: curves)
			subdivide_until_safe(
				curve,
				safe_curves,
				[](QuadraticBezier c, double t) {
					return split_quadratic_bezier(c.p0, c.p1, c.p2, t);
				},
				[](QuadraticBezier curve) {
					return is_quadratic_simple(curve.p0, curve.p1, curve.p2);
				},
				[](QuadraticBezier curve) {
					return is_quadratic_too_small_to_subdivide(curve);
				});
		std::vector<QuadraticBezier> result;
		for (const auto& curve: safe_curves)
		{
			const auto offset_result =
				offset_quadratic(curve.p0, curve.p1, curve.p2, offset_distance);
			if (offset_result)
				result.push_back(*offset_result);
		}
		return result;
	}

	std::vector<CubicBezier>
		offset_cubic_bezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double offset_distance)
	{
		// A straight curve's offset is the curve moved sideways (see the
		// quadratic case).
		if (is_on_chord(p0, p3, { p1, p2 }))
		{
			const auto shifted = offset_line_segment(p0, p3, offset_distance);
			if (!shifted)
				return {};
			const Vec2 by = shifted->first - p0;
			return { CubicBezier { p0 + by, p1 + by, p2 + by, p3 + by } };
		}
		if (is_cubic_degenerate(p0, p1, p2, p3))
			return {};

		const auto split_points = cubic_split_points(p0, p1, p2, p3);
		const CubicBezier source_curve { p0, p1, p2, p3 };
		const auto curves =
			split_curve_at_ts(source_curve, split_points, [](CubicBezier curve, double t) {
				return split_cubic_bezier(curve.p0, curve.p1, curve.p2, curve.p3, t);
			});
		std::vector<CubicBezier> safe_curves;
		for (const auto& curve: curves)
			subdivide_until_safe(
				curve,
				safe_curves,
				[](CubicBezier c, double t) {
					return split_cubic_bezier(c.p0, c.p1, c.p2, c.p3, t);
				},
				[](CubicBezier curve) {
					return is_cubic_simple(curve.p0, curve.p1, curve.p2, curve.p3);
				},
				[](CubicBezier curve) { return is_cubic_too_small_to_subdivide(curve); });
		std::vector<CubicBezier> result;
		for (const auto& curve: safe_curves)
		{
			const auto offset_result =
				offset_cubic(curve.p0, curve.p1, curve.p2, curve.p3, offset_distance);
			if (offset_result)
				result.push_back(*offset_result);
		}
		return result;
	}
}
