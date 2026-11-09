#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <cassert>

namespace stroke_outline
{
	struct alignas(16) Vec2
	{
		double x, y;

		Vec2() = default;
		constexpr Vec2(double x, double y) : x(x), y(y) {}

		constexpr double& operator[](int index)
		{
			assert(index == 0 || index == 1);
			return index == 0 ? x : y;
		}

		constexpr const double& operator[](int index) const
		{
			assert(index == 0 || index == 1);
			return index == 0 ? x : y;
		}

		friend constexpr bool operator==(Vec2, Vec2) = default;

		static constexpr Vec2 zero() { return { 0.0, 0.0 }; }

		constexpr double dot(Vec2 other) const { return x * other.x + y * other.y; }

		constexpr double cross(Vec2 other) const { return x * other.y - y * other.x; }

		constexpr double length_squared() const { return x * x + y * y; }

		double length() const { return std::sqrt(length_squared()); }

		constexpr double distance_squared(Vec2 other) const
		{
			const double dx = x - other.x, dy = y - other.y;
			return dx * dx + dy * dy;
		}
		double distance(Vec2 other) const { return std::sqrt(distance_squared(other)); }

		std::optional<Vec2> normalize() const
		{
			const double len = length();
			if (len == 0.0)
				return std::nullopt;
			const double inv_len = 1.0 / len;
			return Vec2 { x * inv_len, y * inv_len };
		}

		static constexpr Vec2 lerp(Vec2 lhs, Vec2 rhs, double t)
		{
			return { lhs.x + (rhs.x - lhs.x) * t, lhs.y + (rhs.y - lhs.y) * t };
		}

		// optional API
		// static constexpr Vec2 lerp_clamped(Vec2 lhs, Vec2 rhs, double t)
		// {
		// 	return lerp(lhs, rhs, std::clamp(t, 0.0, 1.0));
		// }
	};

	constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs)
	{
		return { lhs.x + rhs.x, lhs.y + rhs.y };
	}
	constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs)
	{
		return { lhs.x - rhs.x, lhs.y - rhs.y };
	}
	constexpr Vec2 operator-(Vec2 rhs)
	{
		return { -rhs.x, -rhs.y };
	}
	constexpr Vec2 operator*(Vec2 lhs, double rhs)
	{
		return { lhs.x * rhs, lhs.y * rhs };
	}
	constexpr Vec2 operator*(double lhs, Vec2 rhs)
	{
		return { lhs * rhs.x, lhs * rhs.y };
	}
	constexpr Vec2 operator/(Vec2 lhs, double rhs)
	{
		rhs = 1.0 / rhs;
		return { lhs.x * rhs, lhs.y * rhs };
	}

	constexpr Vec2& operator+=(Vec2& lhs, Vec2 rhs)
	{
		lhs.x += rhs.x;
		lhs.y += rhs.y;
		return lhs;
	}
	constexpr Vec2& operator-=(Vec2& lhs, Vec2 rhs)
	{
		lhs.x -= rhs.x;
		lhs.y -= rhs.y;
		return lhs;
	}
	constexpr Vec2& operator*=(Vec2& lhs, double rhs)
	{
		lhs.x *= rhs;
		lhs.y *= rhs;
		return lhs;
	}
	constexpr Vec2& operator/=(Vec2& lhs, double rhs)
	{
		rhs = 1.0 / rhs;
		lhs.x *= rhs;
		lhs.y *= rhs;
		return lhs;
	}

	// constexpr bool operator==(Vec2 lhs, Vec2 rhs) { return lhs.x == rhs.x && lhs.y == rhs.y;
	// } constexpr bool operator!=(Vec2 lhs, Vec2 rhs) { return !(lhs == rhs); } constexpr bool
	// operator<(Vec2 lhs, Vec2 rhs) { return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y <
	// rhs.y); } constexpr bool operator>(Vec2 lhs, Vec2 rhs) { return rhs < lhs; } constexpr
	// bool operator<=(Vec2 lhs, Vec2 rhs) { return !(lhs > rhs); } constexpr bool
	// operator>=(Vec2 lhs, Vec2 rhs) { return !(lhs < rhs); }

	constexpr Vec2 min(Vec2 lhs, Vec2 rhs)
	{
		return { std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y) };
	}
	constexpr Vec2 max(Vec2 lhs, Vec2 rhs)
	{
		return { std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y) };
	}

	constexpr Vec2 perpendicular(Vec2 v)
	{
		return { -v.y, v.x };
	}
}
