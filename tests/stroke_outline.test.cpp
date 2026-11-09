#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The stroking pipeline (src/stroke_outline.cpp): subpaths, outer/inner
// assembly, alignment and side selection, dashing per subpath, input
// validation, and scale invariance end to end.

#include "core.hpp"  // internal build_stroke_outline + PathCommand
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/stroke_outline.hpp"
#include "support/coverage.hpp"
#include "support/outline.hpp"

using namespace stroke_outline;
using namespace test_support;

namespace
{
	int count_of(const std::vector<PathCommand>& cmds, auto pred)
	{
		int n = 0;
		for (const auto& c: cmds)
			if (pred(c))
				++n;
		return n;
	}

	struct Bounds
	{
		double min_x, min_y, max_x, max_y;
	};

	void expand(Bounds& b, Vec2 p)
	{
		b.min_x = std::min(b.min_x, p.x);
		b.min_y = std::min(b.min_y, p.y);
		b.max_x = std::max(b.max_x, p.x);
		b.max_y = std::max(b.max_y, p.y);
	}

	Bounds bounds_of(const std::vector<PathCommand>& cmds)
	{
		Bounds b { 1e30, 1e30, -1e30, -1e30 };
		for (const auto& c: cmds)
			std::visit(
				[&](auto&& cmd) {
					using T = std::decay_t<decltype(cmd)>;
					if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
						expand(b, cmd.p);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
					{
						expand(b, cmd.control);
						expand(b, cmd.end);
					}
					else if constexpr (std::is_same_v<T, CubicTo>)
					{
						expand(b, cmd.control1);
						expand(b, cmd.control2);
						expand(b, cmd.end);
					}
				},
				c);
		return b;
	}
}

TEST_CASE("build_stroke_outline strokes a closed square into two contours")
{
	std::vector<PathCommand> path {
		MoveTo { { 0, 0 } },
		LineTo { { 100, 0 } },
		LineTo { { 100, 100 } },
		LineTo { { 0, 100 } },
		Close {}
	};
	StrokeSettings settings;
	settings.line_width = 20.0;

	const auto out = build_stroke_outline(path, settings);

	CHECK(!out.empty());
	// closed stroke = outer contour + inner contour, each ending in Close
	CHECK(
		count_of(out, [](const PathCommand& c) { return std::holds_alternative<Close>(c); })
		== 2);
	CHECK(
		count_of(out, [](const PathCommand& c) { return std::holds_alternative<MoveTo>(c); })
		== 2);
}

TEST_CASE("build_stroke_outline strokes an open line with butt caps")
{
	std::vector<PathCommand> path { MoveTo { { 0, 0 } }, LineTo { { 100, 0 } } };
	StrokeSettings settings;
	settings.line_width = 20.0;

	const auto out = build_stroke_outline(path, settings);

	// start-cap MoveTo + left LineTo + end-cap LineTo + reversed-right LineTo + Close
	CHECK(out.size() == 5);
}

TEST_CASE("build_stroke_outline on an empty path yields an empty stroke")
{
	CHECK(build_stroke_outline({}, StrokeSettings {}).empty());
}

TEST_CASE("stroke alignment shifts a closed square inside/outside its centerline")
{
	// CCW square enclosing [0,100]^2, so the interior is inside the square.
	const std::vector<PathCommand> square {
		MoveTo { { 0, 0 } },
		LineTo { { 100, 0 } },
		LineTo { { 100, 100 } },
		LineTo { { 0, 100 } },
		Close {}
	};
	StrokeSettings settings;
	settings.line_width = 20.0;

	SUBCASE("center straddles the centerline")
	{
		settings.align = StrokeAlign::Center;
		const auto b = bounds_of(build_stroke_outline(square, settings));
		CHECK(b.min_x == doctest::Approx(-10.0));
		CHECK(b.min_y == doctest::Approx(-10.0));
		CHECK(b.max_x == doctest::Approx(110.0));
		CHECK(b.max_y == doctest::Approx(110.0));
	}

	SUBCASE("outside grows the shape outward by the full width")
	{
		settings.align = StrokeAlign::Outside;
		const auto b = bounds_of(build_stroke_outline(square, settings));
		CHECK(b.min_x == doctest::Approx(-20.0));
		CHECK(b.min_y == doctest::Approx(-20.0));
		CHECK(b.max_x == doctest::Approx(120.0));
		CHECK(b.max_y == doctest::Approx(120.0));
	}

	SUBCASE("inside keeps the outer edge on the original path")
	{
		settings.align = StrokeAlign::Inside;
		const auto b = bounds_of(build_stroke_outline(square, settings));
		CHECK(b.min_x == doctest::Approx(0.0));
		CHECK(b.min_y == doctest::Approx(0.0));
		CHECK(b.max_x == doctest::Approx(100.0));
		CHECK(b.max_y == doctest::Approx(100.0));
	}
}

