#include <algorithm>
#include <span>
#include <numeric>
#include <cmath>
#include <ranges>
#include <cstddef>
#include "stroke_outline/Vec2.hpp"
#include "stroke/dash.hpp"
#include "stroke/utils.hpp"
#include "bezier/bezier.hpp"
#include "math/Eps.hpp"

using namespace stroke_outline;

namespace
{
	static constexpr double DEFAULT_PATTERN[] = { 1.0 };

	struct DashState
	{
		std::span<const double> pattern;
		unsigned int pattern_index = 0;
		double remaining = 0.0;

		DashState(std::span<const double> pattern, double offset)
		{
			const double total_length = std::accumulate(pattern.begin(), pattern.end(), 0.0);
			if (total_length <= 0.0 || pattern.empty())
			{
				this->pattern = DEFAULT_PATTERN;
				this->pattern_index = 0;
				this->remaining = 1.0;
				return;
			}
			double offset_mod = std::fmod(offset, total_length);
			if (offset_mod < 0.0)
				offset_mod += total_length;
			unsigned int index = 0;
			double remain = pattern[index];
			while (offset_mod > 0.0 && offset_mod >= remain)
			{
				offset_mod -= remain;
				index = (index + 1) % pattern.size();
				remain = pattern[index];
			}
			remain -= offset_mod;
			this->pattern = pattern;
			this->pattern_index = index;
			this->remaining = remain;
		}

		bool is_dash() const { return pattern_index % 2 == 0; }

		double distance_to_next_dash() const { return remaining; }

		double advance(double distance)
		{
			remaining -= distance;
			if (remaining <= 0.0)
			{
				pattern_index = (pattern_index + 1) % pattern.size();
				remaining = pattern[pattern_index];
			}
			return remaining;
		}
	};

	// Samples for an arc-length table: about one per unit of length, at least
	// `steps`, but capped — a huge curve would otherwise allocate one sample per
	// unit (and overflow `int` past 2^31).
	constexpr double kMaxLutSteps = 4096.0;
	int resolve_lut_steps(double arc_length, int steps)
	{
		const double estimated = std::isfinite(arc_length) ? std::ceil(arc_length) : 0.0;
		return static_cast<int>(
			std::clamp(std::max(estimated, static_cast<double>(steps)), 10.0, kMaxLutSteps));
	}

	struct ArcLengthLUT
	{
		double total_length = 0.0;
		std::vector<double> lengths;
		int steps = 0;

		static ArcLengthLUT from_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, int steps)
		{
			const int resolved_steps =
				resolve_lut_steps(bezier::arc_length_quadratic(p0, p1, p2), steps);
			std::vector<double> lengths;
			lengths.reserve(resolved_steps + 1);
			lengths.push_back(0.0);
			double cumulative = 0.0;
			Vec2 prev_point = p0;
			for (int i = 1; i <= resolved_steps; ++i)
			{
				const double t = static_cast<double>(i) / static_cast<double>(resolved_steps);
				const Vec2 point = bezier::quadratic_bezier(p0, p1, p2, t);
				cumulative += point.distance(prev_point);
				lengths.push_back(cumulative);
				prev_point = point;
			}
			ArcLengthLUT lut = {
				.total_length = cumulative,
				.lengths = std::move(lengths),
				.steps = resolved_steps,
			};
			return lut;
		}

