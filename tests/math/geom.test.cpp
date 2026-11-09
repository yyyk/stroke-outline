#include <doctest/doctest.h>

#include <cmath>

#include "math/geom.hpp"

using namespace stroke_outline;

TEST_SUITE_BEGIN("geom");

TEST_CASE("points_are_too_close: the epsilon boundary is inclusive")
{
	CHECK(points_are_too_close(Vec2 { 0, 0 }, Vec2 { EPSILON.point, 0 }));
}

TEST_CASE("points_are_too_close: just beyond epsilon is not too close")
{
	CHECK_FALSE(points_are_too_close(Vec2 { 0, 0 }, Vec2 { EPSILON.point * 1.01, 0 }));
}

TEST_CASE("points_are_too_close: identical vs far-apart points")
{
	CHECK(points_are_too_close(Vec2 { 3, 4 }, Vec2 { 3, 4 }));
	CHECK_FALSE(points_are_too_close(Vec2 { 0, 0 }, Vec2 { 1, 1 }));
}

TEST_CASE("line_intersection: two crossing lines meet at the expected point")
{
	const auto p =
		line_intersection(Vec2 { 0, 0 }, Vec2 { 10, 10 }, Vec2 { 0, 10 }, Vec2 { 10, 0 });
	REQUIRE(p.has_value());
	CHECK(*p == Vec2 { 5, 5 });
}

TEST_CASE("line_intersection: parallel lines return nullopt")
{
	const auto p =
		line_intersection(Vec2 { 0, 0 }, Vec2 { 10, 0 }, Vec2 { 0, 5 }, Vec2 { 10, 5 });
	CHECK_FALSE(p.has_value());
}

TEST_CASE("line_intersection: intersects the infinite lines, not the segments")
{
	const auto p =
		line_intersection(Vec2 { 0, 0 }, Vec2 { 1, 1 }, Vec2 { 0, 20 }, Vec2 { 1, 19 });
	REQUIRE(p.has_value());
	CHECK(*p == Vec2 { 10, 10 });
}


TEST_CASE("line_intersection: the parallel test is an angle, so it works at any scale")
{
	// Two lines crossing at 1e-6 rad: well above the parallel threshold. This
	// used to compare the raw cross product (a squared length) with 1e-4, so
	// short or slightly converging lines counted as parallel.
	const double angle = 1e-6;
	for (const double s: { 1e-6, 1.0, 1e6 })
	{
		CAPTURE(s);
		const auto hit =
			line_intersection({ 0, 0 }, { s, 0 }, { 0, s }, { s, s - s * std::tan(angle) });
		REQUIRE(hit);
		CHECK(hit->y == doctest::Approx(0.0).scale(s));
	}
	// Exactly parallel lines still don't meet.
	CHECK(!line_intersection({ 0, 0 }, { 1e-6, 0 }, { 0, 1e-6 }, { 1e-6, 1e-6 }));
}

TEST_CASE("line_segment_intersection: crossing, missing, touching and parallel segments")
{
	const auto x = line_segment_intersection({ 0, 0 }, { 10, 10 }, { 0, 10 }, { 10, 0 });
	REQUIRE(x);
	CHECK(x->first == doctest::Approx(0.5));
	CHECK(x->second == doctest::Approx(0.5));

	// The infinite lines cross, but outside the segments.
	CHECK(!line_segment_intersection({ 0, 0 }, { 1, 1 }, { 5, 0 }, { 6, -1 }));

	// Touching at an endpoint.
	const auto touch = line_segment_intersection({ 0, 0 }, { 10, 0 }, { 10, 0 }, { 10, 10 });
	REQUIRE(touch);
	CHECK(touch->first == doctest::Approx(1.0));
	CHECK(touch->second == doctest::Approx(0.0));

	CHECK(!line_segment_intersection({ 0, 0 }, { 10, 0 }, { 0, 1 }, { 10, 1 }));
}

TEST_CASE("line_segment_intersection: segments that collapse to a point")
{
	// A point lying on the other segment meets it at the projected parameter.
	const auto on = line_segment_intersection({ 5, 0 }, { 5, 0 }, { 0, 0 }, { 10, 0 });
	REQUIRE(on);
	CHECK(on->second == doctest::Approx(0.5));
	CHECK(!line_segment_intersection({ 5, 1 }, { 5, 1 }, { 0, 0 }, { 10, 0 }));

	// Same the other way round.
	const auto on2 = line_segment_intersection({ 0, 0 }, { 10, 0 }, { 2, 0 }, { 2, 0 });
	REQUIRE(on2);
	CHECK(on2->first == doctest::Approx(0.2));

	// Two points: they meet only if they coincide.
	CHECK(line_segment_intersection({ 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 }));
	CHECK(!line_segment_intersection({ 3, 3 }, { 3, 3 }, { 4, 3 }, { 4, 3 }));
}

TEST_SUITE_END();
