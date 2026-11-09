#include <algorithm>
#include "stroke/cap.hpp"
#include "stroke/utils.hpp"

namespace stroke_outline
{
	std::vector<PathCommand> generate_cap(
		Vec2 position,
		Vec2 tangent,
		Vec2 left_point,
		Vec2 right_point,
		LineCap cap_type,
		bool is_start_cap)
	{
		tangent = tangent.normalize().value_or(Vec2(1.0, 0.0));
		std::vector<PathCommand> commands;
		switch (cap_type)
		{
			case LineCap::Butt:
			{
				if (is_start_cap)
					commands.push_back(MoveTo { left_point });
				else
					commands.push_back(LineTo { right_point });
				break;
			}
			case LineCap::Square:
			{
				const double reach = std::
					max((left_point - position).length(), (right_point - position).length());
				const Vec2 extension = tangent * reach;
				const Vec2 left_extended = left_point + extension;
				const Vec2 right_extended = right_point + extension;
				if (is_start_cap)
				{
					commands.push_back(MoveTo { right_extended });
					commands.push_back(LineTo { left_extended });
					commands.push_back(LineTo { left_point });
				}
				else
				{
					commands.push_back(LineTo { left_extended });
					commands.push_back(LineTo { right_extended });
					commands.push_back(LineTo { right_point });
				}
				break;
			}
			case LineCap::Round:
			{
				const auto curves = arc_from_points_to_cubic_bezier(
					position,
					is_start_cap ? right_point : left_point,
					is_start_cap ? left_point : right_point,
					true);
				if (is_start_cap)
					commands.push_back(MoveTo { right_point });
				for (const auto& curve: curves)
					commands.push_back(CubicTo { curve.p1, curve.p2, curve.p3 });
				break;
			}
		}
		return commands;
	}

	std::vector<PathCommand>
		generate_dot(Vec2 position, Vec2 tangent, double half_width, LineCap cap_type)
	{
		if (cap_type == LineCap::Butt || !(half_width > 0.0))
			return {};
		tangent = tangent.normalize().value_or(Vec2(1.0, 0.0));
		// The caps of a zero-length open subpath, back to back: same left/right
		// points (offset sideways by half_width) at both ends.
		const Vec2 side = perpendicular(tangent) * half_width;
		const Vec2 left = position + side;
		const Vec2 right = position - side;
		auto commands = generate_cap(position, -tangent, left, right, cap_type, true);
		const auto end = generate_cap(position, tangent, left, right, cap_type, false);
		commands.insert(commands.end(), end.begin(), end.end());
		commands.push_back(Close {});
		return commands;
	}
}
