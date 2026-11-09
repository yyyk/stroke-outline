#pragma once

// Test helpers: rasterize a stroke outline on a 0.5-unit grid and compare its
// coverage with the exact stroke region (or another outline), under both fill
// rules — a correct outline must not depend on which one the renderer uses.
// Plus the test shapes these comparisons use.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "stroke_outline/stroke_outline.hpp"
#include "support/outline.hpp"

namespace test_support
{
	using namespace stroke_outline;

	struct Pt
	{
		double x, y;
	};
	using Polyline = std::vector<Pt>;

	inline std::vector<Polyline> flatten(const Path& path)
	{
		constexpr int kSteps = 32;
		std::vector<Polyline> out;
		Pt pen {};
		path.for_each(
			{
				.move_to =
					[&](double x, double y) {
						out.push_back({ { x, y } });
						pen = { x, y };
					},
				.line_to =
					[&](double x, double y) {
						out.back().push_back({ x, y });
						pen = { x, y };
					},
				.quad_to =
					[&](double cx, double cy, double x, double y) {
						for (int i = 1; i <= kSteps; ++i)
						{
							const double t = double(i) / kSteps, u = 1.0 - t;
							out.back().push_back(
								{ u * u * pen.x + 2 * u * t * cx + t * t * x,
								  u * u * pen.y + 2 * u * t * cy + t * t * y });
						}
						pen = { x, y };
					},
				.cubic_to =
					[&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
						for (int i = 1; i <= kSteps; ++i)
						{
							const double t = double(i) / kSteps, u = 1.0 - t;
							const double
								b0 = u * u * u,
								b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
							out.back().push_back(
								{ b0 * pen.x + b1 * c1x + b2 * c2x + b3 * x,
								  b0 * pen.y + b1 * c1y + b2 * c2y + b3 * y });
						}
						pen = { x, y };
					},
			});
		return out;
	}

	inline int winding(const std::vector<Polyline>& polys, Pt q)
	{
		int w = 0;
		for (const auto& pl: polys)
			for (std::size_t i = 0; i < pl.size(); ++i)
			{
				const Pt a = pl[i], b = pl[(i + 1) % pl.size()];
				const double side = (b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y);
				if (a.y <= q.y && b.y > q.y && side > 0)
					++w;
				else if (a.y > q.y && b.y <= q.y && side < 0)
					--w;
			}
		return w;
	}

	inline double distance_to_boundary(const std::vector<Polyline>& polys, Pt q)
	{
		double best = 1e300;
		for (const auto& pl: polys)
			for (std::size_t i = 0; i < pl.size(); ++i)
			{
				const Pt a = pl[i], b = pl[(i + 1) % pl.size()];
				const double vx = b.x - a.x, vy = b.y - a.y;
				const double len_sq = vx * vx + vy * vy;
				double t = len_sq > 0 ? ((q.x - a.x) * vx + (q.y - a.y) * vy) / len_sq : 0;
				t = std::clamp(t, 0.0, 1.0);
				best = std::min(best, std::hypot(q.x - a.x - t * vx, q.y - a.y - t * vy));
			}
		return best;
	}

	struct Coverage
	{
		double nonzero_mismatch;  // fraction of samples that disagree with the truth
		double evenodd_mismatch;
	};

