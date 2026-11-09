#include <algorithm>
#include <cmath>
#include "stroke/utils.hpp"
#include "math/Eps.hpp"

namespace stroke_outline
{
	Vec2 compute_miter_normal(Vec2 v1, Vec2 v2)
	{
		const Vec2 n1 = perpendicular(v1);
		const Vec2 v12 = v1 + v2;
		if (v12.length_squared() < EPSILON.solver)
			return Vec2::zero();
		const Vec2 tangent = *(v12.normalize());
		const Vec2 n = perpendicular(tangent);
		const double inv_miter_len = n.dot(n1);
		if (std::abs(inv_miter_len) < EPSILON.solver)
			return n1;
		return n / inv_miter_len;
	}

	std::vector<bezier::CubicBezier>
		arc_from_points_to_cubic_bezier(Vec2 center, Vec2 start, Vec2 end, bool is_clockwise)
	{
		const Vec2 start_vector = start - center;
		const Vec2 end_vector = end - center;
		const double radius = start_vector.length();
		// Start and end must be (nearly) equidistant from the center, relative to
		// the radius — an absolute limit would reject large round joins over
		// rounding noise and accept badly mismatched tiny ones.
		if (std::abs(radius - end_vector.length())
			> EPSILON.arc_radius * std::max(radius, end_vector.length()))
			return {};
		// Any positive radius is a real arc: a fixed minimum dropped the round
		// joins and caps of tiny strokes.
		if (!(radius > 0.0) || !std::isfinite(radius))
			return {};
		const double start_angle = std::atan2(start_vector.y, start_vector.x);
		const double end_angle = std::atan2(end_vector.y, end_vector.x);
		return bezier::
			arc_to_cubic_beziers(center, radius, start_angle, end_angle, is_clockwise);
	}
}
