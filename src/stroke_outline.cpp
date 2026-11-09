#include <algorithm>
#include <initializer_list>
#include <vector>
#include <optional>
#include <variant>
#include <cmath>
#include <tuple>
#include <utility>
#include <cstddef>
#include "core.hpp"
#include "stroke_outline/Vec2.hpp"
#include "stroke_outline/PathCommand.hpp"
#include "bezier/bezier.hpp"
#include "bezier/intersect.hpp"
#include "math/geom.hpp"
#include "stroke/cap.hpp"
#include "stroke/clip.hpp"
#include "stroke/join.hpp"
#include "stroke/dash.hpp"
#include "stroke/utils.hpp"

using namespace stroke_outline;

namespace
{
	struct Segment
	{
		Vec2 start;
		PathCommand command;
	};

	struct SegmentOffset
	{
		std::vector<PathCommand> left;
		std::vector<PathCommand> right;
		Vec2 start;
		Vec2 end;
		Vec2 start_tangent;
		Vec2 end_tangent;
		Vec2 left_start;
		Vec2 right_start;
		Vec2 left_end;
		Vec2 right_end;
	};

	std::optional<SegmentOffset>
		build_line_offset(Vec2 start, Vec2 end, double left_dist, double right_dist)
	{
		const Vec2 tangent = end - start;
		const auto tangent_normalized = tangent.normalize();
		if (!tangent_normalized)
			return std::nullopt;
		const Vec2 normal = perpendicular(*tangent_normalized);
		const Vec2 left_start = start + normal * left_dist;
		const Vec2 right_start = start + normal * right_dist;
		const Vec2 left_end = end + normal * left_dist;
		const Vec2 right_end = end + normal * right_dist;
		return SegmentOffset {
			.left = { LineTo { left_end } },
			.right = { LineTo { right_end } },
			.start = start,
			.end = end,
			.start_tangent = *tangent_normalized,
			.end_tangent = *tangent_normalized,
			.left_start = left_start,
			.right_start = right_start,
			.left_end = left_end,
			.right_end = right_end
		};
	}

	std::optional<Vec2> normalize_first_non_zero(const std::vector<Vec2>& candidates)
	{
		for (const Vec2& candidate: candidates)
			if (auto normalized = candidate.normalize())
				return normalized;
		return std::nullopt;
	}

	std::optional<Vec2>
		resolve_quadratic_endpoint_tangent(Vec2 p0, Vec2 p1, Vec2 p2, bool is_start)
	{
		const Vec2 derivative =
			bezier::quadratic_bezier_derivative(p0, p1, p2, is_start ? 0.0 : 1.0);
		if (auto normalized = derivative.normalize())
			return normalized;
		const std::vector<Vec2> fallback =
			is_start ? std::vector<Vec2> { p1 - p0, p2 - p0 } :
					   std::vector<Vec2> { p2 - p1, p2 - p0 };
		return normalize_first_non_zero(fallback);
	}

	std::optional<Vec2>
		resolve_cubic_endpoint_tangent(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, bool is_start)
	{
		const Vec2 derivative =
			bezier::cubic_bezier_derivative(p0, p1, p2, p3, is_start ? 0.0 : 1.0);
		if (auto normalized = derivative.normalize())
			return normalized;
		const std::vector<Vec2> fallback =
			is_start ? std::vector<Vec2> { p1 - p0, p2 - p0, p3 - p0 } :
					   std::vector<Vec2> { p3 - p2, p3 - p1, p3 - p0 };
		return normalize_first_non_zero(fallback);
	}

	std::tuple<Vec2, Vec2, std::vector<PathCommand>> extract_quadratic_offset_data(
		const std::vector<bezier::QuadraticBezier>& offset_curves,
		Vec2 fallback_start,
		Vec2 fallback_end)
	{
		const Vec2 start = offset_curves.empty() ? fallback_start : offset_curves.front().p0;
		const Vec2 end = offset_curves.empty() ? fallback_end : offset_curves.back().p2;
		std::vector<PathCommand> commands;
		for (const auto& curve: offset_curves)
			commands.push_back(QuadraticTo { curve.p1, curve.p2 });
		return { start, end, std::move(commands) };
	}

	std::tuple<Vec2, Vec2, std::vector<PathCommand>> extract_cubic_offset_data(
		const std::vector<bezier::CubicBezier>& offset_curves,
		Vec2 fallback_start,
		Vec2 fallback_end)
	{
		const Vec2 start = offset_curves.empty() ? fallback_start : offset_curves.front().p0;
		const Vec2 end = offset_curves.empty() ? fallback_end : offset_curves.back().p3;
		std::vector<PathCommand> commands;
		for (const auto& curve: offset_curves)
			commands.push_back(CubicTo { curve.p1, curve.p2, curve.p3 });
		return { start, end, std::move(commands) };
	}

	struct SideGeometry
	{
		std::vector<PathCommand> commands;
		Vec2 start;
		Vec2 end;
	};

	SideGeometry quadratic_side_geometry(Vec2 p0, Vec2 control, Vec2 p2, double dist)
	{
		if (dist == 0.0)
			return { { QuadraticTo { control, p2 } }, p0, p2 };
		auto [start, end, commands] = extract_quadratic_offset_data(
			bezier::offset_quadratic_bezier(p0, control, p2, dist),
			p0,
			p2);
		return { std::move(commands), start, end };
	}

