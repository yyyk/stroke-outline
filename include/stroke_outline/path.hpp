#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stroke_outline/settings.hpp"

namespace stroke_outline
{
	// Why an SVG path "d" string stopped parsing.
	struct SvgParseError
	{
		// What went wrong, and where (an index into the string):
		// "expected a number at position 11", or, when the data ends too early,
		// "expected a number at position 10 (end of data)".
		std::string message;
	};

	// A vector path built and read through primitives only. It never exposes the
	// library's internal command/point representation, so those stay free to
	// change without breaking callers.
	//
	//   Path outline = build_outline(
	//       Path{}.move_to(10, 10).line_to(90, 10).line_to(90, 90).close(),
	//       { .line_width = 8.0 });
	//   std::string d = outline.to_svg();
	class Path
	{
	public:
		Path() = default;

		// Fluent construction. Coordinates are absolute; every call chains.
		Path& move_to(double x, double y);
		Path& line_to(double x, double y);
		Path& quad_to(double cx, double cy, double x, double y);
		Path& cubic_to(double c1x, double c1y, double c2x, double c2y, double x, double y);
		// Elliptical arc (SVG "A" semantics), expanded to cubics internally.
		Path& arc_to(
			double rx,
			double ry,
			double x_axis_rotation_deg,
			bool large_arc,
			bool sweep,
			double x,
			double y);
		Path& close();

		// Drop all commands but keep the allocated capacity, so a single Path can
		// be reused to build many outlines without reallocating.
		Path& clear();

		bool empty() const { return verbs_.empty(); }

		// SVG interop: build from / render to an SVG path "d" string.
		//
		// Parsing follows SVG's error handling: it stops at the first error and
		// keeps everything before it (data that doesn't start with M/m yields an
		// empty path). Pass `error` to find out whether and where that happened;
		// it is left empty when the whole string parsed.
		//
		// `precision` is the minimum number of decimal
		// places. More are used when needed to keep rounding within ~1e-4 of the
		// path's size: with the default of 3, paths 10 units or larger are
		// unaffected, a path of size 1 gets 4 decimals and one of size 1e-4 gets 8.
		static Path from_svg(std::string_view d);
		static Path from_svg(std::string_view d, std::optional<SvgParseError>& error);
		std::string to_svg(int precision = 3) const;

		// Read the path back without any exposed geometry type — each callback
		// receives only the coordinates its command needs. Any unset callback is
		// skipped.
		struct Sink
		{
			std::function<void(double x, double y)> move_to;
			std::function<void(double x, double y)> line_to;
			std::function<void(double cx, double cy, double x, double y)> quad_to;
			std::function<
				void(double c1x, double c1y, double c2x, double c2y, double x, double y)>
				cubic_to;
			std::function<void()> close;
		};
		void for_each(const Sink& sink) const;

	private:
		// Flat, primitive-only storage: one verb byte per command plus an
		// x,y-interleaved coordinate array. This is deliberately not the internal
		// PathCommand/Vec2 representation.
		static constexpr std::uint8_t kMove = 0;
		static constexpr std::uint8_t kLine = 1;
		static constexpr std::uint8_t kQuad = 2;
		static constexpr std::uint8_t kCubic = 3;
		static constexpr std::uint8_t kClose = 4;

		std::vector<std::uint8_t> verbs_;
		std::vector<double> coords_;

		// Defined in src/io/path.cpp, where the internal types are visible.
		friend Path build_outline(const Path& path, const StrokeSettings& settings);
	};

	// Stroke `path` and return its outline as a new Path.
	//
	// Fill the outline with the NONZERO rule. Like Skia's and most stroke
	// outliners', it is not guaranteed to be a set of simple polygons: where the
	// stroke is wider than a concave notch or a tight curve, its boundary can
	// loop over itself, and overlapping pieces (e.g. per-subpath outlines or
	// dashes) are not merged. Nonzero renders and hit-tests all of these
	// correctly; even-odd would punch holes where pieces overlap, and consumers
	// that need simple polygons (some triangulators, boolean ops, area sums)
	// must resolve the overlaps themselves — or set StrokeSettings::clip_to_shape,
	// which resolves the outline of closed subpaths into simple contours (open
	// subpaths and dashes are still passed through as-is).
	Path build_outline(const Path& path, const StrokeSettings& settings);

	// Stroke SVG path data: the same as build_outline(Path::from_svg(d), ...).
	// Malformed data is stroked up to the first error, as SVG renders it; pass
	// `error` to find out whether that happened, and why and where.
	Path build_outline(std::string_view d, const StrokeSettings& settings);
	Path build_outline(
		std::string_view d,
		const StrokeSettings& settings,
		std::optional<SvgParseError>& error);
}
