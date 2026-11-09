#include "stroke/clip.hpp"
#include "stroke_outline/Vec2.hpp"
#include "bezier/bezier.hpp"
#include "bezier/intersect.hpp"
#include "math/geom.hpp"
#include "math/Eps.hpp"
#include "math/polynomial.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <numeric>
#include <optional>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// Curve-preserving resolution of a stroke outline (clip_to_shape).
//
// The raw outline of an over-wide stroke is not a set of simple loops: where
// the stroke is wider than the shape, offset edges turn inside out, cross
// themselves, overlap other subpaths' strokes, and spill past the shape. What
// we want instead is the boundary of the region F the stroke should fill:
//
//   F = points covered by the stroke, and inside the shape's fill (inside
//       alignment) / outside it (outside alignment) / anywhere (center).
//
// "Covered" is tested against the stroke's own pieces (a band per segment and
// a wedge per join), not the raw outline's winding: a fully inverted offset is
// a point reflection of the original, which keeps its orientation, so winding
// alone can't tell an inverted loop from a real boundary.
//
// We compute F's boundary without ever flattening a bézier:
//   1. Break the raw outline and the shape into Edges (line/quad/cubic).
//   2. Find every point where two edges meet — including an outline crossing
//      itself and an edge's own loop — and split edges there, so no piece
//      crosses another except at its ends.
//   3. Classify each piece by testing F just to its left and right. A piece is
//      on F's boundary iff F holds on exactly one side. Keep those, oriented
//      with F on their left so every loop winds +1 around F.
//   4. Chain the kept pieces end to end into closed contours.
// Curves are only ever *split* (never approximated), so the output is made of
// the same line and curve pieces as the inputs.
// ---------------------------------------------------------------------------

using namespace stroke_outline;
using namespace stroke_outline::bezier;


namespace
{
	// One boundary edge of a contour: a line, quadratic, or cubic from p0 to p3.
	// For a line only p0/p3 are used; a quad also uses control point p1; a cubic
	// uses both controls p1/p2. This uniform shape lets the walk treat all three
	// segment kinds the same way.
	struct Edge
	{
		enum class Kind { Line, Quadratic, Cubic } kind;
		Vec2 p0, p1, p2, p3;

		Vec2 end() const { return p3; }
	};

	// Stage 1: turn a command list (MoveTo + Line/Quadratic/CubicTo + Close) into a
	// flat list of Edges. `pen` tracks the current point so each edge knows its
	// start. Every subpath is closed — explicitly by Close, or implicitly at the
	// next MoveTo / the end, as a fill would — so the edges always bound a region.
	std::vector<Edge> contour_edges(const std::vector<PathCommand>& contour)
	{
		std::vector<Edge> edges;
		std::optional<Vec2> start;
		Vec2 pen {};
		const auto close = [&] {
			if (start && !points_are_too_close(pen, *start))
				edges.push_back({ Edge::Kind::Line, pen, {}, {}, *start });
			if (start)
				pen = *start;
		};
		for (const auto& c: contour)
		{
			if (std::holds_alternative<MoveTo>(c))
			{
				close();
				pen = std::get<MoveTo>(c).p;
				start = pen;
			}
			else if (std::holds_alternative<LineTo>(c))
			{
				const Vec2 e = std::get<LineTo>(c).p;
				edges.push_back({ Edge::Kind::Line, pen, {}, {}, e });
				pen = e;
			}
			else if (std::holds_alternative<QuadraticTo>(c))
			{
				const auto& q = std::get<QuadraticTo>(c);
				edges.push_back({ Edge::Kind::Quadratic, pen, q.control, {}, q.end });
				pen = q.end;
			}
			else if (std::holds_alternative<CubicTo>(c))
			{
				const auto& cu = std::get<CubicTo>(c);
				edges.push_back({ Edge::Kind::Cubic, pen, cu.control1, cu.control2, cu.end });
				pen = cu.end;
			}
			else if (std::holds_alternative<Close>(c))
				close();
		}
		close();
		return edges;
	}

