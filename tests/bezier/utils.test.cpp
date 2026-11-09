#include <doctest/doctest.h>

#include <algorithm>

#include "bezier/utils.hpp"

using namespace stroke_outline::bezier;

TEST_CASE("find_derivative_extrema_for_quadratic finds the turning point, if any")
{
	const auto peak = find_derivative_extrema_for_quadratic(0, 10, 0);
	REQUIRE(peak);
	CHECK(*peak == doctest::Approx(0.5));
	CHECK(!find_derivative_extrema_for_quadratic(0, 5, 10));   // monotone (straight)
	CHECK(!find_derivative_extrema_for_quadratic(0, 30, 50));  // would turn at t = 3
	CHECK(!find_derivative_extrema_for_quadratic(4, 4, 4));    // constant
}

TEST_CASE("find_derivative_extrema_for_cubic finds up to two turning points")
{
	const auto arch = find_derivative_extrema_for_cubic(0, 10, 10, 0);
	REQUIRE(arch.count == 1);
	CHECK(arch.t[0] == doctest::Approx(0.5));

	// S-curve: P'(t) ∝ t² - t + 1/6, roots (1 ± 1/√3) / 2.
	const auto s = find_derivative_extrema_for_cubic(0, 10, -10, 0);
	REQUIRE(s.count == 2);
	const double lo = std::min(s.t[0], s.t[1]), hi = std::max(s.t[0], s.t[1]);
	CHECK(lo == doctest::Approx(0.2113248654));
	CHECK(hi == doctest::Approx(0.7886751346));

	CHECK(find_derivative_extrema_for_cubic(0, 1, 2, 3).count == 0);  // straight
	CHECK(find_derivative_extrema_for_cubic(5, 5, 5, 5).count == 0);  // constant
}

TEST_CASE("extrema don't depend on the scale of the coordinates")
{
	// The "~0" tests on the derivative's coefficients used to be absolute, so
	// tiny curves lost their turning points.
	for (const double k: { 1e-9, 1e9 })
	{
		CAPTURE(k);
		const auto peak = find_derivative_extrema_for_quadratic(0, 10 * k, 0);
		REQUIRE(peak);
		CHECK(*peak == doctest::Approx(0.5));
		const auto s = find_derivative_extrema_for_cubic(0, 10 * k, -10 * k, 0);
		CHECK(s.count == 2);
	}
}
