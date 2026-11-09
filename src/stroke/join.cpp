#include <cmath>
#include "stroke/join.hpp"
#include "stroke/utils.hpp"
#include "math/Eps.hpp"
#include "math/geom.hpp"

namespace stroke_outline
{
	JoinSideResult
		JoinData::resolve_side_join(JoinSide side, const StrokeSettings& settings) const
	{
		if (side_is_inner(side))
		{
			const SidePointInfo side_points_info =
				(side == JoinSide::Left) ? left_side : right_side;
			// The offset edges were trimmed to meet at this corner, so there is
			// nothing left to bridge.
			if (points_are_too_close(side_points_info.prev, side_points_info.next))
				return { JoinSideResult::Kind::Inner, {} };
			// Otherwise the corner wasn't trimmed (segments too short for the offset
			// edges to cross within them). Bevel straight across the concave side —
			// connecting the two edge ends directly — rather than detouring through
			// the centerline, which would insert an extra corner there.
			return { JoinSideResult::Kind::Inner, { LineTo { side_points_info.next } } };
		}
		return { JoinSideResult::Kind::Outer, outer_commands_for_side(side, settings) };
	}

	std::vector<PathCommand>
		JoinData::outer_commands_for_side(JoinSide side, const StrokeSettings& settings) const
	{
		const SidePointInfo side_points_info =
			(side == JoinSide::Left) ? left_side : right_side;
		const double dot = prev_tangent.dot(next_tangent);
		if (dot > 0.9999)
			return { LineTo { side_points_info.next } };
		if (dot < -0.9999)
		{
			switch (join_type)
			{
				case LineJoin::Round:
				{
					const Vec2 start_vec = side_points_info.prev - position;
					const double cross = prev_tangent.cross(start_vec);
					const bool is_clockwise = cross > 0.0;
					const auto curves = arc_from_points_to_cubic_bezier(
						position,
						side_points_info.prev,
						side_points_info.next,
						is_clockwise);
					std::vector<PathCommand> commands;
					for (const auto& curve: curves)
						commands.push_back(CubicTo { curve.p1, curve.p2, curve.p3 });
					return commands;
				}
				default:
					return { LineTo { side_points_info.next } };
			}
		}
		if (join_type == LineJoin::Bevel)
			return { LineTo { side_points_info.next } };
		if (join_type == LineJoin::Round)
		{
			const Vec2 diff = side_points_info.next - side_points_info.prev;
			if (diff.length_squared() < EPSILON.solver)
				return {};
			const Vec2 start_vec = side_points_info.prev - position;
			const Vec2 end_vec = side_points_info.next - position;
			const double cross = start_vec.cross(end_vec);
			const bool is_clockwise = cross < 0.0;
			const auto curves = arc_from_points_to_cubic_bezier(
				position,
				side_points_info.prev,
				side_points_info.next,
				is_clockwise);
			std::vector<PathCommand> commands;
			for (const auto& curve: curves)
				commands.push_back(CubicTo { curve.p1, curve.p2, curve.p3 });
			return commands;
		}
		const Vec2 miter_normal = compute_miter_normal(prev_tangent, next_tangent);
		if (miter_normal.length_squared() < EPSILON.solver)
			return { LineTo { side_points_info.next } };
		const double cross = prev_tangent.cross(next_tangent);
		const Vec2 front_miter_normal = (cross >= 0.0) ? -miter_normal : miter_normal;
		const double offset = (side == JoinSide::Left) ? left_offset : right_offset;
		const Vec2 miter_point = position + front_miter_normal * offset;
		const bool miter_exceeded =
			is_miter_limit_exceeded(front_miter_normal, settings.miter_limit);
		if (miter_exceeded)
			return { LineTo { side_points_info.next } };
		return {
			LineTo { miter_point },
			LineTo { side_points_info.next },
		};
	}
}
