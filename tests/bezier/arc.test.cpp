#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include "bezier/bezier.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	constexpr double kPi = std::numbers::pi;

	// Every sampled point of the arc lies on the circle (cubic arcs are within
	// ~3e-4 of the radius), and consecutive segments join.
	void check_on_circle(const std::vector<CubicBezier>& arc, Vec2 center, double r)
	{
		REQUIRE(!arc.empty());
		for (std::size_t k = 0; k < arc.size(); ++k)
		{
			if (k > 0)
				CHECK((arc[k - 1].p3 - arc[k].p0).length() < 1e-9);
			for (int i = 0; i <= 8; ++i)
			{
				const Vec2 p =
					cubic_bezier(arc[k].p0, arc[k].p1, arc[k].p2, arc[k].p3, i / 8.0);
				CHECK((p - center).length() == doctest::Approx(r).epsilon(3e-4));
			}
		}
	}

	Vec2 midpoint(const std::vector<CubicBezier>& arc)
	{
		const auto& mid = arc[arc.size() / 2];
		return arc.size() % 2 ? cubic_bezier(mid.p0, mid.p1, mid.p2, mid.p3, 0.5) : mid.p0;
	}
}

TEST_CASE("arc_to_cubic_beziers: a quarter turn is one segment, the long way three")
{
	const Vec2 c { 0, 0 };
	const auto short_way = arc_to_cubic_beziers(c, 10, 0, kPi / 2, false);
	CHECK(short_way.size() == 1);
	check_on_circle(short_way, c, 10);
	CHECK(short_way.front().p0.x == doctest::Approx(10));
	CHECK(short_way.back().p3.y == doctest::Approx(10));
	// It passes through angle π/4.
	CHECK(std::atan2(midpoint(short_way).y, midpoint(short_way).x) == doctest::Approx(kPi / 4));

	const auto long_way = arc_to_cubic_beziers(c, 10, 0, kPi / 2, true);
	CHECK(long_way.size() == 3);
	check_on_circle(long_way, c, 10);
	// It goes round the other side, through angle -3π/4.
	CHECK(
		std::atan2(midpoint(long_way).y, midpoint(long_way).x)
		== doctest::Approx(-3 * kPi / 4));
}

TEST_CASE("arc_to_cubic_beziers: equal start and end angles give a full circle")
{
	for (const bool ccw: { false, true })
	{
		CAPTURE(ccw);
		const auto full = arc_to_cubic_beziers({ 5, 5 }, 3, 1.0, 1.0, ccw);
		CHECK(full.size() == 4);
		check_on_circle(full, { 5, 5 }, 3);
		CHECK((full.front().p0 - full.back().p3).length() < 1e-9);
	}
}

TEST_CASE("arc_to_cubic_beziers: no radius, no arc")
{
	CHECK(arc_to_cubic_beziers({ 0, 0 }, 0, 0, 1, false).empty());
	CHECK(arc_to_cubic_beziers({ 0, 0 }, -1, 0, 1, false).empty());
}
