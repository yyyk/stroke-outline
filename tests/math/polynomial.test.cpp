#include <doctest/doctest.h>

#include <vector>

#include "math/polynomial.hpp"

using namespace stroke_outline;

namespace
{
	std::vector<double> sorted(Roots roots)
	{
		roots.sort();
		return { roots.begin(), roots.end() };
	}

	void check_roots(const Roots& roots, std::vector<double> expected)
	{
		const auto got = sorted(roots);
		REQUIRE(got.size() == expected.size());
		for (std::size_t i = 0; i < got.size(); ++i)
			CHECK(got[i] == doctest::Approx(expected[i]).epsilon(1e-9));
	}
}

TEST_CASE("solve_linear finds the root, or none when there is no x term")
{
	check_roots(solve_linear(2.0, -4.0), { 2.0 });
	check_roots(solve_linear(0.0, 3.0), {});
	check_roots(solve_linear(0.0, 0.0), {});
}

TEST_CASE("solve_quadratic: two roots, a double root, none, and the linear fallback")
{
	check_roots(solve_quadratic(1.0, -3.0, 2.0), { 1.0, 2.0 });  // (x-1)(x-2)
	check_roots(solve_quadratic(1.0, -2.0, 1.0), { 1.0 });       // (x-1)²
	check_roots(solve_quadratic(1.0, 0.0, 1.0), {});             // x²+1
	check_roots(solve_quadratic(0.0, 2.0, -4.0), { 2.0 });       // really 2x-4
}

TEST_CASE("solve_cubic: three real roots, one real root, a triple root, the quadratic fallback")
{
	check_roots(solve_cubic(1.0, -6.0, 11.0, -6.0), { 1.0, 2.0, 3.0 });  // (x-1)(x-2)(x-3)
	check_roots(solve_cubic(1.0, 0.0, 0.0, -8.0), { 2.0 });             // x³-8
	check_roots(solve_cubic(1.0, -3.0, 3.0, -1.0), { 1.0 });            // (x-1)³
	check_roots(solve_cubic(0.0, 1.0, -3.0, 2.0), { 1.0, 2.0 });        // really quadratic
}

TEST_CASE("a range keeps only the roots inside it")
{
	check_roots(solve_quadratic(1.0, -3.0, 2.0, std::pair { 0.0, 1.5 }), { 1.0 });
	check_roots(solve_cubic(1.0, -6.0, 11.0, -6.0, std::pair { 1.5, 3.5 }), { 2.0, 3.0 });
	check_roots(solve_linear(2.0, -4.0, std::pair { 0.0, 1.0 }), {});
}

TEST_CASE("the solvers don't depend on the scale of the coefficients")
{
	// Coefficients carry units (lengths at whatever scale the geometry is), and
	// scaling them all doesn't change the roots. The "~0" checks used to be
	// absolute, so tiny coefficients were mistaken for a lower degree.
	for (const double s: { 1e-12, 1e-6, 1e6, 1e12 })
	{
		CAPTURE(s);
		check_roots(solve_linear(2.0 * s, -4.0 * s), { 2.0 });
		check_roots(solve_quadratic(1.0 * s, -3.0 * s, 2.0 * s), { 1.0, 2.0 });
		check_roots(solve_cubic(1.0 * s, -6.0 * s, 11.0 * s, -6.0 * s), { 1.0, 2.0, 3.0 });
	}
}
