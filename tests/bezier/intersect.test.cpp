#include <doctest/doctest.h>

#include <cmath>

#include "bezier/bezier.hpp"
#include "bezier/intersect.hpp"

using namespace stroke_outline;
using namespace stroke_outline::bezier;

namespace
{
	Vec2 at(const QuadraticBezier& q, double t) { return quadratic_bezier(q.p0, q.p1, q.p2, t); }
	Vec2 at(const CubicBezier& c, double t) { return cubic_bezier(c.p0, c.p1, c.p2, c.p3, t); }
	Vec2 at_line(Vec2 a, Vec2 b, double t) { return Vec2::lerp(a, b, t); }

	// Each reported intersection's point lies on both curves at its parameters.
	template<class A, class B>
	void check_hits(const std::vector<CurveIntersection>& hits, const A& a, const B& b)
	{
		for (const auto& h: hits)
		{
			CHECK(h.t1 >= 0.0);
			CHECK(h.t1 <= 1.0);
			CHECK(h.t2 >= 0.0);
			CHECK(h.t2 <= 1.0);
			CHECK((a(h.t1) - h.point).length() < 1e-3);
			CHECK((b(h.t2) - h.point).length() < 1e-3);
		}
	}

	const QuadraticBezier kArchQ { { 0, 0 }, { 50, 100 }, { 100, 0 } };              // peak y = 50
	const CubicBezier kArchC { { 0, 0 }, { 0, 100 }, { 100, 100 }, { 100, 0 } };     // peak y = 75
}

TEST_CASE("a line crosses a quadratic arch twice")
{
	const auto hits = line_quadratic_intersections({ -10, 25 }, { 110, 25 }, kArchQ);
	CHECK(hits.size() == 2);
	check_hits(hits, [](double t) { return at_line({ -10, 25 }, { 110, 25 }, t); },
			   [](double t) { return at(kArchQ, t); });
	// A segment that stops short of the arch doesn't.
	CHECK(line_quadratic_intersections({ 40, 60 }, { 60, 60 }, kArchQ).empty());
}

TEST_CASE("a line crosses a cubic arch twice")
{
	const auto hits = line_cubic_intersections({ -10, 50 }, { 110, 50 }, kArchC);
	CHECK(hits.size() == 2);
	check_hits(hits, [](double t) { return at_line({ -10, 50 }, { 110, 50 }, t); },
			   [](double t) { return at(kArchC, t); });
}

TEST_CASE("two quadratics crossing twice")
{
	const QuadraticBezier valley { { 0, 50 }, { 50, -50 }, { 100, 50 } };
	const auto hits = quadratic_quadratic_intersections(kArchQ, valley);
	CHECK(hits.size() == 2);
	check_hits(hits, [](double t) { return at(kArchQ, t); },
			   [&](double t) { return at(valley, t); });
}

TEST_CASE("a quadratic and a cubic crossing")
{
	const CubicBezier valley { { 0, 60 }, { 0, -20 }, { 100, -20 }, { 100, 60 } };
	const auto hits = quadratic_cubic_intersections(kArchQ, valley);
	CHECK(hits.size() == 2);
	check_hits(hits, [](double t) { return at(kArchQ, t); },
			   [&](double t) { return at(valley, t); });
}

TEST_CASE("two cubics crossing")
{
	const CubicBezier s { { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 } };
	const CubicBezier back { { 0, 100 }, { 100, 100 }, { 0, 0 }, { 100, 0 } };
	const auto hits = cubic_cubic_intersections(s, back);
	CHECK(!hits.empty());
	check_hits(hits, [&](double t) { return at(s, t); }, [&](double t) { return at(back, t); });
}

TEST_CASE("identical curves are coincident, not intersecting")
{
	CHECK(quadratic_quadratic_intersections(kArchQ, kArchQ).empty());
	CHECK(cubic_cubic_intersections(kArchC, kArchC).empty());
}

TEST_CASE("cubic_self_intersection finds a loop's crossing point")
{
	// The controls cross over, so x runs right, back left and right again.
	const CubicBezier loop { { 0, 0 }, { 200, 100 }, { -100, 100 }, { 100, 0 } };
	const auto hit = cubic_self_intersection(loop);
	REQUIRE(hit);
	CHECK(std::abs(hit->t1 - hit->t2) > 0.1);
	CHECK((at(loop, hit->t1) - at(loop, hit->t2)).length() < 1e-3);

	CHECK(!cubic_self_intersection(kArchC));
}