	SideGeometry
		cubic_side_geometry(Vec2 p0, Vec2 control1, Vec2 control2, Vec2 p3, double dist)
	{
		if (dist == 0.0)
			return { { CubicTo { control1, control2, p3 } }, p0, p3 };
		auto [start, end, commands] = extract_cubic_offset_data(
			bezier::offset_cubic_bezier(p0, control1, control2, p3, dist),
			p0,
			p3);
		return { std::move(commands), start, end };
	}

	std::optional<SegmentOffset>
		offset_segment(const Segment& segment, double left_dist, double right_dist)
	{
		return std::visit(
			[&](auto&& cmd) -> std::optional<SegmentOffset> {
				using T = std::decay_t<decltype(cmd)>;
				if constexpr (std::is_same_v<T, LineTo>)
					return build_line_offset(segment.start, cmd.p, left_dist, right_dist);
				else if constexpr (std::is_same_v<T, QuadraticTo>)
				{
					const auto start_tangent = resolve_quadratic_endpoint_tangent(
						segment.start,
						cmd.control,
						cmd.end,
						true);
					if (!start_tangent)
						return std::nullopt;
					const auto end_tangent = resolve_quadratic_endpoint_tangent(
						segment.start,
						cmd.control,
						cmd.end,
						false);
					if (!end_tangent)
						return std::nullopt;
					auto left =
						quadratic_side_geometry(segment.start, cmd.control, cmd.end, left_dist);
					auto right = quadratic_side_geometry(
						segment.start,
						cmd.control,
						cmd.end,
						right_dist);
					return SegmentOffset {
						.left = std::move(left.commands),
						.right = std::move(right.commands),
						.start = segment.start,
						.end = cmd.end,
						.start_tangent = *start_tangent,
						.end_tangent = *end_tangent,
						.left_start = left.start,
						.right_start = right.start,
						.left_end = left.end,
						.right_end = right.end
					};
				}
				else if constexpr (std::is_same_v<T, CubicTo>)
				{
					const auto start_tangent = resolve_cubic_endpoint_tangent(
						segment.start,
						cmd.control1,
						cmd.control2,
						cmd.end,
						true);
					if (!start_tangent)
						return std::nullopt;
					const auto end_tangent = resolve_cubic_endpoint_tangent(
						segment.start,
						cmd.control1,
						cmd.control2,
						cmd.end,
						false);
					if (!end_tangent)
						return std::nullopt;
					auto left = cubic_side_geometry(
						segment.start,
						cmd.control1,
						cmd.control2,
						cmd.end,
						left_dist);
					auto right = cubic_side_geometry(
						segment.start,
						cmd.control1,
						cmd.control2,
						cmd.end,
						right_dist);
					return SegmentOffset {
						.left = std::move(left.commands),
						.right = std::move(right.commands),
						.start = segment.start,
						.end = cmd.end,
						.start_tangent = *start_tangent,
						.end_tangent = *end_tangent,
						.left_start = left.start,
						.right_start = right.start,
						.left_end = left.end,
						.right_end = right.end
					};
				}
				else
					return std::nullopt;
			},
			segment.command);
	}

	std::vector<PathCommand>
		reverse_commands(const std::vector<PathCommand>& commands, Vec2 start_point)
	{
		if (commands.empty())
			return {};

		std::vector<Vec2> points = { start_point };
		for (const auto& cmd: commands)
		{
			const auto end = end_point(cmd);
			if (end)
				points.push_back(*end);
		}

		std::vector<PathCommand> result;
		for (std::size_t i = commands.size(); i-- > 0;)
		{
			const Vec2 target_point = points[i];
			const auto& cmd = commands[i];
			if (std::holds_alternative<LineTo>(cmd))
				result.push_back(LineTo { target_point });
			else if (std::holds_alternative<QuadraticTo>(cmd))
			{
				const auto& quad_cmd = std::get<QuadraticTo>(cmd);
				result.push_back(QuadraticTo { quad_cmd.control, target_point });
			}
			else if (std::holds_alternative<CubicTo>(cmd))
			{
				const auto& cubic_cmd = std::get<CubicTo>(cmd);
				result.push_back(
					CubicTo { cubic_cmd.control2, cubic_cmd.control1, target_point });
			}
		}
		return result;
	}

	void build_closed_path(
		const std::vector<SegmentOffset>& offsets,
		const StrokeSettings& settings,
		std::vector<PathCommand>& result,
		bool is_left,
		bool is_outer,
		double left_offset,
		double right_offset)
	{
		if (offsets.empty())
			return;

		if (is_outer)
		{
			const auto& first_segment = offsets.front();
			result.push_back(
				MoveTo { is_left ? first_segment.left_start : first_segment.right_start });
			const std::size_t len = offsets.size();
			for (std::size_t i = 0; i < len; ++i)
			{
				const auto& current = offsets[i];
				const auto& next = offsets[(i + 1) % len];
				result.insert(
					result.end(),
					is_left ? current.left.begin() : current.right.begin(),
					is_left ? current.left.end() : current.right.end());
				const JoinData join_data = JoinData {
					current.end,     current.end_tangent, next.start_tangent, current.left_end,
					next.left_start, current.right_end,   next.right_start,   settings,
					left_offset,     right_offset
				};
				const auto join_side_result = join_data.resolve_side_join(
					is_left ? JoinSide::Left : JoinSide::Right,
					settings);
				if (!join_side_result.commands.empty())
					result.insert(
						result.end(),
						join_side_result.commands.begin(),
						join_side_result.commands.end());
			}
		}
		else
		{
			// The reversed contour walks the boundary backward, so it begins at the
			// last segment's END (where its reversed commands implicitly start), not
			// its start. Using the start collapses the first reversed piece into a
			// needle (and, for curves, replaces the arc with the closing line).
			const auto& last_segment = offsets.back();
			result.push_back(
				MoveTo { is_left ? last_segment.left_end : last_segment.right_end });
			const std::size_t len = offsets.size();
			for (std::size_t i = len; i-- > 0;)
			{
				const auto& current = offsets[i];
				const auto& prev = offsets[(i + len - 1) % len];
				const auto reversed_right = reverse_commands(
					is_left ? current.left : current.right,
					is_left ? current.left_start : current.right_start);
				result.insert(result.end(), reversed_right.begin(), reversed_right.end());
				const JoinData join_data = JoinData {
					prev.end,           prev.end_tangent, current.start_tangent, prev.left_end,
					current.left_start, prev.right_end,   current.right_start,   settings,
					left_offset,        right_offset
				};
				const auto join_side_result = join_data.resolve_side_join(
					is_left ? JoinSide::Left : JoinSide::Right,
					settings);
				if (!join_side_result.commands.empty())
				{
					const auto reversed_join = reverse_commands(
						join_side_result.commands,
						is_left ? prev.left_end : prev.right_end);
					result.insert(result.end(), reversed_join.begin(), reversed_join.end());
				}
			}
		}

		result.push_back(Close {});
	}