	// Adapters to hand an Edge to the bezier/intersection routines, which take
	// explicit QuadraticBezier / CubicBezier values.
	QuadraticBezier as_quadratic(const Edge& e)
	{
		return { e.p0, e.p1, e.p3 };
	}
	CubicBezier as_cubic(const Edge& e)
	{
		return { e.p0, e.p1, e.p2, e.p3 };
	}

	// Point on an edge at parameter t in [0,1].
	Vec2 edge_point(const Edge& e, double t)
	{
		if (e.kind == Edge::Kind::Line)
			return Vec2::lerp(e.p0, e.p3, t);
		if (e.kind == Edge::Kind::Quadratic)
			return quadratic_bezier(e.p0, e.p1, e.p3, t);
		return cubic_bezier(e.p0, e.p1, e.p2, e.p3, t);
	}

	// Split an edge at ascending ts in (0,1), returning ts.size()+1 sub-edges
	// that together retrace the original edge exactly (this is where "preserve
	// curves" happens — we subdivide, never approximate).
	//
	// For curves there's a subtlety: de Casteljau's split_*_bezier splits the
	// *current remaining* piece at a local parameter in [0,1], but our ts are in
	// the original edge's parameter space. After chopping off [0, t_prev], the
	// original t maps to local = (t - t_prev) / (1 - t_prev) on what's left.
	std::vector<Edge> split_edge(const Edge& e, const std::vector<double>& ts)
	{
		std::vector<Edge> out;
		if (e.kind == Edge::Kind::Line)
		{
			// Lines split by simple interpolation, no reparametrization needed.
			Vec2 prev = e.p0;
			for (const double t: ts)
			{
				const Vec2 p = Vec2::lerp(e.p0, e.p3, t);
				out.push_back({ Edge::Kind::Line, prev, {}, {}, p });
				prev = p;
			}
			out.push_back({ Edge::Kind::Line, prev, {}, {}, e.p3 });
		}
		else if (e.kind == Edge::Kind::Quadratic)
		{
			QuadraticBezier cur = as_quadratic(e);
			double t_prev = 0.0;
			for (const double t: ts)
			{
				const double local = (t - t_prev) / (1.0 - t_prev);
				const auto [left, right] =
					split_quadratic_bezier(cur.p0, cur.p1, cur.p2, local);
				out.push_back({ Edge::Kind::Quadratic, left.p0, left.p1, {}, left.p2 });
				cur = right;
				t_prev = t;
			}
			out.push_back({ Edge::Kind::Quadratic, cur.p0, cur.p1, {}, cur.p2 });
		}
		else
		{
			CubicBezier cur = as_cubic(e);
			double t_prev = 0.0;
			for (const double t: ts)
			{
				const double local = (t - t_prev) / (1.0 - t_prev);
				const auto [left, right] =
					split_cubic_bezier(cur.p0, cur.p1, cur.p2, cur.p3, local);
				out.push_back({ Edge::Kind::Cubic, left.p0, left.p1, left.p2, left.p3 });
				cur = right;
				t_prev = t;
			}
			out.push_back({ Edge::Kind::Cubic, cur.p0, cur.p1, cur.p2, cur.p3 });
		}
		return out;
	}

