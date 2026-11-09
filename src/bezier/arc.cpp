#include <numbers>
#include <cmath>
#include <optional>
#include "bezier/bezier.hpp"
#include "math/Eps.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	constexpr double PI = std::numbers::pi;
	constexpr double TWO_PI = 2 * PI;
	constexpr double HALF_PI = PI / 2;

	constexpr double normalize_angle(double angle)
	{
		double a = std::fmod(angle, TWO_PI);
		if (a < 0)
			a += TWO_PI;
		return a;
	}

	std::optional<CubicBezier> arc_segment_to_cubic(
		Vec2 center,
		double radius,
		double start_angle_radian,
		double end_angle_radian)
	{
		const double delta_angle = end_angle_radian - start_angle_radian;
		if (std::abs(delta_angle) < EPSILON.angle)
			return std::nullopt;

		// **The Magic Formula**: k = (4/3) * tan(θ/4)
		// This determines control point distance from endpoints.
		// For exactly ±90°, use Spencer Mortensen's constant (more precise than tan):
		// https://spencermortensen.com/articles/bezier-circle/
		double k;
		if (std::abs(delta_angle - HALF_PI) < EPSILON.solver)
			k = 0.551915024494;
		else if (std::abs(delta_angle + HALF_PI) < EPSILON.solver)
			k = -0.551915024494;
		else
			k = (4.0 / 3.0) * std::tan(delta_angle / 4.0);
		const Vec2 p0 =
			center + Vec2(std::cos(start_angle_radian), std::sin(start_angle_radian)) * radius;
		const Vec2 p3 =
			center + Vec2(std::cos(end_angle_radian), std::sin(end_angle_radian)) * radius;
		const Vec2 start_tangent =
			Vec2(-std::sin(start_angle_radian), std::cos(start_angle_radian));
		const Vec2 end_tangent = Vec2(-std::sin(end_angle_radian), std::cos(end_angle_radian));
		const Vec2 p1 = p0 + start_tangent * (k * radius);
		const Vec2 p2 = p3 - end_tangent * (k * radius);

		return CubicBezier { p0, p1, p2, p3 };
	}
}

namespace stroke_outline::bezier
{
	std::vector<CubicBezier> arc_to_cubic_beziers(
		Vec2 center,
		double radius,
		double start_angle_radian,
		double end_angle_radian,
		bool counterclockwise)
	{
		std::vector<CubicBezier> result;

		if (radius <= 0.0)
			return result;

		const double start = normalize_angle(start_angle_radian);
		const double end = normalize_angle(end_angle_radian);
		double total_angle;
		if (counterclockwise)
		{
			if (end > start)
				total_angle = -TWO_PI + (end - start);
			else if (end < start)
				total_angle = -(start - end);
			else
				total_angle = -TWO_PI;
		}
		else
		{
			if (end > start)
				total_angle = end - start;
			else if (end < start)
				total_angle = TWO_PI - (start - end);
			else
				total_angle = TWO_PI;
		}

		if (std::abs(total_angle) < EPSILON.angle)
			return result;

		// num_segments = ceil(abs(total_angle) / HALF_PI)
		// Guard: if ratio is within EPSILON.param of a whole number (e.g. 1.0000000001
		// due to floating-point drift), snap it down to avoid a spurious extra segment
		double ratio = std::abs(total_angle) / HALF_PI;
		if (std::abs(ratio - std::round(ratio)) < EPSILON.param)
			ratio = std::round(ratio);

		const int num_segments = std::max(1, static_cast<int>(std::ceil(ratio)));
		const double segment_angle = total_angle / static_cast<double>(num_segments);

		double current_angle = start;
		for (int i = 0; i < num_segments; ++i)
		{
			const double next_angle = current_angle + segment_angle;
			auto segment = arc_segment_to_cubic(center, radius, current_angle, next_angle);
			if (segment)
				result.push_back(*segment);
			current_angle = next_angle;
		}

		return result;
	}
}
