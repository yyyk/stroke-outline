#pragma once

#include <limits>
#include <span>
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline
{
	struct alignas(32) AABB
	{
		Vec2 min, max;

		constexpr AABB()
			: min(Vec2 { std::numeric_limits<double>::infinity(),
						 std::numeric_limits<double>::infinity() }),
			  max(Vec2 { -std::numeric_limits<double>::infinity(),
						 -std::numeric_limits<double>::infinity() })
		{}
		constexpr AABB(Vec2 min, Vec2 max) : min(min), max(max) {}

		static AABB from_points(std::span<const Vec2> points)
		{
			AABB aabb;
			for (const Vec2& point: points)
			{
				aabb.min = ::stroke_outline::min(aabb.min, point);
				aabb.max = ::stroke_outline::max(aabb.max, point);
			}
			return aabb;
		}

		friend constexpr bool operator==(const AABB&, const AABB&) = default;

		constexpr double left() const { return min.x; }
		constexpr double right() const { return max.x; }
		constexpr double bottom() const { return max.y; }
		constexpr double top() const { return min.y; }

		constexpr bool contains(Vec2 point) const
		{
			return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
		}

		constexpr bool intersects(const AABB& other) const
		{
			return !(
				other.min.x > max.x || other.max.x < min.x || other.min.y > max.y
				|| other.max.y < min.y);
		}

		constexpr AABB merge(const AABB& other) const
		{
			return { ::stroke_outline::min(min, other.min),
					 ::stroke_outline::max(max, other.max) };
		}
	};
}
