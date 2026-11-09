#pragma once
#include <vector>
#include "stroke_outline/settings.hpp"
#include "stroke_outline/Vec2.hpp"
#include "stroke_outline/PathCommand.hpp"

namespace stroke_outline
{
	enum class JoinSide { Left, Right };

	struct SidePointInfo
	{
		Vec2 prev;
		Vec2 next;
	};

	struct JoinSideResult
	{
		enum class Kind { Inner, Outer } kind;
		std::vector<PathCommand> commands;
	};

	struct JoinData
	{
		Vec2 position;
		double left_offset;
		double right_offset;
		Vec2 prev_tangent;
		Vec2 next_tangent;
		SidePointInfo left_side;
		SidePointInfo right_side;
		LineJoin join_type;

		JoinData(
			Vec2 position,
			Vec2 prev_tangent,
			Vec2 next_tangent,
			Vec2 left_prev,
			Vec2 left_next,
			Vec2 right_prev,
			Vec2 right_next,
			const StrokeSettings& settings,
			double left_offset,
			double right_offset)
			: position(position),
			  left_offset(left_offset),
			  right_offset(right_offset),
			  prev_tangent(prev_tangent.normalize().value_or(Vec2(1.0, 0.0))),
			  next_tangent(next_tangent.normalize().value_or(Vec2(1.0, 0.0))),
			  left_side { left_prev, left_next },
			  right_side { right_prev, right_next },
			  join_type(settings.line_join)
		{}

		JoinSideResult resolve_side_join(JoinSide side, const StrokeSettings& settings) const;

	private:
		bool side_is_inner(JoinSide side) const
		{
			const double cross = prev_tangent.cross(next_tangent);
			return (side == JoinSide::Left) ? (cross >= 0.0) : (cross < 0.0);
		}

		std::vector<PathCommand>
			outer_commands_for_side(JoinSide side, const StrokeSettings& settings) const;
	};
}
