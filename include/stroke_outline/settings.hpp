#pragma once

#include <optional>
#include <vector>

namespace stroke_outline
{
	enum class LineJoin { Miter, Bevel, Round };

	enum class LineCap { Butt, Square, Round };

	enum class StrokeAlign { Center, Inside, Outside };

	enum class FillRule { NonZero, EvenOdd };

	// Dash pattern (SVG semantics): odd-length patterns repeat, an all-zero or
	// invalid pattern strokes solid, and the pattern restarts at every subpath.
	// Zero-length dashes are drawn as dots by round/square caps (e.g. [0, gap]
	// with round caps). A pattern that would produce more than ~100,000 dashes
	// over the path strokes it solid instead.
	struct DashSettings
	{
		std::vector<double> dash_pattern;
		double dash_offset = 0.0;  // non-finite is treated as 0
	};

	struct StrokeSettings
	{
		// Must be a positive finite number; otherwise nothing is stroked (as is a
		// path with any non-finite coordinate).
		//
		// Results are the same at any size (tested from strokes ~1e-5 wide on
		// shapes ~1e-4 across up to 1e6 times larger), with one limit: a few
		// fixed thresholds remain in the curve code, so at the smallest of those
		// sizes curved outlines are off by a few percent of the stroke width.
		// If that matters, scale the path (and width) up before stroking and
		// the result back down.
		double line_width = 1.0;
		LineJoin line_join = LineJoin::Miter;
		LineCap line_cap = LineCap::Butt;
		// Ratio of miter length to line width beyond which a miter join is
		// beveled. Values below 1 (or NaN) are treated as 1: always bevel.
		double miter_limit = 4.0;
		// Inside/outside are relative to the fill of the whole path (under
		// `fill_rule`), so a hole is stroked into the material around it. An
		// inside stroke is the stroke at twice the width clipped to the fill; a
		// subpath with the fill on both sides (not a boundary) gets that full
		// double-width band, one with fill on neither side gets nothing (mirrored
		// for outside). Each subpath's side is decided at one point, so subpaths
		// that cross one another are only fully resolved with clip_to_shape.
		// Dashes of a closed subpath keep its alignment: butt-capped dashes are
		// offset to its stroked side; round/square-capped ones (and dots) are
		// always clipped to the fill, since their caps would otherwise cross the
		// shape's edge at corners. Open subpaths ignore alignment and stay
		// centered.
		StrokeAlign align = StrokeAlign::Center;
		// How the path's subpaths combine into a fill, for inside/outside
		// alignment and clip_to_shape. Use the rule the shape itself is filled
		// with (e.g. SVG fill-rule="evenodd" icons, whose holes may be wound the
		// same way as their outer edge).
		FillRule fill_rule = FillRule::NonZero;
		// When true, resolve the outline of closed subpaths into the region the
		// stroke should actually cover: parts of an over-wide stroke that turned
		// inside out, crossed themselves, overlapped another subpath's stroke, or
		// spilled past the shape (inside) / into it (outside) are removed. The
		// result is a set of simple contours that fills the same under nonzero
		// and even-odd. Dashes of inside/outside strokes are resolved the same
		// way; open subpaths and dashes of centered strokes are passed through
		// unchanged. Off by default: cost grows with the square of the total
		// segment count involved.
		bool clip_to_shape = false;
		std::optional<DashSettings> dash = std::nullopt;
	};
}