	// The pieces whose union is a closed stroke, for resolve_stroke_outline: a
	// band per segment between its two offset curves and a wedge per join side.
	// Built from the *untrimmed* offsets so each band is the full region its
	// segment sweeps. Appended to `coverage`.
	void build_coverage(
		const std::vector<SegmentOffset>& offsets,
		const StrokeSettings& settings,
		double left_offset,
		double right_offset,
		std::vector<std::vector<PathCommand>>& coverage)
	{
		const std::size_t len = offsets.size();
		for (std::size_t i = 0; i < len; ++i)
		{
			const auto& current = offsets[i];
			std::vector<PathCommand> band { MoveTo { current.left_start } };
			band.insert(band.end(), current.left.begin(), current.left.end());
			band.push_back(LineTo { current.right_end });
			const auto back = reverse_commands(current.right, current.right_start);
			band.insert(band.end(), back.begin(), back.end());
			band.push_back(Close {});
			coverage.push_back(std::move(band));

			const auto& next = offsets[(i + 1) % len];
			const JoinData join_data = JoinData {
				current.end,     current.end_tangent, next.start_tangent, current.left_end,
				next.left_start, current.right_end,   next.right_start,   settings,
				left_offset,     right_offset
			};
			for (const JoinSide side: { JoinSide::Left, JoinSide::Right })
			{
				const bool left = side == JoinSide::Left;
				std::vector<PathCommand> wedge {
					MoveTo { current.end },
					LineTo { left ? current.left_end : current.right_end },
				};
				const auto join = join_data.resolve_side_join(side, settings);
				wedge.insert(wedge.end(), join.commands.begin(), join.commands.end());
				wedge.push_back(LineTo { left ? next.left_start : next.right_start });
				wedge.push_back(Close {});
				coverage.push_back(std::move(wedge));
			}
		}
	}

	std::vector<PathCommand> finalize_closed_path(
		const std::vector<SegmentOffset>& offsets,
		const StrokeSettings& settings,
		double left_offset,
		double right_offset,
		bool is_ccw)
	{
		if (offsets.empty())
			return {};

		std::vector<PathCommand> outer;
		build_closed_path(offsets, settings, outer, !is_ccw, true, left_offset, right_offset);

		std::vector<PathCommand> inner;
		build_closed_path(offsets, settings, inner, is_ccw, false, left_offset, right_offset);

		std::vector<PathCommand> result;
		result.insert(result.end(), outer.begin(), outer.end());
		result.insert(result.end(), inner.begin(), inner.end());
		return result;
	}

	std::vector<PathCommand> finalize_open_path(
		const std::vector<SegmentOffset>& offsets,
		const StrokeSettings& settings,
		double left_offset,
		double right_offset)
	{
		if (offsets.empty())
			return {};

		std::vector<PathCommand> result;

		const auto& first_seg = offsets.front();
		const auto& last_seg = offsets.back();
		const auto& start_cap = generate_cap(
			first_seg.start,
			-first_seg.start_tangent,
			first_seg.left_start,
			first_seg.right_start,
			settings.line_cap,
			true);
		result.insert(result.end(), start_cap.begin(), start_cap.end());

		for (std::size_t i = 0; i < offsets.size(); ++i)
		{
			const auto& current = offsets[i];
			result.insert(result.end(), current.left.begin(), current.left.end());
			if (i + 1 >= offsets.size())
				break;
			const auto& next = offsets[i + 1];
			const JoinData join_data = JoinData {
				current.end,     current.end_tangent, next.start_tangent, current.left_end,
				next.left_start, current.right_end,   next.right_start,   settings,
				left_offset,     right_offset
			};
			const auto join_side_result = join_data.resolve_side_join(JoinSide::Left, settings);
			if (!join_side_result.commands.empty())
				result.insert(
					result.end(),
					join_side_result.commands.begin(),
					join_side_result.commands.end());
		}

		const auto& end_cap = generate_cap(
			last_seg.end,
			last_seg.end_tangent,
			last_seg.left_end,
			last_seg.right_end,
			settings.line_cap,
			false);
		result.insert(result.end(), end_cap.begin(), end_cap.end());

		for (std::size_t i = offsets.size(); i-- > 0;)
		{
			const auto& current = offsets[i];
			const auto& reversed_right = reverse_commands(current.right, current.right_start);
			result.insert(result.end(), reversed_right.begin(), reversed_right.end());
			if (i == 0)
				break;
			const auto& prev = offsets[i - 1];
			const JoinData join_data = JoinData {
				prev.end,           prev.end_tangent, current.start_tangent, prev.left_end,
				current.left_start, prev.right_end,   current.right_start,   settings,
				left_offset,        right_offset
			};
			const auto join_side_result =
				join_data.resolve_side_join(JoinSide::Right, settings);
			if (!join_side_result.commands.empty())
			{
				const auto reversed_join =
					reverse_commands(join_side_result.commands, prev.right_end);
				result.insert(result.end(), reversed_join.begin(), reversed_join.end());
			}
		}

		result.push_back(Close {});

		return result;
	}

