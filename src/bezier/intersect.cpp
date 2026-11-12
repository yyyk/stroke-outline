#include "intersect.hpp"
#include "math/Eps.hpp"
#include "math/polynomial.hpp"
#include "math/AABB.hpp"
#include "math/geom.hpp"
#include <algorithm>
#include <cmath>

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	inline constexpr Vec2 to_line_coords(Vec2 p, Vec2 l0, Vec2 delta, double len_sq)
	{
		const Vec2 relative_p = p - l0;
		return {
			relative_p.dot(delta) / len_sq,
			delta.cross(relative_p) / len_sq,
		};
	}

	struct CurveSeg
	{
		bool is_cubic;
		Vec2 p0, p1, p2, p3;
		double t0, t1;

		AABB aabb() const
		{
			if (is_cubic)
				return compute_cubic_bounds(p0, p1, p2, p3);
			else
				return compute_quadratic_bounds(p0, p1, p2);
		}

		bool is_coincident_with(const CurveSeg& other) const
		{
			if (!is_cubic && !other.is_cubic)
			{
				bool forward =
					points_are_too_close(p0, other.p0) && points_are_too_close(p1, other.p1)
					&& points_are_too_close(p2, other.p2);
				bool backward =
					points_are_too_close(p0, other.p2) && points_are_too_close(p1, other.p1)
					&& points_are_too_close(p2, other.p0);
				return forward || backward;
			}
			else if (is_cubic && other.is_cubic)
			{
				bool forward =
					points_are_too_close(p0, other.p0) && points_are_too_close(p1, other.p1)
					&& points_are_too_close(p2, other.p2) && points_are_too_close(p3, other.p3);
				bool backward =
					points_are_too_close(p0, other.p3) && points_are_too_close(p1, other.p2)
					&& points_are_too_close(p2, other.p1) && points_are_too_close(p3, other.p0);
				return forward || backward;
			}
			return false;
		}

		std::pair<CurveSeg, CurveSeg> split() const
		{
			if (!is_cubic)
			{
				auto [left, right] = split_quadratic_bezier(p0, p1, p2, 0.5);
				const double mid_t = (t0 + t1) * 0.5;
				return {
					CurveSeg {
						.is_cubic = false,
						.p0 = left.p0,
						.p1 = left.p1,
						.p2 = left.p2,
						.t0 = t0,
						.t1 = mid_t },
					CurveSeg {
						.is_cubic = false,
						.p0 = right.p0,
						.p1 = right.p1,
						.p2 = right.p2,
						.t0 = mid_t,
						.t1 = t1 },
				};
			}
			else
			{
				auto [left, right] = split_cubic_bezier(p0, p1, p2, p3, 0.5);
				const double mid_t = (t0 + t1) * 0.5;
				return {
					CurveSeg {
						.is_cubic = true,
						.p0 = left.p0,
						.p1 = left.p1,
						.p2 = left.p2,
						.p3 = left.p3,
						.t0 = t0,
						.t1 = mid_t },
					CurveSeg {
						.is_cubic = true,
						.p0 = right.p0,
						.p1 = right.p1,
						.p2 = right.p2,
						.p3 = right.p3,
						.t0 = mid_t,
						.t1 = t1 },
				};
			}
		}

		bool is_flat() const
		{
			if (!is_cubic)
			{
				const Vec2 d = p2 - p0;
				const double len_sq = d.length_squared();
				if (len_sq < EPSILON.solver)
					return true;
				const Vec2 v = p1 - p0;
				const double cross = v.cross(d);
				return cross * cross / len_sq <= EPSILON.solver;
			}
			else
			{
				const Vec2 d = p3 - p0;
				const double len_sq = d.length_squared();
				if (len_sq < EPSILON.solver)
					return true;
				const Vec2 v1 = p1 - p0;
				const Vec2 v2 = p2 - p0;
				const double cross1 = v1.cross(d);
				const double cross2 = v2.cross(d);
				return std::max(cross1 * cross1, cross2 * cross2) / len_sq <= EPSILON.solver;
			}
		}

		Vec2 start_point() const { return p0; }
		Vec2 end_point() const { return is_cubic ? p3 : p2; }
		double start_t() const { return t0; }
		double end_t() const { return t1; }
	};

	const unsigned int MAX_DEPTH = 50;

	// Partial overlaps and tangent contacts aren't handled; see intersect.hpp.
	void curve_curve_recursive(
		const CurveSeg& a,
		const CurveSeg& b,
		unsigned int depth,
		std::vector<CurveIntersection>& out)
	{
		if (a.is_coincident_with(b))
			return;
		if (!a.aabb().intersects(b.aabb()))
			return;
		if (depth >= MAX_DEPTH || (a.is_flat() && b.is_flat()))
		{
			if (auto hit = line_segment_intersection(
					a.start_point(),
					a.end_point(),
					b.start_point(),
					b.end_point()))
			{
				auto [s, t] = *hit;
				const double ta = a.start_t() + (a.end_t() - a.start_t()) * s;
				const double tb = b.start_t() + (b.end_t() - b.start_t()) * t;
				out.push_back({ ta, tb, Vec2::lerp(a.start_point(), a.end_point(), s) });
			}
			return;
		}
		auto [a1, a2] = a.split();
		auto [b1, b2] = b.split();
		curve_curve_recursive(a1, b1, depth + 1, out);
		curve_curve_recursive(a1, b2, depth + 1, out);
		curve_curve_recursive(a2, b1, depth + 1, out);
		curve_curve_recursive(a2, b2, depth + 1, out);
	}

	void dedup_intersections(std::vector<CurveIntersection>& results, double eps)
	{
		if (results.size() <= 1)
			return;

		std::sort(
			results.begin(),
			results.end(),
			[](const CurveIntersection& a, const CurveIntersection& b) { return a.t1 < b.t1; });

		const auto new_end = std::unique(
			results.begin(),
			results.end(),
			[eps](const CurveIntersection& a, const CurveIntersection& b) {
				return std::abs(a.t1 - b.t1) < eps && std::abs(a.t2 - b.t2) < eps;
			});

		results.erase(new_end, results.end());
	}
}

