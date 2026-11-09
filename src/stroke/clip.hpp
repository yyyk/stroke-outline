#pragma once

#include <vector>
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"
#include "stroke_outline/settings.hpp"

namespace stroke_outline
{
	// Is `p` inside the fill of `path` under `rule`? Every subpath counts as
	// closed (SVG fill semantics). Curve-exact; nothing is flattened.
	bool path_fill_contains(Vec2 p, const std::vector<PathCommand>& path, FillRule rule);

	// Resolve the raw outline of a set of closed strokes into the boundary of
	// the region they should actually fill, for clip_to_shape.
	//
	//   outline   the raw outlines of every closed stroked subpath (MoveTo +
	//             Line/Quadratic/CubicTo + Close contours), possibly
	//             self-intersecting and overlapping one another.
	//   coverage  closed contours whose union is everything those strokes cover:
	//             a band per segment between its two untrimmed offset curves and
	//             a wedge per join side.
	//   shape     the original path, whose fill (under `rule`) bounds inside
	//             strokes and is excluded from outside strokes.
	//
	// The region kept is: covered, and inside the shape (inside alignment) /
	// outside it (outside alignment) / anywhere (center). Its boundary is
	// returned as simple contours wound +1 around it, so the result fills the
	// same under nonzero and even-odd. Curves are split at crossings, never
	// flattened. When nothing needs resolving, `outline` is returned unchanged.
	std::vector<PathCommand> resolve_stroke_outline(
		const std::vector<PathCommand>& outline,
		const std::vector<std::vector<PathCommand>>& coverage,
		const std::vector<PathCommand>& shape,
		FillRule rule,
		StrokeAlign align);
}