TEST_CASE("collapsed boundary reuses the original curve verbatim")
{
	// CCW shape with a cubic right edge bulging outward to x = 120. With Inside
	// alignment the outer boundary sits on the centerline (offset 0), so that
	// cubic must appear in the output exactly, not as an offset-by-zero refit.
	const CubicTo edge { { 120, 40 }, { 120, 60 }, { 100, 100 } };
	const std::vector<PathCommand> path {
		MoveTo { { 0, 0 } },
		LineTo { { 100, 0 } },
		edge,
		LineTo { { 0, 100 } },
		Close {}
	};
	StrokeSettings settings;
	settings.line_width = 20.0;
	settings.align = StrokeAlign::Inside;

	const auto out = build_stroke_outline(path, settings);

	const int exact_matches = count_of(out, [&](const PathCommand& c) {
		if (!std::holds_alternative<CubicTo>(c))
			return false;
		const auto& cubic = std::get<CubicTo>(c);
		return cubic.control1 == edge.control1 && cubic.control2 == edge.control2
			   && cubic.end == edge.end;
	});
	CHECK(exact_matches == 1);
}

TEST_CASE("inner corners are clipped to the offset intersection, not the centerline")
{
	// A sharp triangle stroked Inside: the inner boundary should miter to a
	// proper inner corner well inside the shape. The old code detoured the inner
	// join through the centerline vertex, leaving the apex near the tip (y ~ 12)
	// and filling flaps outside the edges. The fix miters to the true inner
	// corner (apex pulled deep inside).
	const std::vector<PathCommand>
		path { MoveTo { { 55, 12 } }, LineTo { { 98, 92 } }, LineTo { { 12, 92 } }, Close {} };
	StrokeSettings settings;
	settings.line_width = 14.0;
	settings.line_join = LineJoin::Miter;
	settings.align = StrokeAlign::Inside;

	const auto out = build_stroke_outline(path, settings);

	const int inner_apex = count_of(out, [](const PathCommand& c) {
		if (!std::holds_alternative<LineTo>(c))
			return false;
		const Vec2 p = std::get<LineTo>(c).p;
		return std::abs(p.x - 55.0) < 2.0 && p.y > 35.0 && p.y < 48.0;
	});
	CHECK(inner_apex >= 1);
}

TEST_CASE("stroke alignment is ignored for open subpaths (falls back to center)")
{
	const std::vector<PathCommand> line { MoveTo { { 0, 0 } }, LineTo { { 100, 0 } } };
	StrokeSettings settings;
	settings.line_width = 20.0;
	settings.align = StrokeAlign::Inside;

	const auto b = bounds_of(build_stroke_outline(line, settings));
	// Center behaviour: straddles y = 0 by half the width on each side.
	CHECK(b.min_y == doctest::Approx(-10.0));
	CHECK(b.max_y == doctest::Approx(10.0));
}

TEST_CASE("closed curve shapes assemble without a reversed-contour needle")
{
	// Half-disc: flat base + semicircle (two cubics). The reversed inner contour
	// used to start at the wrong endpoint, collapsing its first piece into an
	// A->B->A needle (and, under outside alignment, mangling the original-path
	// side). Guard against any immediate backtrack: no command endpoint should
	// equal the endpoint two commands earlier.
	const std::vector<PathCommand> path {
		MoveTo { { 10, 90 } },
		LineTo { { 90, 90 } },
		CubicTo { { 90, 67.91 }, { 72.09, 50 }, { 50, 50 } },
		CubicTo { { 27.91, 50 }, { 10, 67.91 }, { 10, 90 } },
		Close {}
	};

	const auto endpoint = [](const PathCommand& c) -> std::optional<Vec2> {
		return std::visit(
			[](auto&& x) -> std::optional<Vec2> {
				using T = std::decay_t<decltype(x)>;
				if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
					return x.p;
				else if constexpr (std::is_same_v<T, QuadraticTo> || std::is_same_v<T, CubicTo>)
					return x.end;
				else
					return std::nullopt;
			},
			c);
	};

	for (const auto align: { StrokeAlign::Center, StrokeAlign::Inside, StrokeAlign::Outside })
	{
		StrokeSettings settings;
		settings.line_width = 16.0;
		settings.align = align;

		const auto out = build_stroke_outline(path, settings);

		std::vector<Vec2> pts;
		for (const auto& c: out)
			if (const auto p = endpoint(c))
				pts.push_back(*p);

		int needles = 0;
		for (std::size_t i = 2; i < pts.size(); ++i)
			if (pts[i].distance_squared(pts[i - 2]) < 1e-6
				&& pts[i].distance_squared(pts[i - 1]) > 1e-6)
				++needles;
		CHECK(needles == 0);
	}
}