		static ArcLengthLUT from_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int steps)
		{
			const int resolved_steps =
				resolve_lut_steps(bezier::arc_length_cubic(p0, p1, p2, p3), steps);
			std::vector<double> lengths;
			lengths.reserve(resolved_steps + 1);
			lengths.push_back(0.0);
			double cumulative = 0.0;
			Vec2 prev_point = p0;
			for (int i = 1; i <= resolved_steps; ++i)
			{
				const double t = static_cast<double>(i) / static_cast<double>(resolved_steps);
				const Vec2 point = bezier::cubic_bezier(p0, p1, p2, p3, t);
				cumulative += point.distance(prev_point);
				lengths.push_back(cumulative);
				prev_point = point;
			}
			ArcLengthLUT lut = {
				.total_length = cumulative,
				.lengths = std::move(lengths),
				.steps = resolved_steps,
			};
			return lut;
		}

		double t_at_distance(double distance) const
		{
			if (distance <= 0.0)
				return 0.0;
			if (distance >= total_length)
				return 1.0;
			int low = 0;
			int high = static_cast<int>(steps);
			while (low < high)
			{
				int mid = (low + high) / 2;
				if (lengths[mid] < distance)
					low = mid + 1;
				else
					high = mid;
			}
			const int index = std::min(low, static_cast<int>(steps));
			if (index == 0)
				return 0.0;
			const double length_before = lengths[index - 1];
			const double length_at = lengths[index];
			const double segment_length = length_at - length_before;
			const double t_before = static_cast<double>(index - 1) / static_cast<double>(steps);
			if (segment_length < EPSILON.solver)
				return t_before;
			const double t_at = static_cast<double>(index) / static_cast<double>(steps);
			const double ratio = (distance - length_before) / segment_length;
			return t_before + ratio * (t_at - t_before);
		}
	};

	// Upper bound on dash/gap intervals along a path before dashing is refused.
	constexpr double kMaxDashIntervals = 100000.0;

	// Total arc length of every segment, including implicit closing lines.
	double path_length(const std::vector<PathCommand>& commands)
	{
		double length = 0.0;
		Vec2 pen = Vec2::zero();
		Vec2 start = pen;
		for (const auto& command: commands)
		{
			if (auto* m = std::get_if<MoveTo>(&command))
				pen = start = m->p;
			else if (auto* l = std::get_if<LineTo>(&command))
			{
				length += pen.distance(l->p);
				pen = l->p;
			}
			else if (auto* q = std::get_if<QuadraticTo>(&command))
			{
				length += bezier::arc_length_quadratic(pen, q->control, q->end);
				pen = q->end;
			}
			else if (auto* c = std::get_if<CubicTo>(&command))
			{
				length += bezier::arc_length_cubic(pen, c->control1, c->control2, c->end);
				pen = c->end;
			}
			else if (std::holds_alternative<Close>(command))
			{
				length += pen.distance(start);
				pen = start;
			}
		}
		return length;
	}

	std::optional<std::vector<double>>
		normalize_dash_pattern(const std::vector<double>& pattern)
	{
		if (std::ranges::any_of(pattern, [](double v) { return !std::isfinite(v) || v < 0.0; }))
			return std::nullopt;
		if (std::ranges::all_of(pattern, [](double v) { return v == 0.0; }))
			return std::nullopt;

		if (pattern.size() % 2 == 1)
		{
			std::vector<double> duplicated;
			duplicated.reserve(pattern.size() * 2);
			duplicated.insert(duplicated.end(), pattern.begin(), pattern.end());
			duplicated.insert(duplicated.end(), pattern.begin(), pattern.end());
			return duplicated;
		}
		else
		{
			return pattern;
		}
	}

	void reconnect_wrapped_closed_subpath(
		std::vector<PathCommand>& commands,
		std::size_t start_index)
	{
		if (start_index >= commands.size())
			return;

		const std::vector<PathCommand> subpath(commands.begin() + start_index, commands.end());
		Vec2 first_start;
		if (std::holds_alternative<MoveTo>(subpath.front()))
			first_start = std::get<MoveTo>(subpath.front()).p;
		else
			return;

		std::size_t last_move_index = 0;
		for (std::size_t i = subpath.size(); i-- > 0;)
		{
			if (std::holds_alternative<MoveTo>(subpath[i]))
			{
				last_move_index = i;
				break;
			}
		}
		if (last_move_index == 0)
			return;

		const std::optional<Vec2> last_end_opt =
			subpath.empty() ? std::nullopt : end_point(subpath.back());
		if (!last_end_opt)
			return;
		const Vec2 last_end = *last_end_opt;
		if ((last_end - first_start).dot(last_end - first_start) > EPSILON.solver)
			return;

		auto it = std::find_if(subpath.begin() + 1, subpath.end(), [](const PathCommand& c) {
			return std::holds_alternative<MoveTo>(c);
		});
		std::size_t first_split_index = static_cast<std::size_t>(it - subpath.begin());
		if (first_split_index <= 1)
			return;

		std::vector<PathCommand> reordered;
		reordered.reserve(subpath.size());
		reordered.insert(reordered.end(), subpath.begin() + first_split_index, subpath.end());
		reordered
			.insert(reordered.end(), subpath.begin() + 1, subpath.begin() + first_split_index);
		commands.erase(commands.begin() + start_index, commands.end());
		commands.insert(commands.end(), reordered.begin(), reordered.end());
	}

	bezier::QuadraticBezier
		extract_quadratic_portion(Vec2 p0, Vec2 p1, Vec2 p2, double t_start, double t_end)
	{
		if (t_start <= 0.0 && t_end >= 1.0)
			return { p0, p1, p2 };

		const auto left = bezier::split_quadratic_bezier(p0, p1, p2, t_end).first;
		if (t_start <= 0.0)
			return left;

		const double t_relative = t_start / t_end;
		return bezier::split_quadratic_bezier(left.p0, left.p1, left.p2, t_relative).second;
	}

	bezier::CubicBezier
		extract_cubic_portion(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, double t_start, double t_end)
	{
		if (t_start <= 0.0 && t_end >= 1.0)
			return { p0, p1, p2, p3 };

		const auto left = bezier::split_cubic_bezier(p0, p1, p2, p3, t_end).first;
		if (t_start <= 0.0)
			return left;

		const double t_relative = t_start / t_end;
		return bezier::split_cubic_bezier(left.p0, left.p1, left.p2, left.p3, t_relative)
			.second;
	}

	// A zero-length dash at the current position: record it as a dot (the
	// stroker draws it as a round/square cap shape) and step past it. Returns
	// whether it consumed one.
	bool take_dot(DashState& dash_state, Vec2 point, Vec2 tangent, std::vector<DashDot>& dots)
	{
		if (!dash_state.is_dash() || dash_state.distance_to_next_dash() > 0.0)
			return false;
		dots.push_back({ point, tangent.normalize().value_or(Vec2(1.0, 0.0)) });
		dash_state.advance(0.0);
		return true;
	}

	void dash_line_segment(
		Vec2 start,
		Vec2 end,
		DashState& dash_state,
		std::vector<PathCommand>& dashed_path,
		bool& need_move_to,
		std::vector<DashDot>& dots)
	{
		const Vec2 delta = end - start;
		const double segment_length = delta.length();
		if (segment_length < EPSILON.solver)
			return;

		const Vec2 direction = delta / segment_length;
		double distance_along = 0.0;
		while (distance_along < segment_length)
		{
			if (take_dot(dash_state, start + direction * distance_along, direction, dots))
				continue;
			const double distance_to_next_dash = dash_state.distance_to_next_dash();
			const double remaining_length = segment_length - distance_along;
			const double step_length = std::min(distance_to_next_dash, remaining_length);

			if (dash_state.is_dash())
			{
				const Vec2 start_point = start + direction * distance_along;
				const Vec2 end_point = start + direction * (distance_along + step_length);
				if (need_move_to)
				{
					dashed_path.push_back(MoveTo { start_point });
					need_move_to = false;
				}
				dashed_path.push_back(LineTo { end_point });
			}
			else
			{
				need_move_to = true;
			}
			distance_along += step_length;
			dash_state.advance(step_length);
		}
	}

	void dash_quadratic_segment(
		Vec2 p0,
		Vec2 p1,
		Vec2 p2,
		DashState& dash_state,
		std::vector<PathCommand>& dashed_path,
		bool& need_move_to,
		std::vector<DashDot>& dots)
	{
		const ArcLengthLUT lut = ArcLengthLUT::from_quadratic(p0, p1, p2, 100);
		const double segment_length = lut.total_length;
		if (segment_length < EPSILON.solver)
			return;

		double distance_along = 0.0;
		while (distance_along < segment_length)
		{
			{
				const double t = lut.t_at_distance(distance_along);
				if (take_dot(
						dash_state,
						bezier::quadratic_bezier(p0, p1, p2, t),
						bezier::quadratic_bezier_derivative(p0, p1, p2, t),
						dots))
					continue;
			}
			const double distance_to_next_dash = dash_state.distance_to_next_dash();
			const double remaining_length = segment_length - distance_along;
			const double step_length = std::min(distance_to_next_dash, remaining_length);
			const double t_start = lut.t_at_distance(distance_along);
			const double t_end = lut.t_at_distance(distance_along + step_length);
			if (dash_state.is_dash())
			{
				const auto curve = extract_quadratic_portion(p0, p1, p2, t_start, t_end);
				if (need_move_to)
				{
					dashed_path.push_back(MoveTo { curve.p0 });
					need_move_to = false;
				}
				dashed_path.push_back(QuadraticTo { curve.p1, curve.p2 });
			}
			else
			{
				need_move_to = true;
			}
			distance_along += step_length;
			dash_state.advance(step_length);
		}
	}

	void dash_cubic_segment(
		Vec2 p0,
		Vec2 p1,
		Vec2 p2,
		Vec2 p3,
		DashState& dash_state,
		std::vector<PathCommand>& dashed_path,
		bool& need_move_to,
		std::vector<DashDot>& dots)
	{
		const ArcLengthLUT lut = ArcLengthLUT::from_cubic(p0, p1, p2, p3, 100);
		const double segment_length = lut.total_length;
		if (segment_length < EPSILON.solver)
			return;

		double distance_along = 0.0;
		while (distance_along < segment_length)
		{
			{
				const double t = lut.t_at_distance(distance_along);
				if (take_dot(
						dash_state,
						bezier::cubic_bezier(p0, p1, p2, p3, t),
						bezier::cubic_bezier_derivative(p0, p1, p2, p3, t),
						dots))
					continue;
			}
			const double distance_to_next_dash = dash_state.distance_to_next_dash();
			const double remaining_length = segment_length - distance_along;
			const double step_length = std::min(distance_to_next_dash, remaining_length);
			const double t_start = lut.t_at_distance(distance_along);
			const double t_end = lut.t_at_distance(distance_along + step_length);
			if (dash_state.is_dash())
			{
				const auto curve = extract_cubic_portion(p0, p1, p2, p3, t_start, t_end);
				if (need_move_to)
				{
					dashed_path.push_back(MoveTo { curve.p0 });
					need_move_to = false;
				}
				dashed_path.push_back(CubicTo { curve.p1, curve.p2, curve.p3 });
			}
			else
			{
				need_move_to = true;
			}
			distance_along += step_length;
			dash_state.advance(step_length);
		}
	}
}

