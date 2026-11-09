#pragma once

namespace stroke_outline
{
	struct Eps
	{
		// Absolute lengths, chosen for SVG-like coordinates (features ~10 units).
		double point;   // two points closer than this are the same point
		double linear;  // general-purpose small length

		// Dimensionless.
		double param;   // curve parameter t, and other ratios near 1
		double angle;   // angles, in radians
		double solver;  // "~0" for normalized polynomial coefficients, squared lengths

		// Relative: a fraction of a size, so they hold at any scale.
		double relative;      // smaller than this fraction of a feature: coincident
		double noise;         // floating-point noise, as a fraction of coordinate magnitude
		double arc_radius;    // allowed mismatch of an arc's two radii, relative to the radius
		double svg_rounding;  // largest SVG output rounding, relative to the path's size
	};

	inline constexpr Eps EPSILON {
		.point = 1e-5,
		.linear = 1e-4,
		.param = 1e-8,
		.angle = 1e-12,
		.solver = 1e-12,
		.relative = 1e-6,
		.noise = 1e-12,
		.arc_radius = 1e-3,
		.svg_rounding = 1e-4,
	};
}