TEST_CASE("a zero-length subpath is a dot for round/square caps, nothing for butt")
{
	auto settings = width(10.0);
	for (const char* d: { "M 10 10 L 10 10", "M 10 10 Z" })
	{
		CAPTURE(d);
		SUBCASE("round: a circle of radius half_width")
		{
			settings.line_cap = LineCap::Round;
			const Subpath s = stroke_single(Path::from_svg(d), settings);
			CHECK(s.closed);
			CHECK(s.cubics == 4);
			CHECK(s.min_x() == doctest::Approx(5.0));
			CHECK(s.max_x() == doctest::Approx(15.0));
			CHECK(s.min_y() == doctest::Approx(5.0));
			CHECK(s.max_y() == doctest::Approx(15.0));
		}
		SUBCASE("square: an axis-aligned square")
		{
			settings.line_cap = LineCap::Square;
			const Subpath s = stroke_single(Path::from_svg(d), settings);
			CHECK(s.cubics == 0);
			CHECK(contains_point(s, { 5, 5 }));
			CHECK(contains_point(s, { 15, 15 }));
		}
		SUBCASE("butt: nothing")
		{
			settings.line_cap = LineCap::Butt;
			CHECK(build_outline(Path::from_svg(d), settings).empty());
		}
	}
	SUBCASE("a lone moveto is not a subpath")
	{
		settings.line_cap = LineCap::Round;
		CHECK(build_outline(Path::from_svg("M 10 10"), settings).empty());
	}
}

TEST_CASE("a curve that ends where it starts is stroked, not dropped")
{
	// A teardrop drawn as a single cubic: its endpoints coincide but its control
	// points don't. It used to vanish entirely.
	// The curve peaks at x = 75 (t = 1/2) and y = 400/9 (t = 2/3); the outline
	// sits half the width (5) beyond.
	const Subpath s =
		stroke_single(Path::from_svg("M 0 0 C 100 0 100 100 0 0"), width(10.0));
	CHECK(s.max_x() == doctest::Approx(80.0).epsilon(0.01));
	CHECK(s.max_y() == doctest::Approx(400.0 / 9.0 + 5.0).epsilon(0.01));
}

TEST_CASE("inside alignment follows curves, not just segment endpoints")
{
	// Regression: the subpath's direction used to come from its endpoint
	// polygon, which is degenerate here, so the stroke landed outside the shape
	// (~50% of samples wrong). The looser tolerance is for quadratic offsetting,
	// which is only accurate to ~7.5% of the offset distance — a separate
	// limitation from the one tested here.
	constexpr double kQuadOffsetTolerance = 0.01;
	SUBCASE("without clip_to_shape")
	{
		check_inside_stroke(kLens, width(8.0), false, kQuadOffsetTolerance);
	}
	SUBCASE("with clip_to_shape")
	{
		check_inside_stroke(kLens, width(8.0), true, kQuadOffsetTolerance);
	}
	SUBCASE("collapsed, with clip_to_shape") { check_inside_stroke(kLens, width(60.0)); }
}

