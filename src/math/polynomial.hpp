#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>
#include "math/Eps.hpp"

namespace stroke_outline
{
	struct Roots
	{
		std::array<double, 3> values {};
		std::size_t count = 0;

		const double* begin() const { return values.data(); }
		const double* end() const { return values.data() + count; }

		bool empty() const { return count == 0; }

		void push(double value)
		{
			if (count < values.size())
				values[count++] = value;
		}

		void sort() { std::sort(values.begin(), values.begin() + count); }

		void dedup()
		{
			if (count < 2)
				return;

			std::size_t write = 1;
			for (std::size_t i = 1; i < count; ++i)
				if (std::abs(values[i] - values[write - 1]) > EPSILON.param)
					values[write++] = values[i];
			count = write;
		}
	};

	// Divide coefficients by the largest magnitude among them. The roots are
	// unchanged, and the "is this coefficient ~0" tests below become relative,
	// so they behave the same whatever units (lengths, at any scale) the
	// coefficients carry. Returns false if every coefficient is zero.
	template<class... T>
	inline bool normalize_coefficients(T&... coefficients)
	{
		const double largest = std::max({ std::abs(coefficients)... });
		if (!(largest > 0.0) || !std::isfinite(largest))
			return false;
		((coefficients /= largest), ...);
		return true;
	}

	// Solve `a*x + b = 0`
	inline Roots solve_linear(
		double a,
		double b,
		std::optional<std::pair<double, double>> range = std::nullopt)
	{
		Roots roots;
		if (!normalize_coefficients(a, b))
			return roots;
		if (std::abs(a) < EPSILON.solver)
			return roots;

		double x = -b / a;
		if (range)
		{
			const auto [min, max] = *range;
			if (x < min - EPSILON.param || x > max + EPSILON.param)
				return roots;
			x = std::clamp(x, min, max);
		}
		roots.push(x);
		return roots;
	}

	inline Roots filter_range(Roots roots, std::optional<std::pair<double, double>> range)
	{
		if (!range)
			return roots;
		Roots out;
		auto [lo, hi] = *range;
		for (double x: roots)
			if (x >= lo - EPSILON.param && x <= hi + EPSILON.param)
				out.push(std::clamp(x, lo, hi));
		out.dedup();
		return out;
	}

	// Solve `a*x² + b*x + c = 0`
	inline Roots solve_quadratic(
		double a,
		double b,
		double c,
		std::optional<std::pair<double, double>> range = std::nullopt)
	{
		if (!normalize_coefficients(a, b, c))
			return {};
		if (std::abs(a) < EPSILON.solver)
			return solve_linear(b, c, range);

		Roots roots;
		double discriminant = b * b - 4 * a * c;
		if (discriminant < 0.0 && discriminant > -EPSILON.solver)
			discriminant = 0.0;
		if (discriminant < 0.0)
			return roots;

		if (discriminant <= EPSILON.solver)
		{
			const double x = -b / (2 * a);
			roots.push(x);
		}
		else
		{
			const double sqrt_discriminant = std::sqrt(discriminant);
			const double q = -0.5 * (b + std::copysign(sqrt_discriminant, b));
			if (std::abs(q) < EPSILON.solver)
			{
				const double x = -b / (2 * a);
				roots.push(x);
			}
			else
			{
				const double x1 = q / a;
				const double x2 = c / q;
				roots.push(x1);
				if (std::abs(x2 - x1) > EPSILON.param)
					roots.push(x2);
			}
		}

		roots.sort();

		// if (range)
		// {
		// 	Roots filtered_roots;
		// 	const auto [min, max] = *range;
		// 	for (double x: roots)
		// 		if (x >= min - EPSILON.param && x <= max + EPSILON.param)
		// 			filtered_roots.push(std::clamp(x, min, max));
		// 	filtered_roots.dedup();
		// 	return filtered_roots;
		// }
		// return roots;

		return filter_range(roots, range);
	}