	// Sample a 0.5-unit grid over the shape's bounds (plus a margin, to catch
	// anything that spills outside) and compare the stroke's fill to the exact
	// stroke region — the standard definition, a stroke at twice the width
	// clipped to the shape's fill (or its complement):
	//   inside:  in the fill, within line_width of any subpath
	//   outside: outside the fill, within line_width of any subpath
	//   center:  within line_width / 2 of any subpath
	// The fill uses settings.fill_rule. Exact only for round joins (or shapes
	// without reflex corners).
	inline Coverage stroke_coverage(const char* d, const StrokeSettings& settings)
	{
		const StrokeAlign align = settings.align;
		const double reach =
			align == StrokeAlign::Center ? settings.line_width / 2 : settings.line_width;
		const Path shape = Path::from_svg(d);
		const auto truth_polys = flatten(shape);
		const auto out_polys = flatten(build_outline(shape, settings));
		const auto in_fill = [&](Pt q) {
			const int w = winding(truth_polys, q);
			return settings.fill_rule == FillRule::NonZero ? w != 0 : (w & 1) != 0;
		};

		double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
		for (const auto& pl: truth_polys)
			for (const Pt p: pl)
			{
				x0 = std::min(x0, p.x), y0 = std::min(y0, p.y);
				x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
			}
		const double margin = 20.0 + (align == StrokeAlign::Inside ? 0.0 : reach);

		int samples = 0, bad_nonzero = 0, bad_evenodd = 0;
		for (double y = y0 - margin + 0.25; y < y1 + margin; y += 0.5)
			for (double x = x0 - margin + 0.25; x < x1 + margin; x += 0.5)
			{
				const Pt q { x, y };
				const bool near_path = distance_to_boundary(truth_polys, q) <= reach;
				const bool truth =
					near_path
					&& (align == StrokeAlign::Center
						|| in_fill(q) == (align == StrokeAlign::Inside));
				const int w = winding(out_polys, q);
				++samples;
				bad_nonzero += (w != 0) != truth;
				bad_evenodd += ((w & 1) != 0) != truth;
			}
		return { double(bad_nonzero) / samples, double(bad_evenodd) / samples };
	}

	inline Coverage
		inside_stroke_coverage(const char* d, StrokeSettings settings, bool clip = true)
	{
		settings.align = StrokeAlign::Inside;
		settings.clip_to_shape = clip;
		return stroke_coverage(d, settings);
	}

	// Sampling and flattening put a few grid points right on edges; anything
	// beyond this is a real coverage error.
	inline constexpr double kTolerance = 0.003;

	inline void check_inside_stroke(
		const char* d,
		StrokeSettings settings,
		bool clip = true,
		double tolerance = kTolerance)
	{
		const Coverage c = inside_stroke_coverage(d, settings, clip);
		CHECK(c.nonzero_mismatch < tolerance);
		CHECK(c.evenodd_mismatch < tolerance);
	}

	inline void check_stroke(
		const char* d,
		StrokeAlign align,
		double line_width,
		bool clip,
		FillRule rule = FillRule::NonZero)
	{
		auto settings = width(line_width, LineJoin::Round);
		settings.align = align;
		settings.clip_to_shape = clip;
		settings.fill_rule = rule;
		const Coverage c = stroke_coverage(d, settings);
		CHECK(c.nonzero_mismatch < kTolerance);
		CHECK(c.evenodd_mismatch < kTolerance);
	}

	// Every sampled point of the outline lies inside (or, for `inside == false`,
	// outside) the shape's fill, allowing `slack` for points on its boundary.
	// The default covers the flattening error of the test's own polyline for a
	// curved shape (a point on a radius-50 arc sits ~0.015 outside its chord).
	inline bool
		outline_stays(const Path& outline, const char* d, bool inside, double slack = 0.05)
	{
		const auto shape = flatten(Path::from_svg(d));
		for (const auto& pl: flatten(outline))
			for (const Pt p: pl)
				if ((winding(shape, p) != 0) != inside
					&& distance_to_boundary(shape, p) > slack)
					return false;
		return true;
	}