	// Twice the signed area enclosed by a closed subpath, curves included (Green's
	// theorem in closed form per segment). Using only the segment endpoints is not
	// enough: a lens made of two quadratics has a zero-area endpoint polygon.
	double compute_segments_signed_area(const std::vector<Segment>& segments)
	{
		double area = 0.0;
		for (const auto& segment: segments)
		{
			const Vec2 p0 = segment.start;
			std::visit(
				[&](auto&& cmd) {
					using T = std::decay_t<decltype(cmd)>;
					if constexpr (std::is_same_v<T, LineTo>)
						area += p0.cross(cmd.p);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
					{
						const Vec2 p1 = cmd.control, p2 = cmd.end;
						area += (2.0 * (p0.cross(p1) + p1.cross(p2)) + p0.cross(p2)) / 3.0;
					}
					else if constexpr (std::is_same_v<T, CubicTo>)
					{
						const Vec2 p1 = cmd.control1, p2 = cmd.control2, p3 = cmd.end;
						area += (6.0 * p0.cross(p1) + 3.0 * p0.cross(p2) + p0.cross(p3)
								 + 3.0 * p1.cross(p2) + 3.0 * p1.cross(p3) + 6.0 * p2.cross(p3))
								/ 10.0;
					}
				},
				segment.command);
		}
		return area;
	}

	// Point and unit tangent halfway along a segment.
	std::optional<std::pair<Vec2, Vec2>> segment_midpoint(const Segment& segment)
	{
		return std::visit(
			[&](auto&& cmd) -> std::optional<std::pair<Vec2, Vec2>> {
				using T = std::decay_t<decltype(cmd)>;
				const Vec2 p0 = segment.start;
				if constexpr (std::is_same_v<T, LineTo>)
				{
					if (const auto t = (cmd.p - p0).normalize())
						return std::pair { Vec2::lerp(p0, cmd.p, 0.5), *t };
				}
				else if constexpr (std::is_same_v<T, QuadraticTo>)
				{
					if (const auto t =
							bezier::quadratic_bezier_derivative(p0, cmd.control, cmd.end, 0.5)
								.normalize())
						return std::pair {
							bezier::quadratic_bezier(p0, cmd.control, cmd.end, 0.5),
							*t
						};
				}
				else if constexpr (std::is_same_v<T, CubicTo>)
				{
					if (const auto t = bezier::cubic_bezier_derivative(
											p0, cmd.control1, cmd.control2, cmd.end, 0.5)
										   .normalize())
						return std::pair {
							bezier::cubic_bezier(p0, cmd.control1, cmd.control2, cmd.end, 0.5),
							*t
						};
				}
				return std::nullopt;
			},
			segment.command);
	}

	// Offset distances (left, right) for a subpath, or nullopt when an
	// inside/outside stroke has nothing to draw there. Left is the
	// perpendicular(tangent) side, positive distances offset that way.
	//
	// Inside/outside are relative to the fill of the *whole* path, not the
	// subpath's own winding: probe the fill just left and right of the subpath
	// (at its longest segment) and stroke towards the filled side for inside,
	// away from it for outside. A subpath with fill on both sides isn't a
	// boundary; the stroke at twice the width clipped to the fill is then the
	// full double-width band for inside, and nothing for outside (and the
	// reverse with fill on neither side).
	std::optional<std::pair<double, double>> resolve_offset_distances(
		const std::vector<Segment>& segments,
		bool is_closed,
		const StrokeSettings& settings,
		const std::vector<PathCommand>& shape)
	{
		const double half_width = settings.line_width * 0.5;
		if (!is_closed || settings.align == StrokeAlign::Center)
			return std::pair { half_width, -half_width };

		const Segment* longest = nullptr;
		double longest_len = -1.0;
		double min_x = segments.front().start.x, max_x = min_x;
		double min_y = segments.front().start.y, max_y = min_y;
		for (const auto& segment: segments)
		{
			const Vec2 end = end_point(segment.command).value_or(segment.start);
			const double len = (end - segment.start).length();
			min_x = std::min(min_x, end.x), max_x = std::max(max_x, end.x);
			min_y = std::min(min_y, end.y), max_y = std::max(max_y, end.y);
			if (len > longest_len)
			{
				longest_len = len;
				longest = &segment;
			}
		}
		const auto mid = longest ? segment_midpoint(*longest) : std::nullopt;
		if (!mid)
			return std::pair { half_width, -half_width };

		// Probe just off the subpath, relative to its own size (so tiny shapes are
		// probed proportionally close), but never inside floating-point noise.
		const double size = std::max(max_x - min_x, max_y - min_y);
		const double magnitude =
			std::max({ std::abs(min_x), std::abs(max_x), std::abs(min_y), std::abs(max_y) });
		// A tenth of the coincidence tolerance, like the clipper's probe.
		const double probe =
			std::max(size * EPSILON.relative * 0.1, magnitude * EPSILON.noise);
		const Vec2 left = perpendicular(mid->second);
		const bool fill_left =
			path_fill_contains(mid->first + left * probe, shape, settings.fill_rule);
		const bool fill_right =
			path_fill_contains(mid->first - left * probe, shape, settings.fill_rule);

		const double w = settings.line_width;
		const bool inside = settings.align == StrokeAlign::Inside;
		if (fill_left == fill_right)
		{
			if (fill_left == inside)
				return std::pair { w, -w };
			return std::nullopt;
		}
		const bool stroke_left = inside ? fill_left : fill_right;
		return stroke_left ? std::pair { w, 0.0 } : std::pair { 0.0, -w };
	}

