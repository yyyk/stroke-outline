#include <doctest/doctest.h>

#include <cmath>

#include "bezier/bezier.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	template <class Eval>
	double polyline_length(Eval eval)
	{
		double total = 0;
		Vec2 prev = eval(0.0);
		for (int i = 1; i <= 200'000; ++i)
		{
			const Vec2 p = eval(i / 200000.0);
			total += (p - prev).length();
			prev = p;
		}
		return total;
	}
}

TEST_CASE("arc length of straight curves is the chord")
{
	CHECK(arc_length_quadratic({ 0, 0 }, { 5, 0 }, { 10, 0 }) == doctest::Approx(10));
	CHECK(
		arc_length_cubic({ 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 })
		== doctest::Approx(std::sqrt(18.0)));
}

TEST_CASE("arc length of curved curves matches a fine polyline")
{
	const Vec2 q0 { 0, 0 }, q1 { 50, 80 }, q2 { 100, 0 };
	CHECK(
		arc_length_quadratic(q0, q1, q2)
		== doctest::Approx(polyline_length([&](double t) {
			   return quadratic_bezier(q0, q1, q2, t);
		   })).epsilon(1e-6));

	const Vec2 c0 { 0, 0 }, c1 { 100, 0 }, c2 { 0, 100 }, c3 { 100, 100 };
	CHECK(
		arc_length_cubic(c0, c1, c2, c3)
		== doctest::Approx(polyline_length([&](double t) {
			   return cubic_bezier(c0, c1, c2, c3, t);
		   })).epsilon(1e-6));
}