TEST_CASE("inside/outside alignment is relative to the whole shape, holes included")
{
	// Regression: each subpath used to pick its side from its own winding, so a
	// hole was stroked into the hole (inside) or into the material (outside).
	// These strokes are narrower than every wall, so no clipping is needed.
	for (const bool clip: { false, true })
	{
		CAPTURE(clip);
		SUBCASE("frame, inside") { check_stroke(kFrame, StrokeAlign::Inside, 12.0, clip); }
		SUBCASE("frame, outside") { check_stroke(kFrame, StrokeAlign::Outside, 12.0, clip); }
		SUBCASE("ring, inside") { check_stroke(kRing, StrokeAlign::Inside, 12.0, clip); }
		SUBCASE("ring, outside") { check_stroke(kRing, StrokeAlign::Outside, 12.0, clip); }
		SUBCASE("same-direction hole under even-odd, inside")
		{
			check_stroke(kSameDirFrame, StrokeAlign::Inside, 12.0, clip, FillRule::EvenOdd);
		}
		SUBCASE("same-direction hole under even-odd, outside")
		{
			check_stroke(kSameDirFrame, StrokeAlign::Outside, 12.0, clip, FillRule::EvenOdd);
		}
		SUBCASE("same-direction inner square under nonzero is not a boundary")
		{
			// Fill on both sides: inside gets the full double-width band around
			// it, outside gets nothing there.
			check_stroke(kSameDirFrame, StrokeAlign::Inside, 12.0, clip);
			check_stroke(kSameDirFrame, StrokeAlign::Outside, 12.0, clip);
		}
	}
}

TEST_CASE("dashed inside/outside strokes stay on their side of the shape")
{
	// Regression: dashes are open pieces, and alignment used to be ignored for
	// them, so a dashed inside stroke straddled the edge.
	for (const LineCap cap: { LineCap::Butt, LineCap::Square, LineCap::Round })
	{
		CAPTURE(static_cast<int>(cap));
		for (const char* d: { kSquare, kFrame, kRing })
		{
			const std::string shape_d = d;
			CAPTURE(shape_d);
			const Path shape = Path::from_svg(d);
			const auto inside = build_outline(shape, dashed(StrokeAlign::Inside, cap, { 20, 10 }));
			const auto outside =
				build_outline(shape, dashed(StrokeAlign::Outside, cap, { 20, 10 }));
			CHECK(!inside.empty());
			CHECK(!outside.empty());
			CHECK(outline_stays(inside, d, true));
			CHECK(outline_stays(outside, d, false));
		}
	}
}

TEST_CASE("one-sided dashes match the clipped double-width dashes on straight edges")
{
	// Without clip_to_shape each dash is offset to one side with half caps; with
	// it, the dashes are double-width strokes clipped to the fill. On straight
	// edges these are the same region. (Square: the frame's four sides; dashes
	// that wrap a corner meet at a round join either way.)
	for (const LineCap cap: { LineCap::Butt, LineCap::Square, LineCap::Round })
		for (const StrokeAlign align: { StrokeAlign::Inside, StrokeAlign::Outside })
		{
			CAPTURE(static_cast<int>(cap));
			CAPTURE(static_cast<int>(align));
			auto settings = dashed(align, cap, { 20, 10 });
			const Path shape = Path::from_svg(kFrame);
			const Path one_sided = build_outline(shape, settings);
			settings.clip_to_shape = true;
			const Path clipped = build_outline(shape, settings);
			CHECK(fill_difference(one_sided, clipped, kFrame, 15.0) < 0.002);
		}
}

TEST_CASE("inside dots are half discs within the shape")
{
	// [0, 25] with round caps on a 100x100 square: a dot every 25 along the
	// 400-long perimeter. Inside, each is the stroked half of a disc of radius
	// line_width (the double-width dot, clipped).
	const Path shape = Path::from_svg(kSquare);
	auto settings = dashed(StrokeAlign::Inside, LineCap::Round, { 0, 25 });
	for (const bool clip: { false, true })
	{
		CAPTURE(clip);
		settings.clip_to_shape = clip;
		const Path out = build_outline(shape, settings);
		CHECK(outline_stays(out, kSquare, true));
		// 16 dots; corner dots are quarter discs. Area check: 12 half discs of
		// radius 10 plus 4 quarter discs.
		const double expected_area = (12 * 0.5 + 4 * 0.25) * 3.14159265 * 100.0;
		const double covered = (1.0 - fill_difference(out, Path {}, kSquare, 0.0)) == 1.0 ?
								   0.0 :
								   fill_difference(out, Path {}, kSquare, 0.0) * 100.0 * 100.0;
		CHECK(covered == doctest::Approx(expected_area).epsilon(0.03));
	}
}

TEST_CASE("a width that isn't a positive finite number strokes nothing")
{
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	for (const double w:
		 { 0.0, -10.0, std::nan(""), std::numeric_limits<double>::infinity() })
	{
		CAPTURE(w);
		CHECK(build_outline(line, { .line_width = w }).empty());
	}
}

