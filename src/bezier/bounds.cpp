#include "bezier/bezier.hpp"
#include "bezier/utils.hpp"

namespace stroke_outline::bezier
{
	AABB compute_quadratic_bounds(Vec2 p0, Vec2 p1, Vec2 p2)
	{
		AABB bounds { ::stroke_outline::min(p0, p2), ::stroke_outline::max(p0, p2) };

		auto tx = find_derivative_extrema_for_quadratic(p0.x, p1.x, p2.x);
		if (tx)
		{
			const double t = *tx;
			const Vec2 p = quadratic_bezier(p0, p1, p2, t);
			bounds.min = ::stroke_outline::min(bounds.min, p);
			bounds.max = ::stroke_outline::max(bounds.max, p);
		}

		auto ty = find_derivative_extrema_for_quadratic(p0.y, p1.y, p2.y);
		if (ty)
		{
			const double t = *ty;
			const Vec2 p = quadratic_bezier(p0, p1, p2, t);
			bounds.min = ::stroke_outline::min(bounds.min, p);
			bounds.max = ::stroke_outline::max(bounds.max, p);
		}

		return bounds;
	}

	AABB compute_cubic_bounds(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
	{
		AABB bounds(::stroke_outline::min(p0, p3), ::stroke_outline::max(p0, p3));

		auto extrema_x = find_derivative_extrema_for_cubic(p0.x, p1.x, p2.x, p3.x);
		for (int i = 0; i < extrema_x.count; ++i)
		{
			const double t = extrema_x.t[i];
			const Vec2 p = cubic_bezier(p0, p1, p2, p3, t);
			bounds.min = ::stroke_outline::min(bounds.min, p);
			bounds.max = ::stroke_outline::max(bounds.max, p);
		}

		auto extrema_y = find_derivative_extrema_for_cubic(p0.y, p1.y, p2.y, p3.y);
		for (int i = 0; i < extrema_y.count; ++i)
		{
			const double t = extrema_y.t[i];
			const Vec2 p = cubic_bezier(p0, p1, p2, p3, t);
			bounds.min = ::stroke_outline::min(bounds.min, p);
			bounds.max = ::stroke_outline::max(bounds.max, p);
		}

		return bounds;
	}
}
