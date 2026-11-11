#include "stroke_outline/path.hpp"

#include <variant>

#include "core.hpp"
#include "io/svg_arc.hpp"
#include "io/svg_path.hpp"
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"

namespace
{
	using namespace stroke_outline;

	// Decode Path's primitive verb/coord storage into internal commands. Verb
	// bytes match Path::kMove..kClose (0..4); Path always emits well-formed
	// buffers, so no bounds guarding is needed here.
	std::vector<PathCommand>
		to_commands(const std::vector<std::uint8_t>& verbs, const std::vector<double>& coords)
	{
		std::vector<PathCommand> out;
		out.reserve(verbs.size());
		std::size_t k = 0;
		for (const std::uint8_t v: verbs)
		{
			switch (v)
			{
				case 0:
					out.push_back(MoveTo { { coords[k], coords[k + 1] } });
					k += 2;
					break;
				case 1:
					out.push_back(LineTo { { coords[k], coords[k + 1] } });
					k += 2;
					break;
				case 2:
					out.push_back(
						QuadraticTo {
							{ coords[k], coords[k + 1] },
							{ coords[k + 2], coords[k + 3] } });
					k += 4;
					break;
				case 3:
					out.push_back(
						CubicTo {
							{ coords[k], coords[k + 1] },
							{ coords[k + 2], coords[k + 3] },
							{ coords[k + 4], coords[k + 5] } });
					k += 6;
					break;
				case 4:
					out.push_back(Close {});
					break;
			}
		}
		return out;
	}
}

namespace stroke_outline
{
	Path& Path::move_to(double x, double y)
	{
		verbs_.push_back(kMove);
		coords_.push_back(x);
		coords_.push_back(y);
		return *this;
	}

	Path& Path::line_to(double x, double y)
	{
		verbs_.push_back(kLine);
		coords_.push_back(x);
		coords_.push_back(y);
		return *this;
	}

	Path& Path::quad_to(double cx, double cy, double x, double y)
	{
		verbs_.push_back(kQuad);
		coords_.insert(coords_.end(), { cx, cy, x, y });
		return *this;
	}

	Path& Path::cubic_to(double c1x, double c1y, double c2x, double c2y, double x, double y)
	{
		verbs_.push_back(kCubic);
		coords_.insert(coords_.end(), { c1x, c1y, c2x, c2y, x, y });
		return *this;
	}

	Path& Path::arc_to(
		double rx,
		double ry,
		double x_axis_rotation_deg,
		bool large_arc,
		bool sweep,
		double x,
		double y)
	{
		// Arc start = the path's current point (the last coordinate pair emitted).
		const Vec2 from =
			coords_.size() >= 2 ?
				Vec2 { coords_[coords_.size() - 2], coords_[coords_.size() - 1] } :
				Vec2 { 0.0, 0.0 };
		const auto cubics =
			svg::arc_to_cubics(from, rx, ry, x_axis_rotation_deg, large_arc, sweep, { x, y });
		if (cubics.empty())
		{
			// Degenerate arc: a straight line, or nothing when the end is the
			// current point (as the SVG parser does; SVG spec F.6.2).
			if (x != from.x || y != from.y)
				line_to(x, y);
		}
		else
			for (const auto& c: cubics)
				cubic_to(
					c.control1.x,
					c.control1.y,
					c.control2.x,
					c.control2.y,
					c.end.x,
					c.end.y);
		return *this;
	}

	Path& Path::close()
	{
		verbs_.push_back(kClose);
		return *this;
	}

	Path& Path::clear()
	{
		verbs_.clear();
		coords_.clear();
		return *this;
	}

	Path Path::from_svg(std::string_view d)
	{
		std::optional<SvgParseError> ignored;
		return from_svg(d, ignored);
	}

	Path Path::from_svg(std::string_view d, std::optional<SvgParseError>& error)
	{
		Path p;
		for (const auto& cmd: parse_svg_path(d, error))
			std::visit(
				[&](auto&& x) {
					using T = std::decay_t<decltype(x)>;
					if constexpr (std::is_same_v<T, MoveTo>)
						p.move_to(x.p.x, x.p.y);
					else if constexpr (std::is_same_v<T, LineTo>)
						p.line_to(x.p.x, x.p.y);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
						p.quad_to(x.control.x, x.control.y, x.end.x, x.end.y);
					else if constexpr (std::is_same_v<T, CubicTo>)
						p.cubic_to(
							x.control1.x,
							x.control1.y,
							x.control2.x,
							x.control2.y,
							x.end.x,
							x.end.y);
					else
						p.close();
				},
				cmd);
		return p;
	}

	std::string Path::to_svg(int precision) const
	{
		return format_svg_path(to_commands(verbs_, coords_), precision);
	}

	void Path::for_each(const Sink& sink) const
	{
		std::size_t k = 0;
		for (const std::uint8_t v: verbs_)
		{
			switch (v)
			{
				case kMove:
					if (sink.move_to)
						sink.move_to(coords_[k], coords_[k + 1]);
					k += 2;
					break;
				case kLine:
					if (sink.line_to)
						sink.line_to(coords_[k], coords_[k + 1]);
					k += 2;
					break;
				case kQuad:
					if (sink.quad_to)
						sink.quad_to(
							coords_[k],
							coords_[k + 1],
							coords_[k + 2],
							coords_[k + 3]);
					k += 4;
					break;
				case kCubic:
					if (sink.cubic_to)
						sink.cubic_to(
							coords_[k],
							coords_[k + 1],
							coords_[k + 2],
							coords_[k + 3],
							coords_[k + 4],
							coords_[k + 5]);
					k += 6;
					break;
				case kClose:
					if (sink.close)
						sink.close();
					break;
			}
		}
	}

	Path build_outline(const Path& path, const StrokeSettings& settings)
	{
		const auto commands = to_commands(path.verbs_, path.coords_);
		const auto out = build_stroke_outline(commands, settings);

		Path result;
		for (const auto& c: out)
			std::visit(
				[&](auto&& x) {
					using T = std::decay_t<decltype(x)>;
					if constexpr (std::is_same_v<T, MoveTo>)
						result.move_to(x.p.x, x.p.y);
					else if constexpr (std::is_same_v<T, LineTo>)
						result.line_to(x.p.x, x.p.y);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
						result.quad_to(x.control.x, x.control.y, x.end.x, x.end.y);
					else if constexpr (std::is_same_v<T, CubicTo>)
						result.cubic_to(
							x.control1.x,
							x.control1.y,
							x.control2.x,
							x.control2.y,
							x.end.x,
							x.end.y);
					else
						result.close();
				},
				c);
		return result;
	}

	Path build_outline(
		std::string_view d,
		const StrokeSettings& settings,
		std::optional<SvgParseError>& error)
	{
		return build_outline(Path::from_svg(d, error), settings);
	}

	Path build_outline(std::string_view d, const StrokeSettings& settings)
	{
		std::optional<SvgParseError> ignored;
		return build_outline(d, settings, ignored);
	}
}