	// Stage 2 helper: crossings of two edges, returning t1 on `a`, t2 on `b`,
	// and the point. This just dispatches to the right routine in the bezier
	// intersection library based on the two edge kinds.
	//
	// Those routines are asymmetric — e.g. line_cubic takes the line first and
	// reports (t_on_line, t_on_cubic). When our `a`/`b` order is the reverse of
	// what the routine expects, we call it in the order it wants and swap the two
	// parameters back so the result is always (t-on-a, t-on-b).
	std::vector<CurveIntersection> edge_crossings(const Edge& a, const Edge& b)
	{
		const auto swap_ts = [](std::vector<CurveIntersection> v) {
			for (auto& h: v)
				std::swap(h.t1, h.t2);
			return v;
		};
		const bool al = a.kind == Edge::Kind::Line;
		const bool bl = b.kind == Edge::Kind::Line;
		const bool aq = a.kind == Edge::Kind::Quadratic;
		const bool bq = b.kind == Edge::Kind::Quadratic;

		if (al && bl)
		{
			std::vector<CurveIntersection> r;
			if (const auto h = line_segment_intersection(a.p0, a.p3, b.p0, b.p3))
				r.push_back({ h->first, h->second, Vec2::lerp(a.p0, a.p3, h->first) });
			return r;
		}
		if (al)
			return bq ? line_quadratic_intersections(a.p0, a.p3, as_quadratic(b)) :
						line_cubic_intersections(a.p0, a.p3, as_cubic(b));
		if (bl)
			return swap_ts(
				aq ? line_quadratic_intersections(b.p0, b.p3, as_quadratic(a)) :
					 line_cubic_intersections(b.p0, b.p3, as_cubic(a)));
		if (aq && bq)
			return quadratic_quadratic_intersections(as_quadratic(a), as_quadratic(b));
		if (aq)
			return quadratic_cubic_intersections(as_quadratic(a), as_cubic(b));
		if (bq)
			return swap_ts(quadratic_cubic_intersections(as_quadratic(b), as_cubic(a)));
		return cubic_cubic_intersections(as_cubic(a), as_cubic(b));
	}

	// Direction of travel along an edge at parameter t (not normalized).
	Vec2 edge_tangent(const Edge& e, double t)
	{
		if (e.kind == Edge::Kind::Line)
			return e.p3 - e.p0;
		if (e.kind == Edge::Kind::Quadratic)
			return quadratic_bezier_derivative(e.p0, e.p1, e.p3, t);
		return cubic_bezier_derivative(e.p0, e.p1, e.p2, e.p3, t);
	}

	// The same edge traversed backward.
	Edge reversed(const Edge& e)
	{
		if (e.kind == Edge::Kind::Cubic)
			return { e.kind, e.p3, e.p2, e.p1, e.p0 };
		return { e.kind, e.p3, e.p1, {}, e.p0 };
	}

	// Parameters in (0,1), ascending, where the edge's y turns around — cutting
	// there leaves pieces along which y is monotone.
	Roots y_turning_points(const Edge& e)
	{
		Roots roots;
		if (e.kind == Edge::Kind::Quadratic)
			// y'(t) ∝ (p1 - p0) + t·(p0 - 2p1 + p2)
			roots = solve_linear(e.p0.y - 2.0 * e.p1.y + e.p3.y, e.p1.y - e.p0.y);
		else if (e.kind == Edge::Kind::Cubic)
		{
			// y'(t) ∝ (a - 2b + c)t² + 2(b - a)t + a, with a/b/c the control deltas.
			const double a = e.p1.y - e.p0.y;
			const double b = e.p2.y - e.p1.y;
			const double c = e.p3.y - e.p2.y;
			roots = solve_quadratic(a - 2.0 * b + c, 2.0 * (b - a), a);
		}
		Roots inside;
		for (const double t: roots)
			if (t > 0.0 && t < 1.0)
				inside.push(t);
		return inside;
	}

