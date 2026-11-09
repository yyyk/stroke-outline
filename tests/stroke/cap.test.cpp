#include <doctest/doctest.h>

#include <cmath>

// Line caps and dot shapes (src/stroke/cap.cpp), observed through the public API.
#include "support/outline.hpp"

using namespace stroke_outline;
using namespace test_support;

TEST_CASE("line caps on an open horizontal segment")
{
	const Path line = Path {}.move_to(0, 0).line_to(100, 0);
	auto settings = width(20.0);

	SUBCASE("butt ends flush with the endpoints")
	{
		settings.line_cap = LineCap::Butt;
		const Subpath s = stroke_single(line, settings);
		CHECK(s.closed);
		CHECK(s.cubics == 0);
		CHECK(s.min_x() == doctest::Approx(0.0));
		CHECK(s.max_x() == doctest::Approx(100.0));
		CHECK(s.min_y() == doctest::Approx(-10.0));
		CHECK(s.max_y() == doctest::Approx(10.0));
	}

	SUBCASE("square extends half the width past each endpoint")
	{
		settings.line_cap = LineCap::Square;
		const Subpath s = stroke_single(line, settings);
		CHECK(s.closed);
		CHECK(s.cubics == 0);
		CHECK(s.min_x() == doctest::Approx(-10.0));
		CHECK(s.max_x() == doctest::Approx(110.0));
		CHECK(s.min_y() == doctest::Approx(-10.0));
		CHECK(s.max_y() == doctest::Approx(10.0));
	}

	SUBCASE("round adds a semicircle at each endpoint")
	{
		settings.line_cap = LineCap::Round;
		const Subpath s = stroke_single(line, settings);
		CHECK(s.closed);
		CHECK(s.cubics >= 2);
		CHECK(contains_point(s, { -10, 0 }));
		CHECK(contains_point(s, { 110, 0 }));
		CHECK(s.min_x() == doctest::Approx(-10.0));
		CHECK(s.max_x() == doctest::Approx(110.0));
	}
}

TEST_CASE("square-cap dots follow the path direction")
{
	// On a 45° line each square dot is rotated 45°: a diamond reaching
	// half_width * sqrt(2) along the axes.
	auto settings = width(10.0);
	settings.line_cap = LineCap::Square;
	settings.dash = DashSettings { { 0.0, 1000.0 }, 0.0 };

	const auto dots =
		subpaths_of(build_outline(Path {}.move_to(0, 0).line_to(50, 50), settings));

	REQUIRE(dots.size() == 1);
	CHECK(dots[0].max_x() == doctest::Approx(5.0 * std::sqrt(2.0)));
	CHECK(dots[0].max_y() == doctest::Approx(5.0 * std::sqrt(2.0)));
}