TEST_CASE("a path with a non-finite coordinate strokes nothing")
{
	const double nan = std::nan("");
	const double inf = std::numeric_limits<double>::infinity();
	CHECK(build_outline(Path {}.move_to(0, 0).line_to(nan, 0), { .line_width = 2.0 }).empty());
	CHECK(build_outline(Path {}.move_to(0, 0).quad_to(inf, 0, 10, 10), { .line_width = 2.0 })
			  .empty());
}

TEST_CASE("out-of-range settings with a sensible meaning are normalized")
{
	const Path corner = Path {}.move_to(0, 0).line_to(100, 0).line_to(100, 100);
	auto settings = StrokeSettings { .line_width = 10.0 };

	SUBCASE("a miter limit below 1 (or NaN) behaves as 1: always bevel")
	{
		settings.miter_limit = 1.0;
		const std::string expected = build_outline(corner, settings).to_svg();
		for (const double limit: { 0.5, 0.0, -3.0, std::nan("") })
		{
			CAPTURE(limit);
			settings.miter_limit = limit;
			CHECK(build_outline(corner, settings).to_svg() == expected);
		}
	}
	SUBCASE("a non-finite dash offset behaves as 0")
	{
		settings.dash = DashSettings { { 20.0, 10.0 }, 0.0 };
		const std::string expected = build_outline(corner, settings).to_svg();
		settings.dash->dash_offset = std::nan("");
		CHECK(build_outline(corner, settings).to_svg() == expected);
	}
}

// ---------------------------------------------------------------------------
// Scale invariance (end to end)
// ---------------------------------------------------------------------------

namespace
{
	using Points = std::vector<std::pair<double, double>>;

	Path scaled(const Path& p, double s)
	{
		Path out;
		p.for_each({
			.move_to = [&](double x, double y) { out.move_to(x * s, y * s); },
			.line_to = [&](double x, double y) { out.line_to(x * s, y * s); },
			.quad_to = [&](double cx, double cy, double x, double y) {
				out.quad_to(cx * s, cy * s, x * s, y * s);
			},
			.cubic_to =
				[&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
					out.cubic_to(c1x * s, c1y * s, c2x * s, c2y * s, x * s, y * s);
				},
			.close = [&] { out.close(); },
		});
		return out;
	}

	// Densely sampled points along an outline.
	Points samples(const Path& p)
	{
		constexpr int kSteps = 16;
		Points v;
		double px = 0, py = 0;
		p.for_each({
			.move_to =
				[&](double x, double y) {
					v.push_back({ x, y });
					px = x, py = y;
				},
			.line_to =
				[&](double x, double y) {
					for (int i = 1; i <= kSteps; ++i)
					{
						const double t = double(i) / kSteps;
						v.push_back({ px + (x - px) * t, py + (y - py) * t });
					}
					px = x, py = y;
				},
			.quad_to =
				[&](double cx, double cy, double x, double y) {
					for (int i = 1; i <= kSteps; ++i)
					{
						const double t = double(i) / kSteps, u = 1 - t;
						v.push_back({ u * u * px + 2 * u * t * cx + t * t * x,
									  u * u * py + 2 * u * t * cy + t * t * y });
					}
					px = x, py = y;
				},
			.cubic_to =
				[&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
					for (int i = 1; i <= kSteps; ++i)
					{
						const double t = double(i) / kSteps, u = 1 - t;
						const double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t,
									 b3 = t * t * t;
						v.push_back({ b0 * px + b1 * c1x + b2 * c2x + b3 * x,
									  b0 * py + b1 * c1y + b2 * c2y + b3 * y });
					}
					px = x, py = y;
				},
		});
		return v;
	}

	// Largest distance from a point of one set to the nearest point of the other.
	double distance(const Points& a, const Points& b)
	{
		const auto one_way = [](const Points& from, const Points& to) {
			double worst = 0;
			for (const auto& p: from)
			{
				double nearest = 1e300;
				for (const auto& q: to)
					nearest =
						std::min(nearest, std::hypot(p.first - q.first, p.second - q.second));
				worst = std::max(worst, nearest);
			}
			return worst;
		};
		return std::max(one_way(a, b), one_way(b, a));
	}