	// One edge's contribution to the winding number around p. A ray shot to the
	// right counts upward crossings +1 and downward ones -1, so a contour with
	// positive signed area (interior on its left) winds +1 around its interior.
	// Curves are cut into y-monotone pieces and every piece uses the half-open
	// rule (y_lo <= p.y < y_hi), so shared endpoints and y-extrema count once.
	int edge_winding(Vec2 p, const Edge& e)
	{
		int winding = 0;
		const auto count = [&](Vec2 a, Vec2 b, auto&& x_at_y) {
			if (a.y == b.y || p.y < std::min(a.y, b.y) || p.y >= std::max(a.y, b.y))
				return;
			if (x_at_y() > p.x)
				winding += a.y < b.y ? 1 : -1;
		};
		if (e.kind == Edge::Kind::Line)
		{
			count(e.p0, e.p3, [&] {
				return e.p0.x + (p.y - e.p0.y) * (e.p3.x - e.p0.x) / (e.p3.y - e.p0.y);
			});
			return winding;
		}
		// Cheap reject: the curve lies inside its control polygon's bounds.
		const double max_x = std::max({ e.p0.x, e.p1.x, e.p2.x, e.p3.x });
		const double min_y = std::min({ e.p0.y, e.p1.y, e.p2.y, e.p3.y });
		const double max_y = std::max({ e.p0.y, e.p1.y, e.p2.y, e.p3.y });
		if (max_x <= p.x || p.y < min_y || p.y > max_y)
			return 0;

		const Roots turns = y_turning_points(e);
		double ta = 0.0;
		Vec2 a = e.p0;
		for (std::size_t i = 0; i <= turns.count; ++i)
		{
			const double tb = i < turns.count ? turns.values[i] : 1.0;
			const Vec2 b = i < turns.count ? edge_point(e, tb) : e.p3;
			count(a, b, [&] {
				// Bisect for y(t) = p.y; y is monotone on [ta, tb].
				double lo = ta, hi = tb;
				if (a.y > b.y)
					std::swap(lo, hi);
				for (int k = 0; k < 64; ++k)
				{
					const double mid = 0.5 * (lo + hi);
					(edge_point(e, mid).y < p.y ? lo : hi) = mid;
				}
				return edge_point(e, 0.5 * (lo + hi)).x;
			});
			ta = tb;
			a = b;
		}
		return winding;
	}

	// Winding number of a contour's edges around p.
	int winding_number(Vec2 p, const std::vector<Edge>& edges)
	{
		int winding = 0;
		for (const auto& e: edges)
			winding += edge_winding(p, e);
		return winding;
	}

	struct Box
	{
		double min_x, min_y, max_x, max_y;

		bool touches(const Box& o) const
		{
			return min_x <= o.max_x && o.min_x <= max_x && min_y <= o.max_y && o.min_y <= max_y;
		}
		bool contains(Vec2 p) const
		{
			return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
		}
	};

	// Control-point bounds (a curve lies inside its control polygon), grown by
	// `margin`.
	Box edge_box(const Edge& e, double margin)
	{
		Box b { std::min(e.p0.x, e.p3.x), std::min(e.p0.y, e.p3.y),
				std::max(e.p0.x, e.p3.x), std::max(e.p0.y, e.p3.y) };
		const auto grow = [&](Vec2 q) {
			b.min_x = std::min(b.min_x, q.x), b.max_x = std::max(b.max_x, q.x);
			b.min_y = std::min(b.min_y, q.y), b.max_y = std::max(b.max_y, q.y);
		};
		if (e.kind != Edge::Kind::Line)
			grow(e.p1);
		if (e.kind == Edge::Kind::Cubic)
			grow(e.p2);
		return { b.min_x - margin, b.min_y - margin, b.max_x + margin, b.max_y + margin };
	}

	// Turn a walked list of arcs back into path commands (one closed subpath).
	void emit_contour(const std::vector<Edge>& arcs, std::vector<PathCommand>& out)
	{
		if (arcs.empty())
			return;
		out.push_back(MoveTo { arcs.front().p0 });
		for (const auto& a: arcs)
			if (a.kind == Edge::Kind::Line)
				out.push_back(LineTo { a.p3 });
			else if (a.kind == Edge::Kind::Quadratic)
				out.push_back(QuadraticTo { a.p1, a.p3 });
			else
				out.push_back(CubicTo { a.p1, a.p2, a.p3 });
		out.push_back(Close {});
	}
}