	// A straight segment's offset boundary is a single LineTo on each side.
	// Where two consecutive offset edges cross, but only when that point lies
	// within both edges. On the concave side of a corner the edges overshoot past
	// their crossing (so it's returned); on the convex side, or when the segments
	// are too short to reach it, the crossing is out of range and nullopt is
	// returned (left for the join / nonzero fill).
	std::optional<Vec2>
		inner_corner(Vec2 start_a, Vec2 end_a, Vec2 dir_a, Vec2 start_b, Vec2 end_b, Vec2 dir_b)
	{
		const auto crossing = line_intersection(end_a, end_a + dir_a, start_b, start_b + dir_b);
		if (!crossing)
			return std::nullopt;
		const double s_a = (*crossing - start_a).dot(dir_a);
		const double len_a = (end_a - start_a).dot(dir_a);
		const double s_b = (*crossing - start_b).dot(dir_b);
		const double len_b = (end_b - start_b).dot(dir_b);
		if (s_a > 0.0 && s_a < len_a && s_b > 0.0 && s_b < len_b)
			return crossing;
		return std::nullopt;
	}

	// One boundary piece: a command plus the point it starts from.
	struct Piece
	{
		Vec2 start;
		const PathCommand* command;
	};

	// The last piece of a side's boundary (it ends at the side's `_end`).
	Piece terminal_piece(const std::vector<PathCommand>& cmds, Vec2 side_start)
	{
		const Vec2 start =
			cmds.size() >= 2 ?
				end_point(cmds[cmds.size() - 2]).value_or(side_start) :
				side_start;
		return { start, &cmds.back() };
	}

	// The first piece of a side's boundary (it starts at the side's `_start`).
	Piece leading_piece(const std::vector<PathCommand>& cmds, Vec2 side_start)
	{
		return { side_start, &cmds.front() };
	}

	bool is_line(const PathCommand& command)
	{
		return std::holds_alternative<LineTo>(command);
	}

	std::pair<Vec2, Vec2> as_line(const Piece& piece)
	{
		return { piece.start, std::get<LineTo>(*piece.command).p };
	}
	bezier::QuadraticBezier as_quad(const Piece& piece)
	{
		const auto& q = std::get<QuadraticTo>(*piece.command);
		return { piece.start, q.control, q.end };
	}
	bezier::CubicBezier as_cubic(const Piece& piece)
	{
		const auto& c = std::get<CubicTo>(*piece.command);
		return { piece.start, c.control1, c.control2, c.end };
	}

	// Crossings of two boundary pieces, dispatched to the exact intersection for
	// each line/quad/cubic combination. `t1` is always the parameter on `a`, `t2`
	// on `b` (results are swapped when the piece order is reversed relative to the
	// function's argument order). Line-line is handled by the caller.
	std::vector<bezier::CurveIntersection> piece_crossings(const Piece& a, const Piece& b)
	{
		const auto swap_ts = [](std::vector<bezier::CurveIntersection> hits) {
			for (auto& h: hits)
				std::swap(h.t1, h.t2);
			return hits;
		};
		const bool a_line = is_line(*a.command);
		const bool b_line = is_line(*b.command);
		const bool a_quad = std::holds_alternative<QuadraticTo>(*a.command);
		const bool b_quad = std::holds_alternative<QuadraticTo>(*b.command);

		if (a_line)
		{
			const auto [l0, l1] = as_line(a);
			if (b_quad)
				return bezier::line_quadratic_intersections(l0, l1, as_quad(b));
			return bezier::line_cubic_intersections(l0, l1, as_cubic(b));
		}
		if (b_line)
		{
			const auto [l0, l1] = as_line(b);
			if (a_quad)
				return swap_ts(bezier::line_quadratic_intersections(l0, l1, as_quad(a)));
			return swap_ts(bezier::line_cubic_intersections(l0, l1, as_cubic(a)));
		}
		if (a_quad && b_quad)
			return bezier::quadratic_quadratic_intersections(as_quad(a), as_quad(b));
		if (a_quad)
			return bezier::quadratic_cubic_intersections(as_quad(a), as_cubic(b));
		if (b_quad)
			return swap_ts(bezier::quadratic_cubic_intersections(as_quad(b), as_cubic(a)));
		return bezier::cubic_cubic_intersections(as_cubic(a), as_cubic(b));
	}

