#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "bezier/bezier.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	// Distance from p to the nearest of many samples along the curve.
	template <class Eval>
	double distance_to(Vec2 p, Eval eval)
	{
		double best = 1e300;
		for (int i = 0; i <= 4000; ++i)
			best = std::min(best, (eval(i / 4000.0) - p).length());
		return best;
	}

	template <class Curves, class EvalPiece, class EvalSource>
	void check_offset(
		const Curves& pieces,
		EvalPiece piece_at,
		EvalSource source_at,
		double d,
		double tolerance)
	{
		REQUIRE(!pieces.empty());
		for (std::size_t k = 0; k < pieces.size(); ++k)
		{
			// Pieces join end to end.
			if (k > 0)
				CHECK(
					(piece_at(pieces[k - 1], 1.0) - piece_at(pieces[k], 0.0)).length() < 1e-9);
			for (int i = 0; i <= 16; ++i)
				CHECK(
					distance_to(piece_at(pieces[k], i / 16.0), source_at)
					== doctest::Approx(std::abs(d)).epsilon(tolerance));
		}
	}

	Vec2 quad_at(const QuadraticBezier& q, double t)
	{
		return quadratic_bezier(q.p0, q.p1, q.p2, t);
	}
	Vec2 cubic_at(const CubicBezier& c, double t)
	{
		return cubic_bezier(c.p0, c.p1, c.p2, c.p3, t);
	}
}

TEST_CASE("offsetting a straight quadratic gives a parallel line")
{
	const auto pieces = offset_quadratic_bezier({ 0, 0 }, { 50, 0 }, { 100, 0 }, 5.0);
	REQUIRE(!pieces.empty());
	for (const auto& q: pieces)
		for (const Vec2 p: { q.p0, q.p1, q.p2 })
			CHECK(std::abs(p.y) == doctest::Approx(5.0));
}

TEST_CASE("curves whose controls sit on their endpoints offset as straight lines")
{
	// These take the "line-like" path: the derivative vanishes at an end.
	const auto q = offset_quadratic_bezier({ 0, 0 }, { 0, 0 }, { 100, 0 }, 5.0);
	REQUIRE(!q.empty());
	for (const auto& piece: q)
		for (const Vec2 p: { piece.p0, piece.p1, piece.p2 })
			CHECK(std::abs(p.y) == doctest::Approx(5.0));

	const auto c = offset_cubic_bezier({ 0, 0 }, { 0, 0 }, { 100, 0 }, { 100, 0 }, 5.0);
	REQUIRE(!c.empty());
	for (const auto& piece: c)
		for (const Vec2 p: { piece.p0, piece.p1, piece.p2, piece.p3 })
			CHECK(std::abs(p.y) == doctest::Approx(5.0));
}

TEST_CASE("offset curves stay at the offset distance from the source")
{
	SUBCASE("gentle quadratic")
	{
		const QuadraticBezier src { { 0, 0 }, { 50, 20 }, { 100, 0 } };
		check_offset(
			offset_quadratic_bezier(src.p0, src.p1, src.p2, 5.0),
			quad_at,
			[&](double t) { return quad_at(src, t); },
			5.0,
			0.01);
	}
	SUBCASE("quarter-circle-like cubic, both sides")
	{
		const CubicBezier src { { 0, 0 }, { 55, 0 }, { 100, 45 }, { 100, 100 } };
		for (const double d: { 5.0, -5.0 })
		{
			CAPTURE(d);
			check_offset(
				offset_cubic_bezier(src.p0, src.p1, src.p2, src.p3, d),
				cubic_at,
				[&](double t) { return cubic_at(src, t); },
				d,
				0.02);
		}
	}
}

TEST_CASE("a curve collapsed to a point has no offset")
{
	CHECK(offset_quadratic_bezier({ 3, 3 }, { 3, 3 }, { 3, 3 }, 5.0).empty());
	CHECK(offset_cubic_bezier({ 3, 3 }, { 3, 3 }, { 3, 3 }, { 3, 3 }, 5.0).empty());
}

TEST_CASE("straight curves with controls along the line offset as the moved curve")
{
	// Regression: the general construction intersects the end normals, which
	// are parallel for a straight curve, so these used to produce no offset and
	// the stroke collapsed to a zero-width line.
	const auto q = offset_quadratic_bezier({ 0, 0 }, { 30, 0 }, { 100, 0 }, 5.0);
	REQUIRE(q.size() == 1);
	CHECK(std::abs(q[0].p0.y) == doctest::Approx(5.0));
	CHECK(q[0].p1.x == doctest::Approx(30.0));  // controls keep their spacing
	CHECK(q[0].p1.y == doctest::Approx(q[0].p0.y));

	const auto c = offset_cubic_bezier({ 0, 0 }, { 0, 0 }, { 50, 0 }, { 100, 0 }, -5.0);
	REQUIRE(c.size() == 1);
	for (const Vec2 p: { c[0].p0, c[0].p1, c[0].p2, c[0].p3 })
		CHECK(std::abs(p.y) == doctest::Approx(5.0));
	// Opposite distances land on opposite sides.
	const auto other = offset_cubic_bezier({ 0, 0 }, { 0, 0 }, { 50, 0 }, { 100, 0 }, 5.0);
	CHECK(other[0].p0.y * c[0].p0.y < 0);
}