namespace stroke_outline::bezier
{
	std::vector<CurveIntersection>
		line_quadratic_intersections(Vec2 l0, Vec2 l1, const QuadraticBezier& curve)
	{
		const Vec2 delta = l1 - l0;
		const double len_sq = delta.length_squared();
		if (len_sq < EPSILON.solver)
			return {};

		const Vec2 p0 = to_line_coords(curve.p0, l0, delta, len_sq);
		const Vec2 p1 = to_line_coords(curve.p1, l0, delta, len_sq);
		const Vec2 p2 = to_line_coords(curve.p2, l0, delta, len_sq);

		// y(t) = (1-t)^2 * y0 + 2(1-t)t * y1 + t^2 * y2
		//      = (y0 - 2y1 + y2)t^2 + (-2y0 + 2y1)t + y0
		const double a = p0.y - 2.0 * p1.y + p2.y;
		const double b = -2.0 * p0.y + 2.0 * p1.y;
		const double c = p0.y;

		std::vector<CurveIntersection> intersections;
		const auto roots = solve_quadratic(a, b, c, std::make_pair(0.0, 1.0));
		for (double t_curve: roots)
		{
			const Vec2 point = quadratic_bezier(curve.p0, curve.p1, curve.p2, t_curve);
			const Vec2 line_coords = to_line_coords(point, l0, delta, len_sq);
			if (line_coords.x >= -EPSILON.param && line_coords.x <= 1.0 + EPSILON.param)
			{
				const double t_line = std::clamp(line_coords.x, 0.0, 1.0);
				intersections.push_back({ t_line, t_curve, point });
			}
		}
		return intersections;
	}