namespace stroke_outline
{
	std::vector<PathCommand> dash_path(
		const std::vector<PathCommand>& commands,
		const DashSettings& settings,
		std::vector<DashDot>& dots)
	{
		if (settings.dash_pattern.empty())
			return commands;

		const auto normalized_pattern = normalize_dash_pattern(settings.dash_pattern);
		if (!normalized_pattern)
			return commands;

		// Refuse patterns that would produce an unbounded number of dashes (a tiny
		// period over a long path): like Skia, stroke the path undashed instead.
		const double period =
			std::accumulate(normalized_pattern->begin(), normalized_pattern->end(), 0.0);
		const double intervals =
			path_length(commands) / period * static_cast<double>(normalized_pattern->size());
		if (!std::isfinite(intervals) || intervals > kMaxDashIntervals)
			return commands;

		std::vector<PathCommand> result;
		const auto fresh_state = [&] {
			return DashState(*normalized_pattern, settings.dash_offset);
		};
		DashState state = fresh_state();
		Vec2 current_point = Vec2::zero();
		Vec2 subpath_start_point = current_point;
		bool need_move_to = true;
		std::size_t dashed_subpath_start = 0;
		// Direction at the end of the last segment, and whether the subpath has
		// any: a zero-length dash landing exactly on the end of an open subpath
		// (e.g. the last dot of a [0, gap] pattern) is still drawn.
		Vec2 end_tangent { 1.0, 0.0 };
		bool open_subpath = false;
		const auto finish_open_subpath = [&] {
			if (open_subpath)
				take_dot(state, current_point, end_tangent, dots);
			open_subpath = false;
		};

		for (const auto& command: commands)
		{
			if (auto* m = std::get_if<MoveTo>(&command))
			{
				finish_open_subpath();
				// Each subpath restarts the pattern (SVG 2, Skia).
				state = fresh_state();
				current_point = m->p;
				subpath_start_point = m->p;
				need_move_to = true;
				dashed_subpath_start = result.size();
			}
			else if (auto* l = std::get_if<LineTo>(&command))
			{
				dash_line_segment(current_point, l->p, state, result, need_move_to, dots);
				if (const auto t = (l->p - current_point).normalize())
					end_tangent = *t;
				open_subpath = true;
				current_point = l->p;
			}
			else if (auto* q = std::get_if<QuadraticTo>(&command))
			{
				dash_quadratic_segment(
					current_point,
					q->control,
					q->end,
					state,
					result,
					need_move_to,
					dots);
				end_tangent = bezier::quadratic_bezier_derivative(current_point, q->control, q->end, 1.0)
								  .normalize()
								  .value_or(end_tangent);
				open_subpath = true;
				current_point = q->end;
			}
			else if (auto* c = std::get_if<CubicTo>(&command))
			{
				dash_cubic_segment(
					current_point,
					c->control1,
					c->control2,
					c->end,
					state,
					result,
					need_move_to,
					dots);
				end_tangent =
					bezier::cubic_bezier_derivative(current_point, c->control1, c->control2, c->end, 1.0)
						.normalize()
						.value_or(end_tangent);
				open_subpath = true;
				current_point = c->end;
			}
			else if (std::holds_alternative<Close>(command))
			{
				if (current_point != subpath_start_point)
					dash_line_segment(
						current_point,
						subpath_start_point,
						state,
						result,
						need_move_to,
						dots);
				reconnect_wrapped_closed_subpath(result, dashed_subpath_start);
				current_point = subpath_start_point;
				need_move_to = true;
				// A closed subpath ends where it started: its start already got
				// any dot due there.
				open_subpath = false;
			}
		}
		finish_open_subpath();

		return result;
	}
}