namespace stroke_outline
{
	bool path_fill_contains(Vec2 p, const std::vector<PathCommand>& path, FillRule rule)
	{
		const int winding = winding_number(p, contour_edges(path));
		return rule == FillRule::NonZero ? winding != 0 : (winding & 1) != 0;
	}

	std::vector<PathCommand> resolve_stroke_outline(
		const std::vector<PathCommand>& outline,
		const std::vector<std::vector<PathCommand>>& coverage,
		const std::vector<PathCommand>& shape,
		FillRule rule,
		StrokeAlign align)
	{
		// Stage 1: candidate boundary edges — the raw outline first, then the
		// shape (where the stroke is clipped by the shape, F's boundary runs along
		// it). Coincident pieces are de-duplicated later in favour of the outline.
		const auto outline_edges = contour_edges(outline);
		const auto shape_edges = contour_edges(shape);
		if (outline_edges.empty())
			return outline;

		std::vector<Edge> edges = outline_edges;
		if (align != StrokeAlign::Center)
			edges.insert(edges.end(), shape_edges.begin(), shape_edges.end());

		// Tolerances scale with the geometry so large coordinates behave like
		// small ones. `tol` merges nearly coincident points; `probe` is how far
		// off a piece we sample to classify its two sides.
		double min_x = edges[0].p0.x, max_x = min_x, min_y = edges[0].p0.y, max_y = min_y;
		for (const auto& e: edges)
		{
			min_x = std::min(min_x, e.p3.x), max_x = std::max(max_x, e.p3.x);
			min_y = std::min(min_y, e.p3.y), max_y = std::max(max_y, e.p3.y);
		}
		// Relative to the geometry's size, never inside floating-point noise. (There
		// used to be a fixed 1e-4 floor, which swamped shapes about that size.)
		const double extent = std::hypot(max_x - min_x, max_y - min_y);
		const double magnitude =
			std::max({ std::abs(min_x), std::abs(max_x), std::abs(min_y), std::abs(max_y) });
		const double tol = std::max(extent * EPSILON.relative, magnitude * EPSILON.noise);
		const double probe = tol * 0.1;
		const auto near = [&](Vec2 a, Vec2 b, double eps) {
			return (a - b).length_squared() <= eps * eps;
		};

		// Stage 2: every point where two edges meet, recorded as a split on each
		// edge whose interior it lies on. A hit at an edge's own endpoint needs no
		// split there, but the *other* edge may still need one (a vertex landing
		// on another edge's interior).
		struct Split
		{
			double t;
			Vec2 pt;
		};
		std::vector<std::vector<Split>> splits(edges.size());
		const auto add_split = [&](std::size_t i, double t, Vec2 pt) {
			if (t <= 0.0 || t >= 1.0 || near(pt, edges[i].p0, tol) || near(pt, edges[i].p3, tol))
				return;
			splits[i].push_back({ t, pt });
		};
		// Broad phase: only edges whose boxes (grown by `tol`) touch can meet.
		// Sort by left edge and sweep: each edge is paired only with the edges
		// that start before it ends in x, then checked in y.
		std::vector<Box> boxes;
		boxes.reserve(edges.size());
		for (const auto& e: edges)
			boxes.push_back(edge_box(e, tol));
		std::vector<int> by_x(edges.size());
		std::iota(by_x.begin(), by_x.end(), 0);
		std::sort(by_x.begin(), by_x.end(), [&](int a, int b) {
			return boxes[a].min_x < boxes[b].min_x;
		});
		std::vector<std::pair<int, int>> candidates;
		for (std::size_t i = 0; i < by_x.size(); ++i)
			for (std::size_t j = i + 1;
				 j < by_x.size() && boxes[by_x[j]].min_x <= boxes[by_x[i]].max_x;
				 ++j)
				if (boxes[by_x[i]].touches(boxes[by_x[j]]))
					candidates.emplace_back(by_x[i], by_x[j]);
		for (const auto [a, b]: candidates)
			for (const auto& h: edge_crossings(edges[a], edges[b]))
			{
				add_split(a, h.t1, h.point);
				add_split(b, h.t2, h.point);
			}
		for (std::size_t a = 0; a < edges.size(); ++a)
			if (edges[a].kind == Edge::Kind::Cubic)
				if (const auto h = cubic_self_intersection(as_cubic(edges[a])))
				{
					add_split(a, h->t1, h->point);
					add_split(a, h->t2, h->point);
				}
		// line_segment_intersection ignores parallel segments, so collinear
		// overlaps are found separately: an endpoint of one edge lying on a
		// line's interior splits that line.
		const auto split_line_at_endpoints_of = [&](int line_index, int other) {
			const Edge& line = edges[line_index];
			if (line.kind != Edge::Kind::Line)
				return;
			const Vec2 d = line.p3 - line.p0;
			const double len_sq = d.length_squared();
			if (len_sq <= tol * tol)
				return;
			for (const Vec2 q: { edges[other].p0, edges[other].p3 })
			{
				const double t = (q - line.p0).dot(d) / len_sq;
				if (near(line.p0 + d * t, q, tol))
					add_split(line_index, t, q);
			}
		};
		for (const auto [a, b]: candidates)
		{
			split_line_at_endpoints_of(a, b);
			split_line_at_endpoints_of(b, a);
		}

		// Split every edge at its (sorted, de-duplicated) points. Sub-edge ends
		// are snapped to the shared intersection point so pieces from different
		// edges meet exactly; pieces shorter than `tol` are dropped.
		struct Piece
		{
			Edge edge;
			bool from_outline;
		};
		std::vector<Piece> pieces;
		bool any_split = false;
		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			auto& s = splits[i];
			std::sort(s.begin(), s.end(), [](const Split& a, const Split& b) {
				return a.t < b.t;
			});
			std::vector<Split> unique;
			for (const auto& sp: s)
				if (unique.empty() || !near(sp.pt, unique.back().pt, tol))
					unique.push_back(sp);
			any_split = any_split || !unique.empty();

			std::vector<double> ts;
			for (const auto& sp: unique)
				ts.push_back(sp.t);
			auto subs = split_edge(edges[i], ts);
			for (std::size_t k = 0; k < unique.size(); ++k)
			{
				subs[k].p3 = unique[k].pt;
				subs[k + 1].p0 = unique[k].pt;
			}
			const bool from_outline = i < outline_edges.size();
			for (const auto& sub: subs)
			{
				const bool tiny = near(sub.p0, sub.p3, tol)
								  && (sub.kind == Edge::Kind::Line
									  || (near(sub.p0, sub.p1, tol)
										  && (sub.kind == Edge::Kind::Quadratic
											  || near(sub.p0, sub.p2, tol))));
				if (!tiny)
					pieces.push_back({ sub, from_outline });
			}
		}

