#include <doctest/doctest.h>

#include <algorithm>

// Line joins (src/stroke/join.cpp), observed through the public API.
#include "support/outline.hpp"

using namespace stroke_outline;
using namespace test_support;

namespace
{
	// An open right-angle polyline whose outer corner is at (100, 0). With a
	// 20-wide stroke the miter tip lands exactly on (110, -10).
	Path right_angle() { return Path {}.move_to(0, 0).line_to(100, 0).line_to(100, 100); }

}

TEST_CASE("miter join extends the outer corner to the offset-edge intersection")
{
	auto settings = width(20.0);
	settings.line_join = LineJoin::Miter;

	const Subpath s = stroke_single(right_angle(), settings);

	CHECK(contains_point(s, { 110, -10 }));
	CHECK(s.cubics == 0);
}

TEST_CASE("bevel join cuts straight across the outer corner")
{
	auto settings = width(20.0);
	settings.line_join = LineJoin::Bevel;

	const Subpath s = stroke_single(right_angle(), settings);

	CHECK(!contains_point(s, { 110, -10 }));
	// Both offset-edge ends are still present; they are joined directly.
	CHECK(contains_point(s, { 100, -10 }));
	CHECK(contains_point(s, { 110, 0 }));
	CHECK(s.cubics == 0);
}

TEST_CASE("round join bridges the outer corner with an arc of radius half_width")
{
	auto settings = width(20.0);
	settings.line_join = LineJoin::Round;

	const Subpath s = stroke_single(right_angle(), settings);

	CHECK(!contains_point(s, { 110, -10 }));
	CHECK(s.cubics >= 1);
	// Arc control points stay inside the miter square around the corner.
	CHECK(s.max_x() <= doctest::Approx(110.0));
	CHECK(s.min_y() >= doctest::Approx(-10.0));
}

TEST_CASE("miter_limit falls back to a bevel on a sharp corner")
{
	// The corner at (100, 0) is ~5.7°, giving a miter length of ~200 for a
	// 20-wide stroke (ratio ~20).
	const Path sharp = Path {}.move_to(0, 0).line_to(100, 0).line_to(0, 10);
	auto settings = width(20.0);
	settings.line_join = LineJoin::Miter;

	SUBCASE("default limit of 4 bevels the corner")
	{
		const Subpath s = stroke_single(sharp, settings);
		CHECK(s.max_x() < 111.0);
	}

	SUBCASE("a generous limit keeps the long miter spike")
	{
		settings.miter_limit = 100.0;
		const Subpath s = stroke_single(sharp, settings);
		CHECK(s.max_x() > 250.0);
	}
}

TEST_CASE("a path that doubles back: a round join wraps the end, others end flat")
{
	// The path runs to x = 100 and straight back. The outer side of that 180°
	// turn is a semicircle for round joins and a flat end for miter/bevel (a
	// miter at 180° would be infinitely long).
	const Path u_turn = Path {}.move_to(0, 0).line_to(100, 0).line_to(0, 0);

	const auto round = subpaths_of(build_outline(u_turn, width(10.0, LineJoin::Round)));
	REQUIRE(!round.empty());
	double round_max_x = 0;
	int cubics = 0;
	for (const auto& s: round)
	{
		round_max_x = std::max(round_max_x, s.max_x());
		cubics += s.cubics;
	}
	CHECK(round_max_x == doctest::Approx(105.0).epsilon(1e-3));
	CHECK(cubics >= 1);

	for (const LineJoin join: { LineJoin::Miter, LineJoin::Bevel })
	{
		CAPTURE(static_cast<int>(join));
		for (const auto& s: subpaths_of(build_outline(u_turn, width(10.0, join))))
			CHECK(s.max_x() == doctest::Approx(100.0));
	}
}

TEST_CASE("a nearly straight corner adds no join geometry")
{
	// A 1e-5 rad bend: no spike or bulge beyond the half width on either side.
	for (const LineJoin join: { LineJoin::Miter, LineJoin::Bevel, LineJoin::Round })
	{
		CAPTURE(static_cast<int>(join));
		const Subpath s = stroke_single(
			Path {}.move_to(0, 0).line_to(100, 0).line_to(200, 0.001),
			width(10.0, join));
		CHECK(s.max_y() == doctest::Approx(5.0).epsilon(1e-3));
		CHECK(s.min_y() == doctest::Approx(-5.0).epsilon(1e-3));
	}
}
