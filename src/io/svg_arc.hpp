#pragma once

#include <vector>
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline::svg
{
	// Convert one SVG elliptical-arc command ("A"/"a") into a sequence of cubic
	// béziers. Uses the endpoint parameterization straight from the SVG spec:
	// the arc runs from `from` to `to`, with radii rx/ry, the ellipse's x-axis
	// rotated `x_axis_rotation_deg` degrees, and the two flags selecting which of
	// the four candidate arcs to take.
	//
	// Faithful port of svg-arc-to-cubic-bezier (Copyright (c) 2017, Colin
	// Meinke; ISC license — see THIRD_PARTY_NOTICES):
	// https://github.com/colinmeinke/svg-arc-to-cubic-bezier
	//
	// Returns an empty vector when the arc degenerates (rx or ry is 0, or the two
	// endpoints coincide) — per the spec the caller should then treat it as a
	// straight line to `to` (or omit it entirely for coincident endpoints).
	std::vector<CubicTo> arc_to_cubics(
		Vec2 from,
		double rx,
		double ry,
		double x_axis_rotation_deg,
		bool large_arc_flag,
		bool sweep_flag,
		Vec2 to);
}