		// Stage 3: keep the pieces with F on exactly one side, oriented with F on
		// their left (so each loop winds +1 around F).
		// A point is covered when any coverage piece winds around it; each
		// piece's bounds reject it cheaply first.
		std::vector<std::vector<Edge>> cover;
		std::vector<Box> cover_boxes;
		for (const auto& contour: coverage)
		{
			auto piece = contour_edges(contour);
			if (piece.empty())
				continue;
			Box box = edge_box(piece.front(), 0.0);
			for (const auto& e: piece)
			{
				const Box b = edge_box(e, 0.0);
				box = { std::min(box.min_x, b.min_x), std::min(box.min_y, b.min_y),
						std::max(box.max_x, b.max_x), std::max(box.max_y, b.max_y) };
			}
			cover.push_back(std::move(piece));
			cover_boxes.push_back(box);
		}

		// Probe points just left and right of every piece's midpoint. They are
		// classified in one batch: covered (a sweep in x against the coverage
		// boxes) and inside the shape (a sweep in y — a winding test's horizontal
		// ray only meets edges spanning the point's y).
		struct Probe
		{
			Vec2 left, right;
			bool valid = false;
		};
		std::vector<Probe> probes(pieces.size());
		std::vector<Vec2> points;
		points.reserve(pieces.size() * 2);
		for (std::size_t k = 0; k < pieces.size(); ++k)
		{
			const Edge& e = pieces[k].edge;
			const auto dir = edge_tangent(e, 0.5).normalize();
			const auto chord = (e.p3 - e.p0).normalize();
			if (!dir && !chord)
				continue;
			const Vec2 t = dir ? *dir : *chord;
			const Vec2 left { -t.y, t.x };
			const Vec2 mid = edge_point(e, 0.5);
			probes[k] = { mid + left * probe, mid - left * probe, true };
			points.push_back(probes[k].left);
			points.push_back(probes[k].right);
		}

