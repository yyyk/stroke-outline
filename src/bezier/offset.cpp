// Offsetting based on bezierjs's Bezier.simple, utils.angle, Bezier.scale and
// Bezier.reduce, Copyright (c) 2023 Pomax (MIT license); see THIRD_PARTY_NOTICES.
// Not from bezierjs: the error control (pieces are also halved until their
// offset is within a tolerance of the true offset), the splitting where the
// offset folds, and the thresholds relative to the curve's size.
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

	// Shorter than 1e-6 of `size`, the size of the curve being offset, so the
	// test is the same at any scale.
	inline bool is_tiny(Vec2 v, double size)
	{
		return v.length_squared() < EPSILON.solver * size * size;
	}

	// The diagonal of the control points' bounding box.
	double curve_size(std::initializer_list<Vec2> points)
	{
		Vec2 lo = *points.begin();
		Vec2 hi = lo;
		for (const Vec2 p: points)
		{
			lo = { std::min(lo.x, p.x), std::min(lo.y, p.y) };
			hi = { std::max(hi.x, p.x), std::max(hi.y, p.y) };
		}
		return (hi - lo).length();
	}

	bool is_quadratic_too_small_to_subdivide(QuadraticBezier curve, double size)
	{
		const Vec2 chord = curve.p2 - curve.p0;
		const Vec2 d0 = curve.p1 - curve.p0;
		const Vec2 d1 = curve.p2 - curve.p1;
		return is_tiny(chord, size) || (is_tiny(d0, size) && is_tiny(d1, size));
	}

	bool is_cubic_too_small_to_subdivide(CubicBezier curve, double size)
	{
		const Vec2 chord = curve.p3 - curve.p0;
		const Vec2 d0 = curve.p1 - curve.p0;
		const Vec2 d1 = curve.p3 - curve.p2;
		return is_tiny(chord, size) || (is_tiny(d0, size) && is_tiny(d1, size));
	}

	bool is_quadratic_degenerate(Vec2 p0, Vec2 p1, Vec2 p2, double size)
	{
		const Vec2 d0 = quadratic_bezier_derivative(p0, p1, p2, 0.0);
		const Vec2 d1 = quadratic_bezier_derivative(p0, p1, p2, 1.0);
		return is_tiny(p2 - p0, size) || (is_tiny(d0, size) && is_tiny(d1, size));
	}

	bool is_cubic_degenerate(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double size)
	{
		const Vec2 d0 = cubic_bezier_derivative(p0, p1, p2, p3, 0.0);
		const Vec2 d1 = cubic_bezier_derivative(p0, p1, p2, p3, 1.0);
		return is_tiny(p3 - p0, size) || (is_tiny(d0, size) && is_tiny(d1, size));
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

	// At most this many halvings of a piece: a bound on the work.
	constexpr int MAX_DEPTH = 24;

	// The tolerance for the offset's distance from the true offset, as a share
	// of the offset distance: 0.2%, so 0.1% of a centered stroke's width.
	constexpr double RELATIVE_TOLERANCE = 2e-3;

	// A curve's point, first and second derivatives at t.
	struct PointAndDerivatives
	{
		Vec2 p, d1, d2;
	};

	PointAndDerivatives evaluate(const QuadraticBezier& c, double t)
	{
		return {
			quadratic_bezier(c.p0, c.p1, c.p2, t),
			quadratic_bezier_derivative(c.p0, c.p1, c.p2, t),
			2.0 * (c.p0 - 2.0 * c.p1 + c.p2),
		};
	}

	PointAndDerivatives evaluate(const CubicBezier& c, double t)
	{
		const Vec2 a = c.p0 - 2.0 * c.p1 + c.p2;
		const Vec2 b = c.p1 - 2.0 * c.p2 + c.p3;
		return {
			cubic_bezier(c.p0, c.p1, c.p2, c.p3, t),
			cubic_bezier_derivative(c.p0, c.p1, c.p2, c.p3, t),
			6.0 * ((1.0 - t) * a + t * b),
		};
	}

	// The largest distance from the true offset of `curve` by d, B(t) + d·N(t),
	// to `piece`, at seven points: each true-offset point is projected onto the
	// piece by Newton's method, starting at the same parameter.
	template <class C>
	double offset_error(const C& curve, const C& piece, double d)
	{
		double worst = 0.0;
		for (int k = 1; k < 8; ++k)
		{
			const double u = k / 8.0;
			const PointAndDerivatives b = evaluate(curve, u);
			const double speed = b.d1.length();
			if (!(speed > 0.0))
				continue;
			const Vec2 target = b.p + d * Vec2 { -b.d1.y, b.d1.x } / speed;
			double s = u;
			for (int step = 0; step < 4; ++step)
			{
				const PointAndDerivatives q = evaluate(piece, s);
				const Vec2 e = q.p - target;
				const double f = e.dot(q.d1);
				const double df = q.d1.dot(q.d1) + e.dot(q.d2);
				if (!(df > 0.0))
					break;
				s = std::clamp(s - f / df, 0.0, 1.0);
			}
			worst = std::max(worst, (evaluate(piece, s).p - target).length());
		}
		return worst;
	}

	// Samples of the fold value along the curve, to find where it changes sign.
	constexpr int FOLD_SAMPLES = 64;

	// Where the offset by d folds: the t values at which
	// 1 + d·(B'' × B')/|B'|³ (one minus d times the curvature) crosses zero.
	// There the true offset has a cusp, and halving near it would leave a piece
	// across it. Found by its sign changes between samples, then bisection.
	template <class C>
	std::vector<double> fold_points(const C& curve, double d)
	{
		const auto fold = [&](double t) {
			const PointAndDerivatives j = evaluate(curve, t);
			const double speed = j.d1.length();
			return 1.0 + d * j.d2.cross(j.d1) / (speed * speed * speed);
		};
		std::vector<double> ts;
		double t0 = 0.0;
		double f0 = fold(0.0);
		for (int i = 1; i <= FOLD_SAMPLES; ++i)
		{
			const double t1 = static_cast<double>(i) / FOLD_SAMPLES;
			const double f1 = fold(t1);
			if (std::isfinite(f0) && std::isfinite(f1) && f0 * f1 < 0.0)
			{
				double lo = t0;
				double hi = t1;
				const bool positive = f0 > 0.0;
				for (int k = 0; k < 60 && hi - lo > 1e-15; ++k)
				{
					const double mid = (lo + hi) / 2.0;
					if ((fold(mid) > 0.0) == positive)
						lo = mid;
					else
						hi = mid;
				}
				ts.push_back((lo + hi) / 2.0);
			}
			t0 = t1;
			f0 = f1;
		}
		return ts;
	}

	// Halves `curve` until each piece is simple and its offset is within
	// `tolerance` of the true offset, and appends the offsets to `result`. A
	// piece too small to halve, or at MAX_DEPTH, keeps the offset it has.
	template <class C, class SplitFn, class IsSimpleFn, class StopFn, class OffsetFn>
	void offset_until_accurate(
		C curve,
		double offset_distance,
		double tolerance,
		std::vector<C>& result,
		SplitFn split_fn,
		IsSimpleFn is_simple_fn,
		StopFn should_stop_fn,
		OffsetFn offset_fn)
	{
		std::vector<std::pair<C, int>> stack = { { curve, 0 } };

		while (!stack.empty())
		{
			const auto [segment, depth] = stack.back();
			stack.pop_back();

			std::optional<C> piece;
			if (is_simple_fn(segment))
				piece = offset_fn(segment);
			if (piece && offset_error(segment, *piece, offset_distance) <= tolerance)
			{
				result.push_back(*piece);
				continue;
			}
			if (depth >= MAX_DEPTH || should_stop_fn(segment))
			{
				if (piece)
					result.push_back(*piece);
				continue;
			}

			const auto [left, right] = split_fn(segment, 0.5);
			// Push right first so left is processed first (stable order).
			stack.push_back({ right, depth + 1 });
			stack.push_back({ left, depth + 1 });
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
		offset_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, double offset_distance, double size)
	{
		if (is_quadratic_degenerate(p0, p1, p2, size))
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
		offset_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double offset_distance, double size)
	{
		if (is_cubic_degenerate(p0, p1, p2, p3, size))
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
		const double size = curve_size({ p0, p1, p2 });
		// A curve collapsed to a point (or a non-finite one) has no offset.
		if (!(size > 0.0))
			return {};
		if (is_quadratic_degenerate(p0, p1, p2, size))
			return {};

		const QuadraticBezier source_curve { p0, p1, p2 };
		// Split at the extrema, as Bezier.reduce does, and where the offset folds.
		auto split_points = quadratic_split_points(p0, p1, p2);
		for (const double t: fold_points(source_curve, offset_distance))
			split_points.push_back(t);
		split_points = unique_sorted_ts(std::move(split_points));
		const auto curves =
			split_curve_at_ts(source_curve, split_points, [](QuadraticBezier curve, double t) {
				return split_quadratic_bezier(curve.p0, curve.p1, curve.p2, t);
			});
		const double tolerance = RELATIVE_TOLERANCE * std::abs(offset_distance);
		std::vector<QuadraticBezier> result;
		for (const auto& curve: curves)
			offset_until_accurate(
				curve,
				offset_distance,
				tolerance,
				result,
				[](QuadraticBezier c, double t) {
					return split_quadratic_bezier(c.p0, c.p1, c.p2, t);
				},
				[](QuadraticBezier curve) {
					return is_quadratic_simple(curve.p0, curve.p1, curve.p2);
				},
				[size](QuadraticBezier curve) {
					return is_quadratic_too_small_to_subdivide(curve, size);
				},
				[offset_distance, size](QuadraticBezier c) {
					return offset_quadratic(c.p0, c.p1, c.p2, offset_distance, size);
				});
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
		const double size = curve_size({ p0, p1, p2, p3 });
		// A curve collapsed to a point (or a non-finite one) has no offset.
		if (!(size > 0.0))
			return {};
		if (is_cubic_degenerate(p0, p1, p2, p3, size))
			return {};

		const CubicBezier source_curve { p0, p1, p2, p3 };
		// Split at the extrema, as Bezier.reduce does, and where the offset folds.
		auto split_points = cubic_split_points(p0, p1, p2, p3);
		for (const double t: fold_points(source_curve, offset_distance))
			split_points.push_back(t);
		split_points = unique_sorted_ts(std::move(split_points));
		const auto curves =
			split_curve_at_ts(source_curve, split_points, [](CubicBezier curve, double t) {
				return split_cubic_bezier(curve.p0, curve.p1, curve.p2, curve.p3, t);
			});
		const double tolerance = RELATIVE_TOLERANCE * std::abs(offset_distance);
		std::vector<CubicBezier> result;
		for (const auto& curve: curves)
			offset_until_accurate(
				curve,
				offset_distance,
				tolerance,
				result,
				[](CubicBezier c, double t) {
					return split_cubic_bezier(c.p0, c.p1, c.p2, c.p3, t);
				},
				[](CubicBezier curve) {
					return is_cubic_simple(curve.p0, curve.p1, curve.p2, curve.p3);
				},
				[size](CubicBezier curve) {
					return is_cubic_too_small_to_subdivide(curve, size);
				},
				[offset_distance, size](CubicBezier c) {
					return offset_cubic(c.p0, c.p1, c.p2, c.p3, offset_distance, size);
				});
		return result;
	}
}
