#pragma once

#include <vector>
#include "stroke_outline/settings.hpp"
#include "stroke_outline/Vec2.hpp"
#include "stroke_outline/PathCommand.hpp"

namespace stroke_outline
{
	// Cap of an open stroke at `position` (on the centerline), facing `tangent`,
	// between its two edge points. A start cap begins with a MoveTo at
	// `right_point` and ends at `left_point`; an end cap runs from `left_point`
	// to `right_point`. Round and square caps are for centered strokes; a
	// one-sided (aligned) open stroke only ever uses butt caps here — aligned
	// round/square caps go through clip_to_shape's resolution instead.
	std::vector<PathCommand> generate_cap(
		Vec2 position,
		Vec2 tangent,
		Vec2 left_point,
		Vec2 right_point,
		LineCap cap_type,
		bool is_start_cap);

	// The outline of a zero-length subpath or dash: a circle (round cap) or a
	// square oriented along `tangent` (square cap), side 2 * half_width. Empty
	// for butt caps, which draw nothing there.
	std::vector<PathCommand>
		generate_dot(Vec2 position, Vec2 tangent, double half_width, LineCap cap_type);
}