		// Visit `points` in order of `coord`, with `items` (sorted by their low
		// bound) entering an active list as the sweep reaches them and leaving
		// once it passes their high bound.
		const auto sweep = [&](auto coord, auto low, auto high, std::size_t item_count, auto visit) {
			std::vector<int> point_order(points.size()), item_order(item_count), active;
			std::iota(point_order.begin(), point_order.end(), 0);
			std::iota(item_order.begin(), item_order.end(), 0);
			std::sort(point_order.begin(), point_order.end(), [&](int a, int b) {
				return coord(points[a]) < coord(points[b]);
			});
			std::sort(item_order.begin(), item_order.end(), [&](int a, int b) {
				return low(a) < low(b);
			});
			std::size_t next = 0;
			for (const int pi: point_order)
			{
				const double c = coord(points[pi]);
				while (next < item_order.size() && low(item_order[next]) <= c)
					active.push_back(item_order[next++]);
				std::erase_if(active, [&](int item) { return high(item) < c; });
				visit(pi, active);
			}
		};

		std::vector<char> is_covered(points.size(), 0);
		sweep(
			[](Vec2 p) { return p.x; },
			[&](int k) { return cover_boxes[k].min_x; },
			[&](int k) { return cover_boxes[k].max_x; },
			cover.size(),
			[&](int pi, const std::vector<int>& active) {
				for (const int k: active)
					if (cover_boxes[k].contains(points[pi]) && winding_number(points[pi], cover[k]) != 0)
					{
						is_covered[pi] = 1;
						return;
					}
			});

		std::vector<char> is_in_shape(points.size(), 0);
		if (align != StrokeAlign::Center)
		{
			std::vector<Box> spans;
			spans.reserve(shape_edges.size());
			for (const auto& e: shape_edges)
				spans.push_back(edge_box(e, tol));
			sweep(
				[](Vec2 p) { return p.y; },
				[&](int k) { return spans[k].min_y; },
				[&](int k) { return spans[k].max_y; },
				shape_edges.size(),
				[&](int pi, const std::vector<int>& active) {
					int winding = 0;
					for (const int k: active)
						winding += edge_winding(points[pi], shape_edges[k]);
					is_in_shape[pi] =
						rule == FillRule::NonZero ? winding != 0 : (winding & 1) != 0;
				});
		}
		std::size_t point_index = 0;
		const auto next_in_region = [&] {
			const std::size_t pi = point_index++;
			if (!is_covered[pi])
				return false;
			if (align == StrokeAlign::Inside)
				return is_in_shape[pi] != 0;
			if (align == StrokeAlign::Outside)
				return is_in_shape[pi] == 0;
			return true;
		};