	// Trim the terminal piece to end at `corner` (keep the [0, t] half), rewriting
	// the boundary's last command in place.
	void trim_terminal(std::vector<PathCommand>& cmds, Vec2 piece_start, double t, Vec2 corner)
	{
		PathCommand& command = cmds.back();
		if (std::holds_alternative<QuadraticTo>(command))
		{
			const auto& q = std::get<QuadraticTo>(command);
			const auto [left, right] =
				bezier::split_quadratic_bezier(piece_start, q.control, q.end, t);
			command = QuadraticTo { left.p1, corner };
		}
		else if (std::holds_alternative<CubicTo>(command))
		{
			const auto& c = std::get<CubicTo>(command);
			const auto [left, right] =
				bezier::split_cubic_bezier(piece_start, c.control1, c.control2, c.end, t);
			command = CubicTo { left.p1, left.p2, corner };
		}
		else
			command = LineTo { corner };
	}

	// Trim the leading piece to start at `corner` (keep the [t, 1] half), rewriting
	// the boundary's first command in place. The caller sets the side's `_start`.
	void trim_leading(std::vector<PathCommand>& cmds, Vec2 piece_start, double t)
	{
		PathCommand& command = cmds.front();
		if (std::holds_alternative<QuadraticTo>(command))
		{
			const auto& q = std::get<QuadraticTo>(command);
			const auto [left, right] =
				bezier::split_quadratic_bezier(piece_start, q.control, q.end, t);
			command = QuadraticTo { right.p1, q.end };
		}
		else if (std::holds_alternative<CubicTo>(command))
		{
			const auto& c = std::get<CubicTo>(command);
			const auto [left, right] =
				bezier::split_cubic_bezier(piece_start, c.control1, c.control2, c.end, t);
			command = CubicTo { right.p1, right.p2, c.end };
		}
		// A line's endpoint is unchanged; only its start moves (set by the caller).
	}

	// Trim one side of one corner. `a_*` is the segment before the corner (its
	// boundary ends at a_end), `b_*` the segment after (its boundary starts at
	// b_start). Fields are passed by reference so the boundary command list and the
	// cached endpoint stay consistent for both the forward and reversed contours.
	void trim_corner_side(
		std::vector<PathCommand>& a_cmds,
		Vec2& a_end,
		Vec2 a_start,
		Vec2 a_dir,
		std::vector<PathCommand>& b_cmds,
		Vec2& b_start,
		Vec2 b_end,
		Vec2 b_dir)
	{
		if (a_cmds.empty() || b_cmds.empty())
			return;

		const Piece piece_a = terminal_piece(a_cmds, a_start);
		const Piece piece_b = leading_piece(b_cmds, b_start);

		// Line-line: exact line intersection (keeps clean LineTo output).
		if (is_line(*piece_a.command) && is_line(*piece_b.command))
		{
			if (const auto corner =
					inner_corner(piece_a.start, a_end, a_dir, piece_b.start, b_end, b_dir))
			{
				a_end = *corner;
				a_cmds.back() = LineTo { *corner };
				b_start = *corner;
			}
			return;
		}

		// A curve is involved: elevate both pieces to cubics, find where they
		// cross, and split each original piece there. Only in-piece crossings are
		// returned, so the convex side (no crossing) is left untouched — same as
		// the line case. Pick the crossing nearest the shared corner (largest t on
		// piece_a), strictly interior on both so a smooth endpoint-touch isn't
		// trimmed.
		const auto hits = piece_crossings(piece_a, piece_b);
		const bezier::CurveIntersection* best = nullptr;
		for (const auto& hit: hits)
		{
			if (hit.t1 <= EPSILON.param || hit.t1 >= 1.0 - EPSILON.param
				|| hit.t2 <= EPSILON.param || hit.t2 >= 1.0 - EPSILON.param)
				continue;
			if (!best || hit.t1 > best->t1)
				best = &hit;
		}
		if (best)
		{
			trim_terminal(a_cmds, piece_a.start, best->t1, best->point);
			trim_leading(b_cmds, piece_b.start, best->t2);
			a_end = best->point;
			b_start = best->point;
		}
	}

	// Trim the concave side of each corner so the offset edges meet at a point
	// instead of overshooting and folding back (the overshoot is what draws as a
	// needle spike). The convex side and unreachable corners are left untouched.
	void trim_corners(std::vector<SegmentOffset>& offsets, bool is_closed)
	{
		const std::size_t n = offsets.size();
		if (n < 2)
			return;
		const std::size_t corners = is_closed ? n : n - 1;
		for (std::size_t k = 0; k < corners; ++k)
		{
			SegmentOffset& a = offsets[k];
			SegmentOffset& b = offsets[(k + 1) % n];

			trim_corner_side(
				a.left,
				a.left_end,
				a.left_start,
				a.end_tangent,
				b.left,
				b.left_start,
				b.left_end,
				b.start_tangent);
			trim_corner_side(
				a.right,
				a.right_end,
				a.right_start,
				a.end_tangent,
				b.right,
				b.right_start,
				b.right_end,
				b.start_tangent);
		}
	}