	// Share of grid samples (over the shape's bounds plus `margin`) where two
	// outlines' nonzero fills disagree.
	inline double fill_difference(const Path& a, const Path& b, const char* d, double margin)
	{
		const auto shape = flatten(Path::from_svg(d));
		const auto pa = flatten(a);
		const auto pb = flatten(b);
		double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
		for (const auto& pl: shape)
			for (const Pt p: pl)
			{
				x0 = std::min(x0, p.x), y0 = std::min(y0, p.y);
				x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
			}
		int samples = 0, differ = 0;
		for (double y = y0 - margin + 0.25; y < y1 + margin; y += 0.5)
			for (double x = x0 - margin + 0.25; x < x1 + margin; x += 0.5)
			{
				++samples;
				differ += (winding(pa, { x, y }) != 0) != (winding(pb, { x, y }) != 0);
			}
		return double(differ) / samples;
	}

	inline StrokeSettings dashed(StrokeAlign align, LineCap cap, std::vector<double> pattern)
	{
		StrokeSettings s;
		s.line_width = 10.0;
		s.align = align;
		s.line_cap = cap;
		s.line_join = LineJoin::Round;
		s.dash = DashSettings { std::move(pattern), 0.0 };
		return s;
	}

	inline constexpr const char* kSquare = "M 0 0 H 100 V 100 H 0 Z";
	// The reflex corner at (30, 30) makes the true erosion rounded there, so
	// round joins are used to match the exact distance-based truth.
	inline constexpr const char* kLShape = "M 0 0 H 100 V 30 H 30 V 100 H 0 Z";
	inline constexpr const char* kTriangle = "M 0 0 L 100 0 L 50 80 Z";
	inline constexpr const char* kCircle = "M 10 50 A 40 40 0 0 1 90 50 A 40 40 0 0 1 10 50 Z";
	inline constexpr const char* kHalfDisc = "M 10 90 L 90 90 A 40 40 0 0 0 10 90 Z";
	// Two 100x100 rooms joined by a 20-high corridor.
	inline constexpr const char* kDumbbell =
		"M 0 0 H 100 V 40 H 150 V 0 H 250 V 100 H 150 V 60 H 100 V 100 H 0 Z";
	// Two quadratics: the polygon through the segment endpoints has zero area,
	// so the winding direction has to come from the curves themselves.
	inline constexpr const char* kLens = "M 0 50 Q 50 -30 100 50 Q 50 130 0 50 Z";
	inline constexpr const char* kUShape = "M 0 0 H 30 V 70 H 70 V 0 H 100 V 100 H 0 Z";
	// Compound shapes. kFrame/kThinFrame/kRing wind the hole opposite to the
	// outer edge (a hole under either fill rule); kSameDirFrame winds it the same
	// way (a hole only under even-odd, as in many SVG icons).
	inline constexpr const char* kFrame = "M 0 0 H 100 V 100 H 0 Z M 30 30 V 70 H 70 V 30 Z";
	inline constexpr const char* kThinFrame =
		"M 0 0 H 100 V 100 H 0 Z M 15 15 V 85 H 85 V 15 Z";
	inline constexpr const char* kSameDirFrame =
		"M 0 0 H 100 V 100 H 0 Z M 30 30 H 70 V 70 H 30 Z";
	inline constexpr const char* kRing =
		"M 0 50 A 50 50 0 1 1 100 50 A 50 50 0 1 1 0 50 Z "
		"M 25 50 A 25 25 0 1 0 75 50 A 25 25 0 1 0 25 50 Z";
	// Two squares overlapping in [40,60]²: parts of each one's edge lie inside
	// the other, so they are not boundaries of the combined fill.
	inline constexpr const char* kOverlapping =
		"M 0 0 H 60 V 60 H 0 Z M 40 40 H 100 V 100 H 40 Z";
	inline constexpr const char* kStar =
		"M 50 0 L 61 35 L 98 35 L 68 57 L 79 91 L 50 70 L 21 91 L 32 57 L 2 35 L 39 35 Z";
	inline constexpr const char* kRoundedRect =
		"M 40 0 H 160 A 40 40 0 0 1 200 40 V 60 A 40 40 0 0 1 160 100 H 40 "
		"A 40 40 0 0 1 0 60 V 40 A 40 40 0 0 1 40 0 Z";
}