		// Kept pieces hashed by their (quantized) start point, for the duplicate
		// check here and for chaining below; `cell` is no smaller than either
		// tolerance, so near points land in the same or a neighbouring cell.
		const double cell = tol * 4.0;
		const auto cell_key = [&](Vec2 p) {
			const auto q = [&](double v, double origin) {
				return static_cast<std::uint64_t>(
					static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor((v - origin) / cell))));
			};
			return (q(p.x, min_x) << 32) | q(p.y, min_y);
		};
		const auto neighbour_keys = [&](Vec2 p) {
			std::array<std::uint64_t, 9> keys {};
			int n = 0;
			for (int dx = -1; dx <= 1; ++dx)
				for (int dy = -1; dy <= 1; ++dy)
					keys[n++] = cell_key({ p.x + dx * cell, p.y + dy * cell });
			return keys;
		};
		std::unordered_map<std::uint64_t, std::vector<int>> kept_by_start;

		std::vector<Edge> kept;
		// For the fast path: did every outline piece survive, all flipped the same
		// way, with nothing else kept?
		bool outline_as_is = true;
		std::optional<bool> outline_flip;
		for (std::size_t piece_index = 0; piece_index < pieces.size(); ++piece_index)
		{
			const Piece& piece = pieces[piece_index];
			const Edge& e = piece.edge;
			if (!probes[piece_index].valid)
				continue;
			const bool in_left = next_in_region();
			if (in_left == next_in_region())
			{
				outline_as_is = outline_as_is && !piece.from_outline;
				continue;
			}

			const Edge oriented = in_left ? e : reversed(e);
			// Coincident pieces (e.g. an inside stroke's outer edge lying on the
			// shape) classify identically; keep just the first, which comes from
			// the outline.
			const Vec2 kmid = edge_point(oriented, 0.5);
			bool duplicate = false;
			for (const auto key: neighbour_keys(oriented.p0))
				if (const auto it = kept_by_start.find(key); it != kept_by_start.end())
					for (const int k: it->second)
						duplicate = duplicate
									|| (near(kept[k].p0, oriented.p0, tol)
										&& near(kept[k].p3, oriented.p3, tol)
										&& near(edge_point(kept[k], 0.5), kmid, tol));
			if (duplicate)
				continue;
			if (piece.from_outline)
			{
				if (!outline_flip)
					outline_flip = !in_left;
				outline_as_is = outline_as_is && *outline_flip == !in_left;
			}
			else
				outline_as_is = false;
			kept_by_start[cell_key(oriented.p0)].push_back(static_cast<int>(kept.size()));
			kept.push_back(oriented);
		}

		// Fast path for the common case: nothing crossed and the outline already
		// bounds F exactly — return it verbatim.
		if (!any_split && outline_as_is)
			return outline;

		// Stage 4: chain kept pieces end to end into closed contours. Every vertex
		// of a region boundary has as many pieces leaving as arriving, so a greedy
		// walk from any unused piece can only end back at its start.
		const double chain_tol = tol * 4.0;
		std::vector<PathCommand> result;
		std::vector<bool> used(kept.size(), false);
		for (std::size_t start = 0; start < kept.size(); ++start)
		{
			if (used[start])
				continue;
			used[start] = true;
			std::vector<Edge> loop { kept[start] };
			while (!near(loop.back().p3, loop.front().p0, chain_tol))
			{
				std::size_t next = kept.size();
				double best = chain_tol * chain_tol;
				for (const auto key: neighbour_keys(loop.back().p3))
					if (const auto it = kept_by_start.find(key); it != kept_by_start.end())
						for (const int k: it->second)
						{
							const double d = (kept[k].p0 - loop.back().p3).length_squared();
							if (!used[k] && d <= best)
							{
								best = d;
								next = static_cast<std::size_t>(k);
							}
						}
				if (next == kept.size())
					break;  // shouldn't happen; Close bridges any leftover gap
				used[next] = true;
				loop.push_back(kept[next]);
			}
			emit_contour(loop, result);
		}
		return result;
	}
}