	bool path_is_finite(const std::vector<PathCommand>& commands)
	{
		const auto finite = [](Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); };
		return std::ranges::all_of(commands, [&](const PathCommand& c) {
			return std::visit(
				[&](auto&& cmd) {
					using T = std::decay_t<decltype(cmd)>;
					if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
						return finite(cmd.p);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
						return finite(cmd.control) && finite(cmd.end);
					else if constexpr (std::is_same_v<T, CubicTo>)
						return finite(cmd.control1) && finite(cmd.control2) && finite(cmd.end);
					else
						return true;
				},
				c);
		});
	}

	// Raw outlines and coverage of the strokes clip_to_shape must resolve,
	// gathered so they can be resolved together (they may overlap one another).
	struct ClipInput
	{
		std::vector<PathCommand> outline;
		std::vector<std::vector<PathCommand>> coverage;
	};

	// Stroke one subpath with the given (left, right) offset distances. With
	// `defer`, the outline goes to clip_to_shape's input instead of `result`,
	// along with its coverage: a band per segment and a wedge per join for a
	// closed subpath (from the untrimmed offsets), the outline itself for an
	// open one.
	void process_subpath(
		const std::vector<Segment>& segments,
		bool is_closed,
		std::pair<double, double> distances,
		const StrokeSettings& settings,
		std::vector<PathCommand>& result,
		ClipInput* defer)
	{
		const auto [left_dist, right_dist] = distances;

		std::vector<SegmentOffset> offsets;
		for (const auto& segment: segments)
		{
			const auto offset = offset_segment(segment, left_dist, right_dist);
			if (offset)
				offsets.push_back(*offset);
		}
		if (offsets.empty())
			return;

		if (defer && is_closed)
			build_coverage(
				offsets,
				settings,
				std::abs(left_dist),
				std::abs(right_dist),
				defer->coverage);

		trim_corners(offsets, is_closed);

		const auto subpath_outline =
			is_closed ?
				finalize_closed_path(
					offsets,
					settings,
					std::abs(left_dist),
					std::abs(right_dist),
					compute_segments_signed_area(segments) > 0.0) :
				finalize_open_path(offsets, settings, std::abs(left_dist), std::abs(right_dist));

		if (defer && !is_closed)
			defer->coverage.push_back(subpath_outline);
		auto& out = defer ? defer->outline : result;
		out.insert(out.end(), subpath_outline.begin(), subpath_outline.end());
	}

	// A subpath of the input, ready to stroke.
	struct SourceSubpath
	{
		std::vector<Segment> segments;
		bool is_closed = false;
		Vec2 start;
		// Had any drawing command, even zero-length ones: a zero-length subpath
		// ("M p L p", "M p Z") is drawn as a dot by round/square caps (SVG 2), a
		// lone MoveTo is not.
		bool drew = false;
	};

	std::vector<SourceSubpath> split_subpaths(const std::vector<PathCommand>& commands)
	{
		std::vector<SourceSubpath> subpaths;
		SourceSubpath current;
		Vec2 pen = Vec2::zero();
		const auto finish = [&](bool is_closed) {
			current.is_closed = is_closed && !current.segments.empty();
			if (!current.segments.empty() || current.drew)
				subpaths.push_back(std::move(current));
			current = SourceSubpath { .start = pen };
		};
		// A segment is dropped only if it draws nothing: a curve whose end meets
		// its start can still loop out through its control points. Such a closed
		// loop is split in half, since curve offsetting needs distinct endpoints.
		const auto add_segment = [&](const PathCommand& cmd, std::initializer_list<Vec2> points) {
			current.drew = true;
			const Vec2 end = *(points.end() - 1);
			if (std::ranges::none_of(points, [&](Vec2 p) { return !points_are_too_close(pen, p); }))
				return;
			if (points_are_too_close(pen, end))
			{
				if (auto* q = std::get_if<QuadraticTo>(&cmd))
				{
					const auto [a, b] = bezier::split_quadratic_bezier(pen, q->control, q->end, 0.5);
					current.segments.push_back(Segment { a.p0, QuadraticTo { a.p1, a.p2 } });
					current.segments.push_back(Segment { b.p0, QuadraticTo { b.p1, b.p2 } });
				}
				else if (auto* c = std::get_if<CubicTo>(&cmd))
				{
					const auto [a, b] =
						bezier::split_cubic_bezier(pen, c->control1, c->control2, c->end, 0.5);
					current.segments.push_back(Segment { a.p0, CubicTo { a.p1, a.p2, a.p3 } });
					current.segments.push_back(Segment { b.p0, CubicTo { b.p1, b.p2, b.p3 } });
				}
				pen = end;
				return;
			}
			current.segments.push_back(Segment { pen, cmd });
			pen = end;
		};

		for (const PathCommand& cmd: commands)
		{
			if (auto* m = std::get_if<MoveTo>(&cmd))
			{
				finish(false);
				pen = m->p;
				current.start = pen;
			}
			else if (auto* l = std::get_if<LineTo>(&cmd))
				add_segment(cmd, { l->p });
			else if (auto* q = std::get_if<QuadraticTo>(&cmd))
				add_segment(cmd, { q->control, q->end });
			else if (auto* c = std::get_if<CubicTo>(&cmd))
				add_segment(cmd, { c->control1, c->control2, c->end });
			else if (std::holds_alternative<Close>(cmd))
			{
				current.drew = true;
				if (!current.segments.empty() && !points_are_too_close(pen, current.start))
					current.segments.push_back(Segment { pen, LineTo { current.start } });
				pen = current.start;
				finish(true);
			}
		}
		finish(false);
		return subpaths;
	}

	std::vector<PathCommand> subpath_commands(const SourceSubpath& subpath)
	{
		std::vector<PathCommand> commands { MoveTo { subpath.start } };
		for (const auto& segment: subpath.segments)
			commands.push_back(segment.command);
		if (subpath.is_closed)
			commands.push_back(Close {});
		return commands;
	}

	// Drop zero-length line segments — a LineTo that lands (within `tolerance`)
	// on the current point. Corner trimming on a collapsed (offset-0) boundary
	// and degenerate miters leave a few of these coincident points; they draw
	// nothing but pad the command list. Curves are left untouched. The tolerance
	// must follow the stroke's size: a fixed one deleted real corners of tiny
	// strokes, whose outline points are that close together.
	std::vector<PathCommand>
		remove_degenerate_lines(const std::vector<PathCommand>& commands, double tolerance)
	{
		std::vector<PathCommand> result;
		result.reserve(commands.size());
		std::optional<Vec2> current;
		for (const auto& cmd: commands)
		{
			if (std::holds_alternative<LineTo>(cmd))
			{
				const Vec2 p = std::get<LineTo>(cmd).p;
				if (current && (*current - p).length() <= tolerance)
					continue;
				current = p;
			}
			else if (std::holds_alternative<MoveTo>(cmd))
				current = std::get<MoveTo>(cmd).p;
			else if (std::holds_alternative<QuadraticTo>(cmd))
				current = std::get<QuadraticTo>(cmd).end;
			else if (std::holds_alternative<CubicTo>(cmd))
				current = std::get<CubicTo>(cmd).end;
			result.push_back(cmd);
		}
		return result;
	}
}

