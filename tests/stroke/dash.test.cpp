#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

// Dashing (src/stroke/dash.cpp), observed through the public API.
#include "support/outline.hpp"

using namespace stroke_outline;
using namespace test_support;

TEST_CASE("dash pattern splits an open line into one outline per dash")
{
	// 100 long, [20 on, 10 off] -> dashes at [0,20] [30,50] [60,80] [90,100].
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(2.0);
	settings.dash = DashSettings { { 20.0, 10.0 }, 0.0 };

	const auto dashes = subpaths_of(build_outline(line, settings));

	REQUIRE(dashes.size() == 4);
	const double expected[4][2] = { { 0, 20 }, { 30, 50 }, { 60, 80 }, { 90, 100 } };
	for (std::size_t i = 0; i < 4; ++i)
	{
		CAPTURE(i);
		CHECK(dashes[i].closed);
		CHECK(dashes[i].min_x() == doctest::Approx(expected[i][0]));
		CHECK(dashes[i].max_x() == doctest::Approx(expected[i][1]));
	}
}

TEST_CASE("dash_offset shifts where the pattern starts")
{
	// Offset 25 into a 30-long [20, 10] pattern: start 5 into the gap, so the
	// first dash begins at x = 5.
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(2.0);
	settings.dash = DashSettings { { 20.0, 10.0 }, 25.0 };

	const auto dashes = subpaths_of(build_outline(line, settings));

	REQUIRE(dashes.size() == 4);
	CHECK(dashes.front().min_x() == doctest::Approx(5.0));
	CHECK(dashes.front().max_x() == doctest::Approx(25.0));
	CHECK(dashes.back().min_x() == doctest::Approx(95.0));
	CHECK(dashes.back().max_x() == doctest::Approx(100.0));
}

TEST_CASE("an odd-length dash pattern is repeated, as in SVG")
{
	// [10] behaves like [10, 10]: dashes at [0,10] [20,30] ... [80,90].
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(2.0);
	settings.dash = DashSettings { { 10.0 }, 0.0 };

	const auto dashes = subpaths_of(build_outline(line, settings));

	REQUIRE(dashes.size() == 5);
	CHECK(dashes[1].min_x() == doctest::Approx(20.0));
	CHECK(dashes[1].max_x() == doctest::Approx(30.0));
}

TEST_CASE("an all-zero dash pattern strokes the path solid")
{
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(2.0);
	settings.dash = DashSettings { { 0.0, 0.0 }, 0.0 };

	const Subpath s = stroke_single(line, settings);
	CHECK(s.min_x() == doctest::Approx(0.0));
	CHECK(s.max_x() == doctest::Approx(100.0));
}

TEST_CASE("dashes get caps applied to each end")
{
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(2.0);
	settings.line_cap = LineCap::Square;
	settings.dash = DashSettings { { 20.0, 10.0 }, 0.0 };

	const auto dashes = subpaths_of(build_outline(line, settings));

	REQUIRE(dashes.size() == 4);
	// Square caps push each dash out by half the width (1) on both ends.
	CHECK(dashes[1].min_x() == doctest::Approx(29.0));
	CHECK(dashes[1].max_x() == doctest::Approx(51.0));
}

TEST_CASE("dashing follows curves")
{
	// A quarter-ish curve is ~150 long; a [10, 10] pattern must yield several
	// dashes, each made of curve segments rather than collapsing to a line.
	const Path curve = Path {}.move_to(0, 0).cubic_to(50, 0, 100, 50, 100, 100);
	auto settings = width(2.0);
	settings.dash = DashSettings { { 10.0, 10.0 }, 0.0 };

	const auto dashes = subpaths_of(build_outline(curve, settings));

	CHECK(dashes.size() >= 6);
	for (const auto& d: dashes)
		CHECK(d.closed);
}

TEST_CASE("a [0, gap] dash pattern with round caps draws dots, including at the end")
{
	// 100 long, a dot every 20: at 0, 20, 40, 60, 80 and 100.
	auto settings = width(4.0);
	settings.line_cap = LineCap::Round;
	settings.dash = DashSettings { { 0.0, 20.0 }, 0.0 };

	const auto dots = subpaths_of(build_outline(Path {}.move_to(0, 0).line_to(100, 0), settings));

	REQUIRE(dots.size() == 6);
	for (std::size_t i = 0; i < dots.size(); ++i)
	{
		CAPTURE(i);
		CHECK(dots[i].cubics == 4);
		CHECK((dots[i].min_x() + dots[i].max_x()) / 2 == doctest::Approx(20.0 * i));
		CHECK(dots[i].max_x() - dots[i].min_x() == doctest::Approx(4.0));
	}
}

TEST_CASE("the dash pattern restarts at every subpath")
{
	// Two 25-long lines with [20, 10]: each gets a dash at [0, 20]. Without the
	// restart the second line would start 5 into the gap.
	auto settings = width(2.0);
	settings.dash = DashSettings { { 20.0, 10.0 }, 0.0 };

	const auto dashes = subpaths_of(
		build_outline(Path {}.move_to(0, 0).line_to(25, 0).move_to(0, 20).line_to(25, 20), settings));

	REQUIRE(dashes.size() == 2);
	for (const auto& dash: dashes)
	{
		CHECK(dash.min_x() == doctest::Approx(0.0));
		CHECK(dash.max_x() == doctest::Approx(20.0));
	}
}

TEST_CASE("a dash pattern that would produce too many dashes strokes the path solid")
{
	// 1e-9 dashes over 100 units is ~1e11 intervals; this used to hang.
	auto settings = width(2.0);
	settings.dash = DashSettings { { 1e-9, 1e-9 }, 0.0 };

	const Subpath s = stroke_single(Path {}.move_to(0, 0).line_to(100, 0), settings);
	CHECK(s.min_x() == doctest::Approx(0.0));
	CHECK(s.max_x() == doctest::Approx(100.0));
}

TEST_CASE("dashing a huge curve stays bounded")
{
	// The arc-length table used to allocate one sample per unit of length.
	auto settings = width(2.0);
	settings.dash = DashSettings { { 1e7, 1e7 }, 0.0 };

	const auto dashes =
		subpaths_of(build_outline(Path {}.move_to(0, 0).quad_to(5e8, 1e9, 1e9, 0), settings));
	CHECK(dashes.size() > 10);
}

TEST_CASE("a negative dash offset wraps around the pattern")
{
	// -5 into a 30-long pattern is the same as 25.
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto negative = width(2.0);
	negative.dash = DashSettings { { 20.0, 10.0 }, -5.0 };
	auto positive = width(2.0);
	positive.dash = DashSettings { { 20.0, 10.0 }, 25.0 };
	CHECK(build_outline(line, negative).to_svg() == build_outline(line, positive).to_svg());
}

TEST_CASE("on a closed path, a dash running through the start is one piece")
{
	// A 400-long square with [30, 20] offset by 10: the dash covering
	// [390, 420] wraps past the start, so its two halves ([390, 400] up the
	// left edge and [0, 20] along the top) are joined: 8 dashes, not 9.
	auto settings = width(2.0);
	settings.dash = DashSettings { { 30.0, 20.0 }, 10.0 };
	const auto dashes =
		subpaths_of(build_outline(Path::from_svg("M 0 0 H 100 V 100 H 0 Z"), settings));

	CHECK(dashes.size() == 8);
	const bool wraps_corner = std::ranges::any_of(dashes, [](const Subpath& s) {
		return s.max_x() == doctest::Approx(20.0) && s.max_y() == doctest::Approx(10.0);
	});
	CHECK(wraps_corner);
}
