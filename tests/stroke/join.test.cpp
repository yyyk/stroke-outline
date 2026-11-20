#include <doctest/doctest.h>

#include <algorithm>

// Line joins (src/stroke/join.cpp), observed through the public API.
#include "support/coverage.hpp"
#include "support/outline.hpp"

using namespace stroke_outline;
using namespace test_support;

namespace
{
	// An open right-angle polyline whose outer corner is at (100, 0). With a
	// 20-wide stroke the miter tip lands exactly on (110, -10).
	Path right_angle()
	{
		return Path {}.move_to(0, 0).line_to(100, 0).line_to(100, 100);
	}

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

namespace
{
	// How many grid samples near `centre`'s corners an open polyline's outline
	// (butt caps, round joins) gets wrong, against the exact stroke region: in
	// a segment's butt rectangle, or in a join's disc sector on the outer side
	// of its corner (a round join adds nothing on the inner side, as in canvas).
	int open_stroke_mismatches(const std::vector<Pt>& centre, double w)
	{
		Path path;
		path.move_to(centre[0].x, centre[0].y);
		for (std::size_t i = 1; i < centre.size(); ++i)
			path.line_to(centre[i].x, centre[i].y);
		auto settings = width(w, LineJoin::Round);
		settings.line_cap = LineCap::Butt;
		const auto outline = flatten(build_outline(path, settings));

		const double h = w / 2;
		const auto in_stroke = [&](Pt q) {
			for (std::size_t i = 0; i + 1 < centre.size(); ++i)
			{
				const Pt a = centre[i], b = centre[i + 1];
				const double len = std::hypot(b.x - a.x, b.y - a.y);
				const double ux = (b.x - a.x) / len, uy = (b.y - a.y) / len;
				const double along = (q.x - a.x) * ux + (q.y - a.y) * uy;
				const double across = -(q.x - a.x) * uy + (q.y - a.y) * ux;
				if (along >= 0 && along <= len && std::abs(across) <= h)
					return true;
			}
			for (std::size_t i = 1; i + 1 < centre.size(); ++i)
			{
				const Pt v = centre[i];
				const double dx = q.x - v.x, dy = q.y - v.y;
				if (std::hypot(dx, dy) > h)
					continue;
				// The outer normals at the end of the segment before and the start
				// of the one after: the join fills the sector between them.
				const Pt a = centre[i - 1], b = centre[i + 1];
				const double u1x = v.x - a.x, u1y = v.y - a.y;
				const double u2x = b.x - v.x, u2y = b.y - v.y;
				const double turn = u1x * u2y - u1y * u2x;
				const double s = turn > 0 ? 1 : -1;  // the outer side is away from the turn
				const double n1x = s * u1y, n1y = -s * u1x;
				const double n2x = s * u2y, n2y = -s * u2x;
				const double c12 = n1x * n2y - n1y * n2x;
				const double c1q = n1x * dy - n1y * dx;
				const double cq2 = dx * n2y - dy * n2x;
				if ((c12 >= 0 && c1q >= 0 && cq2 >= 0) || (c12 < 0 && c1q <= 0 && cq2 <= 0))
					return true;
			}
			return false;
		};

		// A fine grid (0.05) around every inner vertex, offset so that no
		// sample sits exactly on an edge.
		int bad = 0;
		for (std::size_t i = 1; i + 1 < centre.size(); ++i)
			for (double y = centre[i].y - 2 * w + 0.0123; y < centre[i].y + 2 * w; y += 0.05)
				for (double x = centre[i].x - 2 * w + 0.0123; x < centre[i].x + 2 * w;
					 x += 0.05)
				{
					const Pt q { x, y };
					bad += (winding(outline, q) != 0) != in_stroke(q);
				}
		return bad;
	}
}

TEST_CASE("a segment shorter than half the width at a corner adds nothing outside the stroke")
{
	// The inner side of such a corner can't be trimmed (the offset edges don't
	// cross within the segments), so it has to pass through the corner: a
	// straight bevel across it would fill a wedge outside the stroke. Shown at
	// the open end, where nothing else covers that wedge.
	const double w = 4.0;
	const std::vector<std::vector<Pt>> cases {
		{ { 0, 0 }, { 40, 0 }, { 40, -0.1 } },  // a short last segment, turning left
		{ { 0, 0 }, { 40, 0 }, { 40, 0.1 } },   // turning right
		{ { 40, -0.1 }, { 40, 0 }, { 0, 0 } },  // a short first segment
		{ { 0, 0 }, { 40, 0 }, { 40, -1.5 } },  // shorter than half the width, but not tiny
		{ { 0, 0 }, { 40, 0 }, { 40, -0.5 }, { 80, -0.5 } },  // a short step in the middle
	};
	for (const auto& centre: cases)
	{
		CAPTURE(centre.back().x);
		CAPTURE(centre.back().y);
		// A few samples right on a round join's arc could disagree, since the
		// test helper flattens it; none do now, against ~700 for the old bevel.
		CHECK(open_stroke_mismatches(centre, w) <= 5);
	}
}
