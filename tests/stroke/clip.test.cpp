#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

// clip_to_shape (src/stroke/clip.cpp). Most tests compare rasterized coverage
// with the exact stroke region (support/coverage.hpp); the last one inspects
// the internal command output directly.
#include "core.hpp"  // internal build_stroke_outline + PathCommand
#include "stroke_outline/PathCommand.hpp"
#include "support/coverage.hpp"

using namespace stroke_outline;
using namespace test_support;

TEST_CASE("clip_to_shape: a normal-width inside stroke is left alone")
{
	SUBCASE("square")
	{
		check_inside_stroke(kSquare, width(20.0));
	}
	SUBCASE("L-shape")
	{
		check_inside_stroke(kLShape, width(10.0, LineJoin::Round));
	}
	SUBCASE("circle")
	{
		check_inside_stroke(kCircle, width(30.0));
	}
	SUBCASE("rounded rect")
	{
		check_inside_stroke(kRoundedRect, width(30.0));
	}
}

TEST_CASE("clip_to_shape: a stroke just under the collapse width keeps a tiny hole")
{
	check_inside_stroke(kSquare, width(49.0));
}

TEST_CASE("clip_to_shape: a stroke wider than the shape fills it completely")
{
	SUBCASE("square, inner edge inverts")
	{
		check_inside_stroke(kSquare, width(60.0));
	}
	SUBCASE("square, inner edge inverts further")
	{
		check_inside_stroke(kSquare, width(80.0));
	}
	SUBCASE("square, inner edge self-intersects and escapes")
	{
		check_inside_stroke(kSquare, width(150.0));
	}
	SUBCASE("L-shape")
	{
		check_inside_stroke(kLShape, width(20.0, LineJoin::Round));
	}
	SUBCASE("L-shape, wider")
	{
		check_inside_stroke(kLShape, width(40.0, LineJoin::Round));
	}
	SUBCASE("triangle")
	{
		check_inside_stroke(kTriangle, width(50.0));
	}
	SUBCASE("circle, inner edge turns inside out")
	{
		check_inside_stroke(kCircle, width(70.0));
	}
	SUBCASE("half-disc")
	{
		check_inside_stroke(kHalfDisc, width(30.0));
	}
	SUBCASE("rounded rect")
	{
		check_inside_stroke(kRoundedRect, width(60.0));
	}
}

TEST_CASE("clip_to_shape: a narrow part collapses while wide parts keep their holes")
{
	// The corridor is only 20 high, so a 30-wide inside stroke fills it, while
	// each room keeps a 40x40 hole.
	check_inside_stroke(kDumbbell, width(30.0));
}

TEST_CASE("clip_to_shape: a fully collapsed stroke fills the shape for every join")
{
	for (const LineJoin join: { LineJoin::Miter, LineJoin::Bevel, LineJoin::Round })
	{
		CAPTURE(static_cast<int>(join));
		// The join style changes the raw inner edge, but none of it may survive.
		const Coverage c = inside_stroke_coverage(kSquare, width(150.0, join));
		CHECK(c.nonzero_mismatch < kTolerance);
		CHECK(c.evenodd_mismatch < kTolerance);
	}
}

TEST_CASE("a wide center stroke on a concave shape is correct under nonzero fill")
{
	// The documented contract (see build_outline in path.hpp): outlines may loop
	// over themselves — here the outer boundary does, where the stroke is wider
	// than the notch — and must be filled with nonzero. Only nonzero is checked;
	// even-odd is deliberately not promised.
	struct Case
	{
		const char* name;
		const char* d;
		double line_width;
	};
	for (const Case& c:
		 { Case { "U-shape", kUShape, 70.0 },
		   Case { "dumbbell", kDumbbell, 90.0 },
		   Case { "star", kStar, 130.0 } })
		for (const bool clip: { false, true })
		{
			CAPTURE(c.name);
			CAPTURE(clip);
			auto settings = width(c.line_width, LineJoin::Round);
			settings.align = StrokeAlign::Center;
			settings.clip_to_shape = clip;
			CHECK(stroke_coverage(c.d, settings).nonzero_mismatch < kTolerance);
		}
}

