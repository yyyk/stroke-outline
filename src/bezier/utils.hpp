#pragma once

#include <optional>
#include <array>
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline::bezier
{
	template <class T>
	constexpr T second_difference(T p0, T p1, T p2)
	{
		return p0 - 2.0 * p1 + p2;
	}

	std::optional<double>
		find_derivative_extrema_for_quadratic(double p0, double p1, double p2);

	struct CubicExtrema
	{
		std::array<double, 2> t;
		int count;
	};

	CubicExtrema find_derivative_extrema_for_cubic(double p0, double p1, double p2, double p3);
}
