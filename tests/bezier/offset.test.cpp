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

namespace
{
	// The true offset of `c` by d at t: B(t) + d·N(t), N the tangent turned 90°.
	Vec2 true_offset(const CubicBezier& c, double d, double t)
	{
		const Vec2 tangent = cubic_bezier_derivative(c.p0, c.p1, c.p2, c.p3, t);
		return cubic_at(c, t) + d * Vec2 { -tangent.y, tangent.x } / tangent.length();
	}

	// Distance from p to the curve f(t), t in [0, 1]: samples, then golden
	// section around every local minimum of them.
	template <class F>
	double distance_to_curve(Vec2 p, F f)
	{
		constexpr int n = 1000;
		std::vector<double> ds(n + 1);
		for (int i = 0; i <= n; ++i)
			ds[i] = (f(static_cast<double>(i) / n) - p).length();
		double best = *std::min_element(ds.begin(), ds.end());
		const double g = (std::sqrt(5.0) - 1.0) / 2.0;
		for (int i = 0; i <= n; ++i)
		{
			if ((i > 0 && ds[i - 1] < ds[i]) || (i < n && ds[i + 1] < ds[i]))
				continue;
			double lo = std::max(0, i - 1) / static_cast<double>(n);
			double hi = std::min(n, i + 1) / static_cast<double>(n);
			const auto at = [&](double t) { return (f(t) - p).length(); };
			for (int k = 0; k < 60; ++k)
			{
				const double m1 = hi - g * (hi - lo);
				const double m2 = lo + g * (hi - lo);
				if (at(m1) < at(m2))
					hi = m2;
				else
					lo = m1;
			}
			best = std::min(best, at((lo + hi) / 2.0));
		}
		return best;
	}

	// The largest gap between the pieces and the true offset, both ways.
	double offset_gap(const std::vector<CubicBezier>& pieces, const CubicBezier& c, double d)
	{
		double worst = 0.0;
		const auto truth = [&](double t) { return true_offset(c, d, t); };
		for (const auto& piece: pieces)
			for (int i = 0; i <= 32; ++i)
				worst = std::max(worst, distance_to_curve(cubic_at(piece, i / 32.0), truth));
		for (int i = 1; i < 256; ++i)
		{
			double best = 1e300;
			for (const auto& piece: pieces)
			{
				const auto at = [&](double t) { return cubic_at(piece, t); };
				best = std::min(best, distance_to_curve(truth(i / 256.0), at));
			}
			worst = std::max(worst, best);
		}
		return worst;
	}
}

TEST_CASE("sharply bent curves are offset within 0.2% of the distance")
{
	// Without error control, these were off by 1.5%, 4% and 17% of d.
	struct Case
	{
		const char* name;
		CubicBezier curve;
		double d;
	};
	const Case cases[] = {
		{ "S-curve", { { 0, 0 }, { 100, 0 }, { 0, 100 }, { 100, 100 } }, 5.0 },
		{ "tight loop", { { 0, 0 }, { 150, 100 }, { -50, 100 }, { 100, 0 } }, 5.0 },
		{ "near-cusp",
		  { { 66.74577858575741, 81.43129003954115 },
		    { -99.35479722424123, -58.55475114747044 },
		    { 66.77985624433265, 54.76959317939138 },
		    { -19.606171739820732, 5.667480472146158 } },
		  2.1724827918843188 },
	};
	for (const auto& [name, c, d]: cases)
		for (const double side: { d, -d })
		{
			CAPTURE(name);
			CAPTURE(side);
			const auto pieces = offset_cubic_bezier(c.p0, c.p1, c.p2, c.p3, side);
			REQUIRE(!pieces.empty());
			// The check samples seven points per piece, so it can miss the worst
			// spot by a little: allow 10% over.
			CHECK(offset_gap(pieces, c, side) <= 1.1 * 2e-3 * d);
		}
}

TEST_CASE("the offset is the same at any scale")
{
	// A cusp: halving toward it makes pieces small enough that an absolute
	// threshold gave different pieces at different scales.
	const CubicBezier c { { 0, 0 }, { 100, 100 }, { 0, 100 }, { 100, 0 } };
	const auto base = offset_cubic_bezier(c.p0, c.p1, c.p2, c.p3, 5.0);
	for (const double s: { 1e-4, 1e6 })
	{
		CAPTURE(s);
		const auto scaled =
			offset_cubic_bezier(c.p0 * s, c.p1 * s, c.p2 * s, c.p3 * s, 5.0 * s);
		REQUIRE(scaled.size() == base.size());
		for (std::size_t k = 0; k < base.size(); ++k)
		{
			const Vec2 a[] = { base[k].p0, base[k].p1, base[k].p2, base[k].p3 };
			const Vec2 b[] = { scaled[k].p0, scaled[k].p1, scaled[k].p2, scaled[k].p3 };
			for (int j = 0; j < 4; ++j)
				CHECK((b[j] / s - a[j]).length() <= 1e-9 * 100.0);
		}
	}
}

TEST_CASE("an offset that folds is split where it folds")
{
	// The arch's tightest radius of curvature is 37.5 (at its top), so on its
	// inner side an offset of 50 folds twice: where 1 + d·(B'' × B')/|B'|³
	// crosses zero. The true offset has a cusp there; a piece ends at each.
	const CubicBezier c { { 0, 0 }, { 0, 100 }, { 100, 100 }, { 100, 0 } };
	const double d = -50.0;
	const auto fold = [&](double t) {
		const Vec2 d1 = cubic_bezier_derivative(c.p0, c.p1, c.p2, c.p3, t);
		const Vec2 a = c.p0 - 2.0 * c.p1 + c.p2;
		const Vec2 b = c.p1 - 2.0 * c.p2 + c.p3;
		const Vec2 d2 = 6.0 * ((1.0 - t) * a + t * b);
		return 1.0 + d * d2.cross(d1) / std::pow(d1.length(), 3);
	};
	REQUIRE(fold(0.0) > 0.0);
	REQUIRE(fold(0.5) < 0.0);
	const auto pieces = offset_cubic_bezier(c.p0, c.p1, c.p2, c.p3, d);
	for (const auto [lo0, hi0]: { std::pair { 0.0, 0.5 }, std::pair { 0.5, 1.0 } })
	{
		double lo = lo0;
		double hi = hi0;
		const bool positive = fold(lo) > 0.0;
		for (int k = 0; k < 60; ++k)
		{
			const double mid = (lo + hi) / 2.0;
			if ((fold(mid) > 0.0) == positive)
				lo = mid;
			else
				hi = mid;
		}
		const Vec2 cusp = true_offset(c, d, (lo + hi) / 2.0);
		CAPTURE(cusp.x);
		CAPTURE(cusp.y);
		const auto ends_at_cusp = [&](const CubicBezier& p) {
			return (p.p3 - cusp).length() < 1e-6 * 100.0;
		};
		const bool ends_there = std::any_of(pieces.begin(), pieces.end(), ends_at_cusp);
		CHECK(ends_there);
	}
}