namespace stroke_outline
{
	std::vector<PathCommand> build_stroke_outline(
		const std::vector<PathCommand>& commands,
		const StrokeSettings& requested)
	{
		// Invalid input draws nothing rather than producing NaNs: a width that
		// isn't a positive finite number (SVG: 0 means no stroke, negative is an
		// error), or any non-finite coordinate. Out-of-range settings that still
		// have a sensible meaning are normalized instead.
		if (!std::isfinite(requested.line_width) || requested.line_width <= 0.0)
			return {};
		if (!path_is_finite(commands))
			return {};
		StrokeSettings settings = requested;
		// A miter limit is a ratio >= 1; anything below (or NaN) means "always
		// bevel", which a limit of exactly 1 already gives.
		if (!(settings.miter_limit >= 1.0))
			settings.miter_limit = 1.0;
		if (settings.dash && !std::isfinite(settings.dash->dash_offset))
			settings.dash->dash_offset = 0.0;

		std::vector<PathCommand> result;
		std::optional<ClipInput> clip;
		if (settings.clip_to_shape)
			clip.emplace();
		// Outline points closer than this are the same point (1e-5 at width 10).
		const double degenerate = settings.line_width * EPSILON.relative;

		// Aligned dashes with round/square caps, when clip_to_shape is off: their
		// caps reach past the dash's ends, which at a convex corner crosses the
		// shape's edge, so only clipping them to the shape is right. Resolved
		// separately from (and only when there's no) full clip_to_shape pass.
		ClipInput capped_dashes;
		const double w = settings.line_width;
		const auto emit_dot = [&](const DashDot& dot, std::pair<double, double> distances, ClipInput* defer) {
			const double half_width = std::max(std::abs(distances.first), std::abs(distances.second));
			const auto shape = generate_dot(dot.position, dot.tangent, half_width, settings.line_cap);
			if (shape.empty())
				return;
			if (defer)
				defer->coverage.push_back(shape);
			auto& out = defer ? defer->outline : result;
			out.insert(out.end(), shape.begin(), shape.end());
		};

		for (const auto& subpath: split_subpaths(commands))
		{
			if (subpath.segments.empty())
			{
				// No direction to align a square cap to; SVG aligns it to the axes.
				emit_dot({ subpath.start, Vec2(1.0, 0.0) }, { w * 0.5, -w * 0.5 }, nullptr);
				continue;
			}

			// Inside/outside apply to closed subpaths. With clip_to_shape they are
			// built as the full stroke at twice the width and clipped to the fill
			// (or its complement) afterwards — the definition itself, so no side
			// needs choosing and strokes that overlap or cross holes come out
			// right. Without it, the side is chosen once per subpath.
			const bool aligned = subpath.is_closed && settings.align != StrokeAlign::Center;
			ClipInput* defer = !aligned                                     ? nullptr :
							   clip                                         ? &*clip :
							   settings.dash && settings.line_cap != LineCap::Butt ? &capped_dashes :
																			  nullptr;
			const auto distances = defer ?
									   std::optional { std::pair { w, -w } } :
									   resolve_offset_distances(
										   subpath.segments,
										   subpath.is_closed,
										   settings,
										   commands);
			if (!distances)
				continue;

			if (!settings.dash)
			{
				process_subpath(
					subpath.segments,
					subpath.is_closed,
					*distances,
					settings,
					result,
					clip && subpath.is_closed ? &*clip : nullptr);
				continue;
			}

			// Dash this subpath on its own, so its dashes keep its alignment: each
			// dash is an open stroke offset to the same side (butt caps), or a
			// double-width stroke clipped to the fill like the subpath would be.
			std::vector<DashDot> dots;
			const auto dashed = dash_path(subpath_commands(subpath), *settings.dash, dots);
			for (const auto& dash: split_subpaths(dashed))
				if (!dash.segments.empty())
					process_subpath(dash.segments, dash.is_closed, *distances, settings, result, defer);
			for (const auto& dot: dots)
				emit_dot(dot, *distances, defer);
		}

		if (!capped_dashes.outline.empty())
		{
			const auto resolved = resolve_stroke_outline(
				remove_degenerate_lines(capped_dashes.outline, degenerate),
				capped_dashes.coverage,
				commands,
				settings.fill_rule,
				settings.align);
			result.insert(result.end(), resolved.begin(), resolved.end());
		}

		if (clip && !clip->outline.empty())
		{
			const auto resolved = resolve_stroke_outline(
				remove_degenerate_lines(clip->outline, degenerate),
				clip->coverage,
				commands,
				settings.fill_rule,
				settings.align);
			result.insert(result.end(), resolved.begin(), resolved.end());
		}

		return remove_degenerate_lines(result, degenerate);
	}
}
