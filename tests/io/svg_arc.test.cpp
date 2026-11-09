#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "io/svg_arc.hpp"

using namespace stroke_outline;

namespace
{
	// Largest |y| along the arc, and the y of its most extreme point.
	double extreme_y(const std::vector<CubicTo>& arc, Vec2 from)
	{
		double best = 0.0;
		Vec2 pen = from;
		for (const auto& c: arc)
		{
			for (int i = 0; i <= 32; ++i)
			{
				const double t = i / 32.0, u = 1 - t;
				const double y = u * u * u * pen.y + 3 * u * u * t * c.control1.y
								 + 3 * u * t * t * c.control2.y + t * t * t * c.end.y;
				if (std::abs(y) > std::abs(best))
					best = y;
			}
			pen = c.end;
		}
		return best;
	}
}

TEST_CASE("the four flag combinations pick the four candidate arcs")
{
	// From (0,0) to (100,0) on a radius-100 circle: the small arcs bulge
	// 100 - √(100² - 50²) ≈ 13.4, the large ones 100 + 86.6 ≈ 186.6, and the
	// sweep flag picks the side.
	const Vec2 from { 0, 0 }, to { 100, 0 };
	double y[2][2];
	for (const bool large: { false, true })
		for (const bool sweep: { false, true })
		{
			CAPTURE(large);
			CAPTURE(sweep);
			const auto arc = svg::arc_to_cubics(from, 100, 100, 0, large, sweep, to);
			REQUIRE(!arc.empty());
			CHECK((arc.back().end - to).length() < 1e-9);
			y[large][sweep] = extreme_y(arc, from);
			CHECK(std::abs(y[large][sweep]) == doctest::Approx(large ? 186.6025 : 13.3975).epsilon(1e-3));
		}
	// The sweep flag (direction of travel) decides the side: both arcs with
	// the same sweep bulge the same way, and flipping it flips the side.
	CHECK(y[0][0] * y[0][1] < 0);
	CHECK(y[1][0] * y[1][1] < 0);
	CHECK(y[0][0] * y[1][0] > 0);
	CHECK(y[0][1] * y[1][1] > 0);
}

TEST_CASE("radii too small to reach the endpoint are scaled up (SVG spec)")
{
	// Radius 10 can't span a 100-long chord: scaled to 50, a semicircle.
	const auto arc = svg::arc_to_cubics({ 0, 0 }, 10, 10, 0, false, true, { 100, 0 });
	REQUIRE(!arc.empty());
	CHECK((arc.back().end - Vec2 { 100, 0 }).length() < 1e-9);
	CHECK(std::abs(extreme_y(arc, { 0, 0 })) == doctest::Approx(50).epsilon(1e-3));
}

TEST_CASE("x-axis rotation turns the ellipse")
{
	// An ellipse 100 wide, 50 tall, rotated 90°, is 50 wide and 100 tall: the
	// same bulge as the unrotated 50 x 100 one.
	const Vec2 from { 0, 0 }, to { 60, 0 };
	const auto rotated = svg::arc_to_cubics(from, 100, 50, 90, false, true, to);
	const auto upright = svg::arc_to_cubics(from, 50, 100, 0, false, true, to);
	REQUIRE(!rotated.empty());
	CHECK(extreme_y(rotated, from) == doctest::Approx(extreme_y(upright, from)).epsilon(1e-6));
}

TEST_CASE("degenerate arcs produce no curves")
{
	CHECK(svg::arc_to_cubics({ 0, 0 }, 0, 10, 0, false, true, { 10, 0 }).empty());   // rx = 0
	CHECK(svg::arc_to_cubics({ 0, 0 }, 10, 0, 0, false, true, { 10, 0 }).empty());   // ry = 0
	CHECK(svg::arc_to_cubics({ 5, 5 }, 10, 10, 0, false, true, { 5, 5 }).empty());   // same point
}
