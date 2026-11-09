#include "bezier/utils.hpp"

#include <cmath>

#include "math/Eps.hpp"
#include "math/polynomial.hpp"

namespace stroke_outline::bezier
{
	std::optional<double> find_derivative_extrema_for_quadratic(double p0, double p1, double p2)
	{
		// The derivative is ∝ (p1 - p0) + t·(p0 - 2p1 + p2); compare its slope
		// to the size of its terms, not to an absolute length.
		double slope = second_difference(p0, p1, p2);
		double offset = p0 - p1;
		if (!normalize_coefficients(slope, offset) || std::abs(slope) < EPSILON.solver)
			return std::nullopt;

		const double t = offset / slope;
		if (t < 0.0 || t > 1.0)
			return std::nullopt;

		return t;
	}

	CubicExtrema find_derivative_extrema_for_cubic(double p0, double p1, double p2, double p3)
	{
		// Cubic in power basis: P(t) = a t^3 + b t^2 + c t + d.
		const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
		const double b = 3.0 * p0 - 6.0 * p1 + 3.0 * p2;
		const double c = -3.0 * p0 + 3.0 * p1;

		// P'(t) = qa t^2 + qb t + qc, normalized so the "~0" tests below are
		// relative to the size of the coefficients, not absolute lengths.
		double qa = 3.0 * a;
		double qb = 2.0 * b;
		double qc = c;

		CubicExtrema result { .t = { 0.0, 0.0 }, .count = 0 };
		if (!normalize_coefficients(qa, qb, qc))
			return result;

		if (std::abs(qa) < EPSILON.solver)
		{
			if (std::abs(qb) < EPSILON.solver)
				return result;

			const double t = -qc / qb;
			if (t >= 0.0 && t <= 1.0)
			{
				result.t[0] = t;
				result.count = 1;
			}
			return result;
		}

		double disc = qb * qb - 4.0 * qa * qc;
		if (disc < 0.0 && disc > -EPSILON.solver)
			disc = 0.0;
		if (disc < 0.0)
			return result;

		if (disc <= EPSILON.solver)
		{
			const double t = -qb / (2.0 * qa);
			if (t >= 0.0 && t <= 1.0)
			{
				result.t[0] = t;
				result.count = 1;
			}
			return result;
		}

		const double sqrt_d = std::sqrt(disc);
		const double q = -0.5 * (qb + std::copysign(sqrt_d, qb));

		if (std::abs(q) < EPSILON.solver)
		{
			const double t = -qb / (2.0 * qa);
			if (t >= 0.0 && t <= 1.0)
			{
				result.t[0] = t;
				result.count = 1;
			}
			return result;
		}

		const double r0 = q / qa;
		const double r1 = qc / q;

		if (r0 >= 0.0 && r0 <= 1.0)
			result.t[result.count++] = r0;
		if (r1 >= 0.0 && r1 <= 1.0)
		{
			if (result.count == 0 || std::abs(result.t[0] - r1) > EPSILON.param)
				result.t[result.count++] = r1;
		}

		return result;
	}
}
