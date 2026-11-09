#include "stroke/clip_weiler_atherton.hpp"
#include "stroke_outline/Vec2.hpp"
#include "bezier/bezier.hpp"
#include "bezier/intersect.hpp"
#include "math/geom.hpp"
#include "math/Eps.hpp"
#include <variant>
#include <vector>
#include <optional>
#include <algorithm>

// ---------------------------------------------------------------------------
// ORIGINAL clip_to_shape implementation, kept for reference — not used by the
// stroker. See clip_weiler_atherton.hpp for why it was replaced by
// resolve_stroke_outline (clip.cpp).
//
// Curve-preserving polygon clipping: subject ∩ region.
//
// This computes the part of `subject` that lies inside `region` using the
// Weiler-Atherton algorithm, without ever flattening a bézier — arcs are only
// ever *split* at intersection points, so the output is made of the same line
// and curve pieces as the inputs.
//
// The idea in one picture. Two closed contours overlap. Every place their
// boundaries cross is a "crossing". Starting at a crossing where the subject
// is *entering* the region, we walk along the subject boundary until we hit
// the next crossing; there we hop onto the region boundary and follow it until
// the next crossing; hop back to the subject; and so on. Because we always
// turn onto whichever boundary keeps us on the inside, the loop we trace out is
// exactly one piece of the intersection. Repeating from every unused entering
// crossing yields all the pieces.
//
// The stages, mirrored in clip_contour() below:
//   1. Break both contours into a flat list of Edges (line/quad/cubic).
//   2. Find every crossing between a subject edge and a region edge.
//   3. Split the edges at those crossings and build an "augmented ring" for
//      each contour: its original vertices plus the crossings, in order.
//   4. Link each crossing to its twin on the other ring and mark whether the
//      subject is entering or leaving the region there.
//   5. Walk the rings as described above, emitting each traced loop.
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
	// start; a Close adds the implicit segment back to the subpath start (unless
	// the contour already ends there).
	std::vector<Edge> contour_edges(const std::vector<PathCommand>& contour)
	{
		std::vector<Edge> edges;
		std::optional<Vec2> start;
		Vec2 pen {};
		for (const auto& c: contour)
		{
			if (std::holds_alternative<MoveTo>(c))
			{
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
			else if (
				std::holds_alternative<Close>(c) && start && !points_are_too_close(pen, *start))
			{
				edges.push_back({ Edge::Kind::Line, pen, {}, {}, *start });
				pen = *start;
			}
		}
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

	// Is point p inside the region? Even-odd rule via a horizontal ray shot to
	// the right: count how many times the region boundary crosses it — odd means
	// inside. Curve-aware (uses the real curve/line intersections, no flattening).
	// Endpoint hits (t2 at 0 or 1) are excluded so a ray grazing a shared vertex
	// isn't counted twice.
	bool point_in_region(Vec2 p, const std::vector<Edge>& region)
	{
		const Vec2 far { p.x + 1e7, p.y };
		int crossings = 0;
		for (const auto& e: region)
		{
			if (e.kind == Edge::Kind::Line)
			{
				if (line_segment_intersection(p, far, e.p0, e.p3))
					++crossings;
			}
			else
			{
				const auto hits =
					e.kind == Edge::Kind::Quadratic ?
						line_quadratic_intersections(p, far, as_quadratic(e)) :
						line_cubic_intersections(p, far, as_cubic(e));
				for (const auto& h: hits)
					if (h.t2 > EPSILON.param && h.t2 < 1.0 - EPSILON.param)
						++crossings;
			}
		}
		return (crossings % 2) == 1;
	}

	// A vertex of an "augmented ring": one original contour with the crossings
	// inserted as extra vertices, in boundary order. Each node stores the arc
	// that leaves it toward the next node, so walking = following arcs.
	struct Node
	{
		Vec2 pt;
		Edge arc;               // arc from this node to the next node
		bool crossing = false;  // true if this node is an intersection point
		int cross_id = -1;      // index into the shared crossing list (if crossing)
		bool entering = false;  // subject side: does the boundary enter region here?
		int partner = -1;       // index of the twin node on the other ring
		bool used = false;      // consumed by the walk already
	};

	// Stage 3: build one augmented ring. For each edge, split it at that edge's
	// crossing parameters, then emit: a node at the edge's own start (arc = first
	// sub-piece), followed by one node per crossing (arc = the sub-piece leaving
	// that crossing). The nodes come out in boundary order, ready to walk.
	std::vector<Node> build_ring(
		const std::vector<Edge>& edges,
		const std::vector<std::vector<std::pair<double, int>>>& per_edge)
	{
		std::vector<Node> ring;
		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			std::vector<double> ts;
			for (const auto& [t, id]: per_edge[i])
				ts.push_back(t);
			const auto subs = split_edge(edges[i], ts);
			// Original vertex: not a crossing; its arc is the piece before the
			// first crossing on this edge (or the whole edge if there are none).
			ring.push_back({ edges[i].p0, subs[0], false, -1, false, -1, false });
			// One crossing node per intersection on this edge, arc = next piece.
			for (std::size_t j = 0; j < per_edge[i].size(); ++j)
				ring.push_back(
					{ edge_point(edges[i], per_edge[i][j].first),
					  subs[j + 1],
					  true,
					  per_edge[i][j].second,
					  false,
					  -1,
					  false });
		}
		return ring;
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
	std::vector<PathCommand> clip_contour_weiler_atherton(
		const std::vector<PathCommand>& subject,
		const std::vector<PathCommand>& region)
	{
		// Stage 1: flatten both contours into edge lists.
		const auto s_edges = contour_edges(subject);
		const auto r_edges = contour_edges(region);
		if (s_edges.empty() || r_edges.empty())
			return subject;

		// Stage 2: every subject-edge × region-edge crossing. `se`/`re` are the
		// edge indices, `ts`/`tr` the parameters on each. Crossings at an exact
		// endpoint (t ≈ 0 or 1) are dropped — a boundary that merely touches at a
		// shared vertex isn't a true entry/exit and would confuse the walk.
		struct Cross
		{
			int se, re;
			double ts, tr;
			Vec2 pt;
		};
		std::vector<Cross> crosses;
		for (std::size_t i = 0; i < s_edges.size(); ++i)
			for (std::size_t j = 0; j < r_edges.size(); ++j)
				for (const auto& h: edge_crossings(s_edges[i], r_edges[j]))
					if (h.t1 > EPSILON.param && h.t1 < 1.0 - EPSILON.param
						&& h.t2 > EPSILON.param && h.t2 < 1.0 - EPSILON.param)
						crosses.push_back(
							{ static_cast<int>(i), static_cast<int>(j), h.t1, h.t2, h.point });

		// No crossings means the two boundaries don't interleave, so the subject
		// is either wholly inside the region (keep it unchanged — this is also the
		// fast no-op path for a normal stroke whose inner edge never escapes) or
		// wholly outside (nothing survives the intersection).
		if (crosses.empty())
			return point_in_region(s_edges.front().p0, r_edges) ?
					   subject :
					   std::vector<PathCommand> {};

		// Stage 3: bucket crossings per edge, sort each bucket by parameter so the
		// split points are in boundary order, then build the augmented rings.
		std::vector<std::vector<std::pair<double, int>>> s_per(s_edges.size());
		std::vector<std::vector<std::pair<double, int>>> r_per(r_edges.size());
		for (int id = 0; id < static_cast<int>(crosses.size()); ++id)
		{
			s_per[crosses[id].se].push_back({ crosses[id].ts, id });
			r_per[crosses[id].re].push_back({ crosses[id].tr, id });
		}
		const auto by_t = [](const auto& a, const auto& b) { return a.first < b.first; };
		for (auto& v: s_per)
			std::sort(v.begin(), v.end(), by_t);
		for (auto& v: r_per)
			std::sort(v.begin(), v.end(), by_t);

		auto s_ring = build_ring(s_edges, s_per);
		auto r_ring = build_ring(r_edges, r_per);

		// Stage 4: link each crossing to its twin (same point, other ring) and,
		// on the subject side, decide whether the boundary is *entering* the
		// region there. We test the midpoint of the arc leaving the crossing: if
		// that arc runs into the region, this crossing is an entry point. Those
		// entries are where a valid intersection loop can begin.
		std::vector<int> id_to_r(crosses.size(), -1);
		for (std::size_t k = 0; k < r_ring.size(); ++k)
			if (r_ring[k].crossing)
				id_to_r[r_ring[k].cross_id] = static_cast<int>(k);
		for (std::size_t k = 0; k < s_ring.size(); ++k)
			if (s_ring[k].crossing)
			{
				const int r = id_to_r[s_ring[k].cross_id];
				s_ring[k].partner = r;
				r_ring[r].partner = static_cast<int>(k);
				s_ring[k].entering = point_in_region(edge_point(s_ring[k].arc, 0.5), r_edges);
			}

		// Stage 5: the Weiler-Atherton walk. Begin at each unused entering
		// crossing and trace a loop: follow the current ring's arcs up to the next
		// crossing, then hop to that crossing's twin on the other ring and switch
		// which ring we're following. Since we always turn onto the boundary that
		// keeps us inside the intersection, the loop closes on a piece of
		// subject ∩ region. Marking nodes `used` stops us re-tracing a loop, so
		// each entering crossing seeds at most one output contour.
		std::vector<PathCommand> result;
		for (std::size_t start = 0; start < s_ring.size(); ++start)
		{
			if (!s_ring[start].crossing || !s_ring[start].entering || s_ring[start].used)
				continue;

			std::vector<Edge> arcs;
			int idx = static_cast<int>(start);
			bool on_subject = true;
			// Safety cap: a well-formed walk can't visit more than every node on
			// both rings, so this bounds the loop against degenerate inputs.
			std::size_t guard = 0;
			const std::size_t cap = (s_ring.size() + r_ring.size()) * 2 + 4;
			while (guard++ < cap)
			{
				auto& ring = on_subject ? s_ring : r_ring;
				// Mark the crossing we're standing on (and its twin) consumed.
				ring[idx].used = true;
				if (ring[idx].partner >= 0)
					(on_subject ? r_ring : s_ring)[ring[idx].partner].used = true;

				// Collect arcs along this ring until the next crossing.
				int adv = idx;
				do
				{
					arcs.push_back(ring[adv].arc);
					adv = (adv + 1) % static_cast<int>(ring.size());
				}
				while (!ring[adv].crossing);

				// Hop to the twin of that crossing on the other ring and continue.
				const int partner = ring[adv].partner;
				ring[adv].used = true;
				on_subject = !on_subject;
				idx = partner;
				// Back to where this loop began (on the subject ring) → close it.
				if (on_subject && idx == static_cast<int>(start))
					break;
			}
			emit_contour(arcs, result);
		}
		return result;
	}
}