	// Solve `a*x³ + b*x² + c*x + d = 0`
	inline Roots solve_cubic(
		double a,
		double b,
		double c,
		double d,
		std::optional<std::pair<double, double>> range = std::nullopt)
	{
		if (!normalize_coefficients(a, b, c, d))
			return {};
		if (std::abs(a) < EPSILON.solver)
			return solve_quadratic(b, c, d, range);

		// Normalize to monic: x^3 + B x^2 + C x + D = 0
		const double inv_a = 1.0 / a;
		b *= inv_a;
		c *= inv_a;
		d *= inv_a;

		// Depress: x = t - B/3 => t^3 + p t + q = 0
		const double shift = b / 3.0;
		const double p = c - (b * b) / 3.0;
		const double q = d - (b * c) / 3.0 + (2.0 * b * b * b) / 27.0;

		Roots roots;

		// If p ~ 0, it's basically t^3 + q = 0 (one real root)
		if (std::abs(p) < EPSILON.solver)
		{
			const double t = std::cbrt(-q);
			roots.push(t - shift);
			roots.sort();
			roots.dedup();

			// if (range)
			// {
			// 	Roots filtered_roots;
			// 	const auto [min, max] = *range;
			// 	for (double x: roots)
			// 		if (x >= min - EPSILON.param && x <= max + EPSILON.param)
			// 			filtered_roots.push(std::clamp(x, min, max));
			// 	filtered_roots.dedup();
			// 	return filtered_roots;
			// }
			// return roots;

			return filter_range(roots, range);
		}

		// Standard discriminant for depressed cubic:
		// Δ = (q/2)^2 + (p/3)^3
		const double half_q = q * 0.5;
		const double third_p = p / 3.0;
		const double delta = half_q * half_q + third_p * third_p * third_p;

		// Tolerance scaled to the magnitude of the terms (prevents silly misclassification)
		const double scale = std::abs(half_q * half_q) + std::abs(third_p * third_p * third_p);
		const double delta_eps = std::max(scale, 1.0) * EPSILON.solver;

		if (std::abs(delta) <= delta_eps)
		{
			// Repeated root(s): Δ ~ 0
			if (std::abs(q) < EPSILON.solver && std::abs(p) < EPSILON.solver)
			{
				// Triple root at t = 0
				roots.push(-shift);
			}
			else
			{
				// One single and one double root
				// t1 = 3q/p, t2 = -3q/(2p) (double)
				const double t1 = 3.0 * q / p;
				const double t2 = -0.5 * t1;
				roots.push(t1 - shift);
				if (std::abs(t2 - t1) > EPSILON.param)
					roots.push(t2 - shift);
			}
		}
		else if (delta > 0.0)
		{
			// One real root (Cardano)
			const double sqrt_delta = std::sqrt(delta);
			const double u = std::cbrt(-half_q + sqrt_delta);
			const double v = std::cbrt(-half_q - sqrt_delta);
			roots.push(u + v - shift);
		}
		else
		{
			// Three distinct real roots (trigonometric)
			// cos(3θ) = -q/(2 * sqrt(-(p/3)^3))
			const double r = std::sqrt(-third_p * third_p * third_p);  // positive
			if (std::abs(r) < EPSILON.solver)
			{
				// ultra-degenerate fallback (should be rare due to earlier p~0)
				const double t = std::cbrt(-q);
				roots.push(t - shift);
			}
			else
			{
				const double arg = std::clamp(-half_q / r, -1.0, 1.0);
				const double theta = std::acos(arg) / 3.0;
				const double m = 2.0 * std::sqrt(-third_p);  // since p < 0 in this branch
				const double tau = 2.0 * std::numbers::pi / 3.0;
				roots.push(m * std::cos(theta) - shift);
				roots.push(m * std::cos(theta - tau) - shift);
				roots.push(m * std::cos(theta + tau) - shift);
			}
		}

		roots.sort();
		roots.dedup();

		// if (range)
		// {
		// 	Roots filtered_roots;
		// 	const auto [min, max] = *range;
		// 	for (double x: roots)
		// 		if (x >= min - EPSILON.param && x <= max + EPSILON.param)
		// 			filtered_roots.push(std::clamp(x, min, max));
		// 	filtered_roots.dedup();
		// 	return filtered_roots;
		// }
		// return roots;

		return filter_range(roots, range);
	}
}
