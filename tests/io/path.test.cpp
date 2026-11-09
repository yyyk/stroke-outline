#include <doctest/doctest.h>

#include <string>
#include <vector>

// The public Path type (src/io/path.cpp). Public API only — this file
// deliberately does NOT include any internal header (no PathCommand/Vec2),
// mirroring what an external consumer can reach.
#include "stroke_outline/stroke_outline.hpp"

using namespace stroke_outline;

TEST_CASE("Path builds fluently and renders to SVG")
{
	const std::string d =
		Path {}.move_to(0, 0).line_to(10, 0).quad_to(15, 5, 10, 10).close().to_svg();
	CHECK(d == "M 0 0 L 10 0 Q 15 5 10 10 Z");
}

TEST_CASE("Path round-trips through SVG")
{
	const std::string d = "M 0 0 C 0 -5 5 -5 10 0 Z";
	CHECK(Path::from_svg(d).to_svg() == d);
}

TEST_CASE("Path::clear empties the path and allows reuse")
{
	Path p;
	p.move_to(0, 0).line_to(10, 0).close();
	CHECK(!p.empty());

	p.clear();
	CHECK(p.empty());
	CHECK(p.to_svg() == "");

	// Reusable after clearing.
	p.move_to(5, 5).line_to(15, 5);
	CHECK(p.to_svg() == "M 5 5 L 15 5");
}

TEST_CASE("Path::arc_to expands an arc to cubics")
{
	// Quarter-ish arc: from (0,0) to (50,50); should emit at least one cubic and
	// end where asked.
	const Path p = Path {}.move_to(0, 0).arc_to(50, 50, 0, false, true, 50, 50);

	int cubics = 0;
	double last_x = 0, last_y = 0;
	p.for_each(
		{
			.cubic_to =
				[&](double, double, double, double, double x, double y) {
					++cubics;
					last_x = x;
					last_y = y;
				},
		});
	CHECK(cubics >= 1);
	CHECK(last_x == doctest::Approx(50.0).epsilon(0.01));
	CHECK(last_y == doctest::Approx(50.0).epsilon(0.01));
}

TEST_CASE("build_outline returns a closed outline")
{
	const Path outline = build_outline(
		Path {}.move_to(0, 0).line_to(100, 0).line_to(100, 100).line_to(0, 100).close(),
		{ .line_width = 20.0 });

	int moves = 0, closes = 0;
	outline.for_each(
		{
			.move_to = [&](double, double) { ++moves; },
			.close = [&] { ++closes; },
		});
	// Closed stroke = outer + inner contour.
	CHECK(moves == 2);
	CHECK(closes == 2);
}

TEST_CASE("for_each visits every command with the right arity")
{
	const Path p =
		Path {}
			.move_to(1, 2)
			.line_to(3, 4)
			.quad_to(5, 6, 7, 8)
			.cubic_to(9, 10, 11, 12, 13, 14)
			.close();

	std::string trace;
	p.for_each(
		{
			.move_to = [&](double, double) { trace += 'M'; },
			.line_to = [&](double, double) { trace += 'L'; },
			.quad_to = [&](double, double, double, double) { trace += 'Q'; },
			.cubic_to = [&](double, double, double, double, double, double) { trace += 'C'; },
			.close = [&] { trace += 'Z'; },
		});
	CHECK(trace == "MLQCZ");
}
