#include <doctest/doctest.h>

#include <algorithm>

#include "bezier/bezier.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	template<class Eval>
	AABB sampled_bounds(Eval eval)
	{
		AABB b { eval(0.0), eval(0.0) };
		for (int i = 1; i <= 100000; ++i)
		{
			const Vec2 p = eval(i / 100000.0);
			b.min = { std::min(b.min.x, p.x), std::min(b.min.y, p.y) };
			b.max = { std::max(b.max.x, p.x), std::max(b.max.y, p.y) };
		}
		return b;
	}

	void check_same(const AABB& a, const AABB& b)
	{
		CHECK(a.min.x == doctest::Approx(b.min.x).epsilon(1e-6));
		CHECK(a.min.y == doctest::Approx(b.min.y).epsilon(1e-6));
		CHECK(a.max.x == doctest::Approx(b.max.x).epsilon(1e-6));
		CHECK(a.max.y == doctest::Approx(b.max.y).epsilon(1e-6));
	}
}

TEST_CASE("curve bounds are tight, not the control-point hull")
{
	// The arch's control point (50, 100) is outside the curve, which peaks at 50.
	const Vec2 q0 { 0, 0 }, q1 { 50, 100 }, q2 { 100, 0 };
	const AABB qb = compute_quadratic_bounds(q0, q1, q2);
	check_same(qb, sampled_bounds([&](double t) { return quadratic_bezier(q0, q1, q2, t); }));
	CHECK(qb.max.y == doctest::Approx(50));

	// S-curve: both x extrema are interior.
	const Vec2 c0 { 0, 0 }, c1 { 100, 0 }, c2 { -50, 100 }, c3 { 50, 100 };
	check_same(compute_cubic_bounds(c0, c1, c2, c3),
			   sampled_bounds([&](double t) { return cubic_bezier(c0, c1, c2, c3, t); }));
}
