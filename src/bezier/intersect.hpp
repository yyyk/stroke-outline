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

	std::vector<CurveIntersection> quadratic_quadratic_intersections(
		const QuadraticBezier& curve1,
		const QuadraticBezier& curve2);

	std::vector<CurveIntersection>
		quadratic_cubic_intersections(const QuadraticBezier& curve1, const CubicBezier& curve2);

	std::vector<CurveIntersection>
		cubic_cubic_intersections(const CubicBezier& curve1, const CubicBezier& curve2);

	std::optional<CurveIntersection> cubic_self_intersection(const CubicBezier& curve);
}
