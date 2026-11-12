#pragma once
#include "stroke_outline/Vec2.hpp"
#include "bezier/bezier.hpp"
#include <vector>
#include <optional>

namespace stroke_outline::bezier
{
	struct CurveIntersection
	{
		double t1, t2;
		Vec2 point;
	};

	std::vector<CurveIntersection>
		line_quadratic_intersections(Vec2 l0, Vec2 l1, const QuadraticBezier& curve);

	std::vector<CurveIntersection>
		line_cubic_intersections(Vec2 l0, Vec2 l1, const CubicBezier& curve);

	// Curve-curve intersections, by recursive subdivision: crossings are found
	// accurately at any scale. Known limits, harmless for the clipper (a
	// stroke's offset edges never exactly coincide with another curve):
	// - Curves that overlap along a stretch are only recognized when they are
	//   identical; a partial overlap (e.g. a curve and a piece of it) is reported
	//   as many intersections along the shared stretch.
	// - Curves that touch without crossing (tangent) usually aren't reported.
	std::vector<CurveIntersection> quadratic_quadratic_intersections(
		const QuadraticBezier& curve1,
		const QuadraticBezier& curve2);

	std::vector<CurveIntersection>
		quadratic_cubic_intersections(const QuadraticBezier& curve1, const CubicBezier& curve2);

	std::vector<CurveIntersection>
		cubic_cubic_intersections(const CubicBezier& curve1, const CubicBezier& curve2);
}