	std::vector<CurveIntersection>
		line_cubic_intersections(Vec2 l0, Vec2 l1, const CubicBezier& curve)
	{
		const Vec2 delta = l1 - l0;
		const double len_sq = delta.length_squared();
		if (len_sq < EPSILON.solver)
			return {};

		const Vec2 p0 = to_line_coords(curve.p0, l0, delta, len_sq);
		const Vec2 p1 = to_line_coords(curve.p1, l0, delta, len_sq);
		const Vec2 p2 = to_line_coords(curve.p2, l0, delta, len_sq);
		const Vec2 p3 = to_line_coords(curve.p3, l0, delta, len_sq);

		// y(t) = (1-t)^3*y0 + 3t(1-t)^2*y1 + 3(1-t)t^2*y2 + t^3*y3
		//      = (-y0+3y1-3y2+y3)t^3 + (3y0-6y1+3y2)t^2 + (-3y0+3y1)t + y0
		const double a = -p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y;
		const double b = 3.0 * p0.y - 6.0 * p1.y + 3.0 * p2.y;
		const double c = -3.0 * p0.y + 3.0 * p1.y;
		const double d = p0.y;

		std::vector<CurveIntersection> intersections;
		const auto roots = solve_cubic(a, b, c, d, std::make_pair(0.0, 1.0));
		for (double t_curve: roots)
		{
			const Vec2 point = cubic_bezier(curve.p0, curve.p1, curve.p2, curve.p3, t_curve);
			const Vec2 line_coords = to_line_coords(point, l0, delta, len_sq);
			if (line_coords.x >= -EPSILON.param && line_coords.x <= 1.0 + EPSILON.param)
			{
				const double t_line = std::clamp(line_coords.x, 0.0, 1.0);
				intersections.push_back({ t_line, t_curve, point });
			}
		}
		return intersections;
	}

	std::vector<CurveIntersection> quadratic_quadratic_intersections(
		const QuadraticBezier& curve1,
		const QuadraticBezier& curve2)
	{
		CurveSeg a {
			.is_cubic = false,
			.p0 = curve1.p0,
			.p1 = curve1.p1,
			.p2 = curve1.p2,
			.t0 = 0.0,
			.t1 = 1.0
		};
		CurveSeg b {
			.is_cubic = false,
			.p0 = curve2.p0,
			.p1 = curve2.p1,
			.p2 = curve2.p2,
			.t0 = 0.0,
			.t1 = 1.0
		};
		std::vector<CurveIntersection> intersections;
		curve_curve_recursive(a, b, 0, intersections);
		dedup_intersections(intersections, EPSILON.param * 10.0);
		return intersections;
	}

	std::vector<CurveIntersection>
		quadratic_cubic_intersections(const QuadraticBezier& curve1, const CubicBezier& curve2)
	{
		CurveSeg a {
			.is_cubic = false,
			.p0 = curve1.p0,
			.p1 = curve1.p1,
			.p2 = curve1.p2,
			.t0 = 0.0,
			.t1 = 1.0
		};
		CurveSeg b {
			.is_cubic = true,
			.p0 = curve2.p0,
			.p1 = curve2.p1,
			.p2 = curve2.p2,
			.p3 = curve2.p3,
			.t0 = 0.0,
			.t1 = 1.0
		};
		std::vector<CurveIntersection> intersections;
		curve_curve_recursive(a, b, 0, intersections);
		dedup_intersections(intersections, EPSILON.param * 10.0);
		return intersections;
	}

	std::vector<CurveIntersection>
		cubic_cubic_intersections(const CubicBezier& curve1, const CubicBezier& curve2)
	{
		CurveSeg a {
			.is_cubic = true,
			.p0 = curve1.p0,
			.p1 = curve1.p1,
			.p2 = curve1.p2,
			.p3 = curve1.p3,
			.t0 = 0.0,
			.t1 = 1.0
		};
		CurveSeg b {
			.is_cubic = true,
			.p0 = curve2.p0,
			.p1 = curve2.p1,
			.p2 = curve2.p2,
			.p3 = curve2.p3,
			.t0 = 0.0,
			.t1 = 1.0
		};
		std::vector<CurveIntersection> intersections;
		curve_curve_recursive(a, b, 0, intersections);
		dedup_intersections(intersections, EPSILON.param * 10.0);
		return intersections;
	}
}
