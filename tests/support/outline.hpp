#pragma once

// Test helpers: read a stroke outline back through the public API as a list of
// subpaths, with bounds and point lookups.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "stroke_outline/stroke_outline.hpp"

namespace test_support
{
	using namespace stroke_outline;

	struct Point
	{
		double x, y;
	};

	struct Subpath
	{
		std::vector<Point> points;  // every on-curve and control point, in order
		int cubics = 0;
		bool closed = false;

		double min_x() const { return std::ranges::min(points, {}, &Point::x).x; }
		double max_x() const { return std::ranges::max(points, {}, &Point::x).x; }
		double min_y() const { return std::ranges::min(points, {}, &Point::y).y; }
		double max_y() const { return std::ranges::max(points, {}, &Point::y).y; }
	};

	inline std::vector<Subpath> subpaths_of(const Path& path)
	{
		std::vector<Subpath> out;
		path.for_each(
			{
				.move_to = [&](double x, double y) { out.push_back({ { { x, y } } }); },
				.line_to = [&](double x, double y) { out.back().points.push_back({ x, y }); },
				.quad_to =
					[&](double cx, double cy, double x, double y) {
						out.back().points.push_back({ cx, cy });
						out.back().points.push_back({ x, y });
					},
				.cubic_to =
					[&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
						out.back().points.push_back({ c1x, c1y });
						out.back().points.push_back({ c2x, c2y });
						out.back().points.push_back({ x, y });
						++out.back().cubics;
					},
				.close = [&] { out.back().closed = true; },
			});
		return out;
	}

	inline bool contains_point(const Subpath& s, Point p, double eps = 1e-6)
	{
		return std::ranges::any_of(s.points, [&](const Point& q) {
			return std::abs(q.x - p.x) < eps && std::abs(q.y - p.y) < eps;
		});
	}

	inline Subpath stroke_single(const Path& path, const StrokeSettings& settings)
	{
		const auto subpaths = subpaths_of(build_outline(path, settings));
		REQUIRE(subpaths.size() == 1);
		return subpaths.front();
	}

	inline StrokeSettings width(double w, LineJoin join = LineJoin::Miter)
	{
		StrokeSettings s;
		s.line_width = w;
		s.line_join = join;
		return s;
	}
}