TEST_CASE("clip_to_shape resolves strokes that overlap across subpaths")
{
	SUBCASE("inside stroke wider than a frame's wall stays in the material")
	{
		// Each band crosses the 15-wide wall: without clipping, the outer edge's
		// band spills into the hole and the hole's band spills outside.
		check_stroke(kThinFrame, StrokeAlign::Inside, 25.0, true);
	}
	SUBCASE("overlapping subpaths, inside")
	{
		check_stroke(kOverlapping, StrokeAlign::Inside, 10.0, true);
	}
	SUBCASE("overlapping subpaths, outside")
	{
		check_stroke(kOverlapping, StrokeAlign::Outside, 10.0, true);
	}
	SUBCASE("wide outside stroke on a concave shape stays out of the shape")
	{
		// The notch is 40 wide: a 60-wide outside stroke from one arm reaches
		// into the other arm.
		check_stroke(kUShape, StrokeAlign::Outside, 60.0, true);
	}
	SUBCASE("wide center stroke on a concave shape fills correctly under even-odd too")
	{
		check_stroke(kUShape, StrokeAlign::Center, 70.0, true);
		check_stroke(kDumbbell, StrokeAlign::Center, 90.0, true);
	}
}

TEST_CASE("clip_to_shape keeps an over-wide inner boundary inside the original region")
{
	// Half-disc (flat base + semicircular dome, radius 40). With inside
	// alignment and a width approaching/exceeding the radius, the raw inner
	// boundary overshoots and spills past the original shape. clip_to_shape
	// intersects the inner contour with the outer one so nothing escapes.
	const std::vector<PathCommand> path {
		MoveTo { { 10, 90 } },
		LineTo { { 90, 90 } },
		CubicTo { { 90, 67.91 }, { 72.09, 50 }, { 50, 50 } },
		CubicTo { { 27.91, 50 }, { 10, 67.91 }, { 10, 90 } },
		Close {}
	};

	// Flatten the original to a polygon for an even-odd containment test.
	std::vector<Vec2> region;
	Vec2 pen {};
	for (const auto& c: path)
		std::visit(
			[&](auto&& cmd) {
				using T = std::decay_t<decltype(cmd)>;
				if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
				{
					region.push_back(cmd.p);
					pen = cmd.p;
				}
				else if constexpr (std::is_same_v<T, CubicTo>)
				{
					for (int i = 1; i <= 32; ++i)
					{
						const double t = i / 32.0, u = 1.0 - t;
						const double
							b0 = u * u * u,
							b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
						region.push_back(
							{ b0 * pen.x + b1 * cmd.control1.x + b2 * cmd.control2.x
								  + b3 * cmd.end.x,
							  b0 * pen.y + b1 * cmd.control1.y + b2 * cmd.control2.y
								  + b3 * cmd.end.y });
					}
					pen = cmd.end;
				}
			},
			c);

	const auto inside = [&](Vec2 p) {
		bool in = false;
		for (std::size_t i = 0, j = region.size() - 1; i < region.size(); j = i++)
			if ((region[i].y > p.y) != (region[j].y > p.y)
				&& p.x < (region[j].x - region[i].x) * (p.y - region[i].y)
								 / (region[j].y - region[i].y)
							 + region[i].x)
				in = !in;
		return in;
	};

	// Distance from p to the region boundary — lets us ignore points that merely
	// sit on the boundary (where even-odd classification is ambiguous) and flag
	// only genuine overshoots.
	const auto dist_to_boundary = [&](Vec2 p) {
		double best = 1e30;
		for (std::size_t i = 0, j = region.size() - 1; i < region.size(); j = i++)
		{
			const Vec2 a = region[j], b = region[i];
			const double dx = b.x - a.x, dy = b.y - a.y;
			const double len2 = dx * dx + dy * dy;
			double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
			t = std::max(0.0, std::min(1.0, t));
			const double ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
			best = std::min(best, std::sqrt(ex * ex + ey * ey));
		}
		return best;
	};

	const auto endpoints = [](const std::vector<PathCommand>& cmds) {
		std::vector<Vec2> pts;
		for (const auto& c: cmds)
			std::visit(
				[&](auto&& x) {
					using T = std::decay_t<decltype(x)>;
					if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
						pts.push_back(x.p);
					else if constexpr (
						std::is_same_v<T, QuadraticTo> || std::is_same_v<T, CubicTo>)
						pts.push_back(x.end);
				},
				c);
		return pts;
	};

	for (const double width: { 20.0, 40.0 })
	{
		StrokeSettings settings;
		settings.line_width = width;
		settings.align = StrokeAlign::Inside;
		settings.clip_to_shape = true;

		int outside = 0;
		for (const auto p: endpoints(build_stroke_outline(path, settings)))
			if (!inside(p) && dist_to_boundary(p) > 1.0)
				++outside;
		// No clipped output vertex escapes the original region by any real
		// margin (boundary-landing points are absorbed by the 1px slack).
		CHECK(outside == 0);
	}
}