	// `tiny_tolerance` applies at 1e-6, where curve code still has some fixed
	// thresholds (see StrokeSettings::line_width).
	void check_scale_invariant(
		const char* d,
		StrokeSettings settings,
		std::initializer_list<double> scales = { 1e-6, 1e-4, 1e-2, 1e2, 1e4, 1e6 },
		double tiny_tolerance = 1e-2)
	{
		const Path shape = Path::from_svg(d);
		const Points reference = samples(build_outline(shape, settings));
		REQUIRE(!reference.empty());
		for (const double s: scales)
		{
			CAPTURE(s);
			CAPTURE(d);
			StrokeSettings at_scale = settings;
			at_scale.line_width *= s;
			const Points got = samples(scaled(build_outline(scaled(shape, s), at_scale), 1 / s));
			REQUIRE(!got.empty());
			// In line widths. Sampling alone leaves ~1e-3 of slack where the two
			// outlines split curves at slightly different points.
			CHECK(distance(got, reference) / settings.line_width < (s <= 1e-6 ? tiny_tolerance : 1e-2));
		}
	}
}

// 1e-6 means shapes ~1e-4 across with strokes ~1e-5. Polylines and clipping are
// exact there; curves are within a few percent of the stroke width (some curve
// thresholds are still fixed — a documented limit, see
// StrokeSettings::line_width).
TEST_CASE("stroking is scale-invariant from 1e-6 to 1e6")
{
	SUBCASE("polyline, miter joins")
	{
		check_scale_invariant("M 0 0 L 100 0 L 100 100 L 30 60", { .line_width = 10.0 });
	}
	SUBCASE("curves, round joins and caps")
	{
		// Used to collapse to a flat line at 1e-4: curve offsetting's "parallel"
		// test compared a cross product (a squared length) to a fixed 1e-4.
		check_scale_invariant(
			"M 0 50 C 0 0 100 0 100 50 S 50 100 0 50 Z",
			{ .line_width = 8.0, .line_join = LineJoin::Round, .line_cap = LineCap::Round },
			{ 1e-6, 1e-4, 1e-2, 1e2, 1e4, 1e6 },
			0.05);
	}
	SUBCASE("quadratic lens, inside")
	{
		check_scale_invariant(
			"M 0 50 Q 50 -30 100 50 Q 50 130 0 50 Z",
			{ .line_width = 10.0, .align = StrokeAlign::Inside },
			{ 1e-6, 1e-4, 1e-2, 1e2, 1e4, 1e6 },
			0.05);
	}
	SUBCASE("frame, inside with clip_to_shape")
	{
		check_scale_invariant(
			"M 0 0 H 100 V 100 H 0 Z M 15 15 V 85 H 85 V 15 Z",
			{ .line_width = 25.0,
			  .line_join = LineJoin::Round,
			  .align = StrokeAlign::Inside,
			  .clip_to_shape = true },
			// The clipper's tolerance is fully size-relative (it used to have a
			// fixed 1e-4 floor that swamped this shape at 1e-6).
			{ 1e-6, 1e-4, 1e-2, 1e2, 1e4, 1e6 });
	}
}

TEST_CASE("curves with a zero end tangent stroke like their visible direction")
{
	// A control point sitting on an endpoint (common in SVG) makes the
	// derivative vanish there; the tangent falls back to the next control
	// point. All of these are straight along x, so the outline is the band.
	for (const char* d: { "M 0 0 C 0 0 50 0 100 0", "M 0 0 C 50 0 100 0 100 0",
						  "M 0 0 C 0 0 100 0 100 0", "M 0 0 Q 0 0 100 0", "M 0 0 Q 100 0 100 0" })
	{
		CAPTURE(std::string(d));
		auto settings = width(10.0);
		settings.line_cap = LineCap::Square;
		const Subpath s = stroke_single(Path::from_svg(d), settings);
		CHECK(s.min_x() == doctest::Approx(-5.0));
		CHECK(s.max_x() == doctest::Approx(105.0));
		CHECK(s.min_y() == doctest::Approx(-5.0));
		CHECK(s.max_y() == doctest::Approx(5.0));
	}
}

TEST_CASE("straight curves stroke as a band, not a zero-width line")
{
	// Regression: a quadratic or cubic whose controls lie on the line between
	// its ends (common in SVG) produced a zero-width outline.
	for (const char* d: { "M 0 0 Q 50 0 100 0", "M 0 0 C 30 0 70 0 100 0", "M 0 0 C 0 0 50 0 100 0" })
	{
		CAPTURE(std::string(d));
		const Subpath s = stroke_single(Path::from_svg(d), width(10.0));
		CHECK(s.min_y() == doctest::Approx(-5.0));
		CHECK(s.max_y() == doctest::Approx(5.0));
		CHECK(s.min_x() == doctest::Approx(0.0));
		CHECK(s.max_x() == doctest::Approx(100.0));
	}
}
