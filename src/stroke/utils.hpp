#pragma once

#include <vector>
#include <optional>
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"
#include "bezier/bezier.hpp"

namespace stroke_outline
{
	Vec2 compute_miter_normal(Vec2 v1, Vec2 v2);

	constexpr bool is_miter_limit_exceeded(Vec2 normal, double miter_limit)
	{
		return normal.length_squared() > miter_limit * miter_limit;
	}

	std::vector<bezier::CubicBezier>
		arc_from_points_to_cubic_bezier(Vec2 center, Vec2 start, Vec2 end, bool is_clockwise);

	inline std::optional<Vec2> end_point(const PathCommand& command)
	{
		if (std::holds_alternative<MoveTo>(command))
			return std::get<MoveTo>(command).p;
		else if (std::holds_alternative<LineTo>(command))
			return std::get<LineTo>(command).p;
		else if (std::holds_alternative<QuadraticTo>(command))
			return std::get<QuadraticTo>(command).end;
		else if (std::holds_alternative<CubicTo>(command))
			return std::get<CubicTo>(command).end;
		else
			return std::nullopt;
	}
}
