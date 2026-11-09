#pragma once

#include <vector>
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline
{
	// The original clip_to_shape implementation, kept for reference and
	// comparison. NOT USED by the stroker — resolve_stroke_outline (stroke/clip.hpp)
	// replaced it. Still compiled so it keeps building.
	//
	// Clip `subject` to the interior of `region` (both are closed contours:
	// MoveTo + Line/Quadratic/CubicTo + Close). Returns the parts of `subject`
	// that lie inside `region`, bridged along `region`'s boundary where `subject`
	// exits — i.e. subject ∩ region, via a curve-preserving Weiler-Atherton walk.
	//
	// Known limitations (why it was replaced, all covered by
	// tests/clip_to_shape.test.cpp against the new clipper):
	//   - Assumes both contours are simple and wound the same way. A stroke's
	//     inner boundary is wound opposite to the outer one, and an over-wide one
	//     crosses itself, so the walk can follow the wrong boundary and emit
	//     points outside `region` (e.g. a 100x100 square, inside stroke 150 wide).
	//   - With no crossings it keeps `subject` whole if it starts inside
	//     `region`, so an inner boundary that turned inside out but stayed inside
	//     (circle r=40, inside stroke 70 wide) is left as a spurious hole.
	//   - Crossings at an exact vertex are dropped, and collinear overlaps are
	//     not detected.
	//   - Output is only correct under the nonzero fill rule in many cases.
	std::vector<PathCommand> clip_contour_weiler_atherton(
		const std::vector<PathCommand>& subject,
		const std::vector<PathCommand>& region);
}
