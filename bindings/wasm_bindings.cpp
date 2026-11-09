#include <emscripten/bind.h>

#include <optional>
#include <string>
#include <vector>

#include "stroke_outline/stroke_outline.hpp"

// These bindings are a pure consumer of the public API (Path + build_outline). They never touch the internal PathCommand/Vec2 types — the
// JS boundary uses its own small POD mirror below. The npm package (js/) wraps
// them in a friendlier API; these are the raw module functions.
using namespace emscripten;
using namespace stroke_outline;

namespace
{
	// Binding-local point (registered to JS as "Vec2"). Not the internal Vec2.
	struct Point
	{
		double x, y;
	};

	enum class Verb { MoveTo, LineTo, QuadraticTo, CubicTo, Close };

	struct JsCommand
	{
		Verb verb;
		Point ctrl1;
		Point ctrl2;
		Point to;
	};

	struct JsStrokeSettings
	{
		double line_width = 1.0;
		LineJoin line_join = LineJoin::Miter;
		LineCap line_cap = LineCap::Butt;
		double miter_limit = 4.0;
		StrokeAlign align = StrokeAlign::Center;
		FillRule fill_rule = FillRule::NonZero;
		bool clip_to_shape = false;
		val dash_pattern = val::array();
		double dash_offset = 0.0;
	};

	StrokeSettings to_settings(const JsStrokeSettings& s)
	{
		StrokeSettings out;
		out.line_width = s.line_width;
		out.line_join = s.line_join;
		out.line_cap = s.line_cap;
		out.miter_limit = s.miter_limit;
		out.align = s.align;
		out.fill_rule = s.fill_rule;
		out.clip_to_shape = s.clip_to_shape;
		const std::vector<double> pattern = vecFromJSArray<double>(s.dash_pattern);
		if (!pattern.empty())
			out.dash = DashSettings { pattern, s.dash_offset };
		return out;
	}

	Path to_path(const std::vector<JsCommand>& cmds)
	{
		Path p;
		for (const auto& c: cmds)
			switch (c.verb)
			{
				case Verb::MoveTo:
					p.move_to(c.to.x, c.to.y);
					break;
				case Verb::LineTo:
					p.line_to(c.to.x, c.to.y);
					break;
				case Verb::QuadraticTo:
					p.quad_to(c.ctrl1.x, c.ctrl1.y, c.to.x, c.to.y);
					break;
				case Verb::CubicTo:
					p.cubic_to(c.ctrl1.x, c.ctrl1.y, c.ctrl2.x, c.ctrl2.y, c.to.x, c.to.y);
					break;
				case Verb::Close:
					p.close();
					break;
			}
		return p;
	}

	std::vector<JsCommand> to_js(const Path& path)
	{
		std::vector<JsCommand> js_out;
		path.for_each(
			{
				.move_to =
					[&](double x, double y) {
						js_out.push_back({ Verb::MoveTo, {}, {}, { x, y } });
					},
				.line_to =
					[&](double x, double y) {
						js_out.push_back({ Verb::LineTo, {}, {}, { x, y } });
					},
				.quad_to =
					[&](double cx, double cy, double x, double y) {
						js_out.push_back({ Verb::QuadraticTo, { cx, cy }, {}, { x, y } });
					},
				.cubic_to =
					[&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
						js_out.push_back(
							{ Verb::CubicTo, { c1x, c1y }, { c2x, c2y }, { x, y } });
					},
				.close = [&] { js_out.push_back({ Verb::Close, {}, {}, {} }); },
			});
		return js_out;
	}

	std::vector<JsCommand>
		buildStroke(const std::vector<JsCommand>& js_path, const JsStrokeSettings& js_settings)
	{
		return to_js(build_outline(to_path(js_path), to_settings(js_settings)));
	}

	// The C++ defaults (StrokeSettings{}), so JS fills in omitted options from
	// the same source instead of a copy that could drift.
	JsStrokeSettings defaultStrokeSettings()
	{
		const StrokeSettings d;
		JsStrokeSettings s;
		s.line_width = d.line_width;
		s.line_join = d.line_join;
		s.line_cap = d.line_cap;
		s.miter_limit = d.miter_limit;
		s.align = d.align;
		s.fill_rule = d.fill_rule;
		s.clip_to_shape = d.clip_to_shape;
		s.dash_pattern = val::array();
		s.dash_offset = d.dash ? d.dash->dash_offset : 0.0;
		return s;
	}

	// SVG path data to commands (lenient, like Path::from_svg; see
	// parseSvgPathChecked for errors).
	std::vector<JsCommand> parseSvgPath(const std::string& d)
	{
		return to_js(Path::from_svg(d));
	}

	// Both at once, from a single parse: the commands (everything before the
	// first error) and the error, if any.
	struct SvgParse
	{
		std::vector<JsCommand> commands;
		bool ok = true;
		std::string message;
	};

	SvgParse parseSvgPathChecked(const std::string& d)
	{
		std::optional<SvgParseError> error;
		SvgParse result { to_js(Path::from_svg(d, error)) };
		if (error)
		{
			result.ok = false;
			result.message = error->message;
		}
		return result;
	}

	// Commands to SVG path data, with at least `precision` decimals.
	std::string pathToSvg(const std::vector<JsCommand>& cmds, int precision)
	{
		return to_path(cmds).to_svg(precision);
	}

	// An SVG elliptical arc from (from_x, from_y) as cubic commands (without the
	// leading MoveTo), via Path::arc_to.
	std::vector<JsCommand> arcToCommands(
		double from_x,
		double from_y,
		double rx,
		double ry,
		double x_axis_rotation_deg,
		bool large_arc,
		bool sweep,
		double x,
		double y)
	{
		auto cmds = to_js(Path {}.move_to(from_x, from_y).arc_to(
			rx, ry, x_axis_rotation_deg, large_arc, sweep, x, y));
		cmds.erase(cmds.begin());
		return cmds;
	}
}

EMSCRIPTEN_BINDINGS(stroke_outline_module)
{
	value_object<Point>("Vec2").field("x", &Point::x).field("y", &Point::y);

	enum_<Verb>("Verb")
		.value("MoveTo", Verb::MoveTo)
		.value("LineTo", Verb::LineTo)
		.value("QuadraticTo", Verb::QuadraticTo)
		.value("CubicTo", Verb::CubicTo)
		.value("Close", Verb::Close);

	enum_<LineJoin>("LineJoin")
		.value("Miter", LineJoin::Miter)
		.value("Bevel", LineJoin::Bevel)
		.value("Round", LineJoin::Round);

	enum_<LineCap>("LineCap")
		.value("Butt", LineCap::Butt)
		.value("Square", LineCap::Square)
		.value("Round", LineCap::Round);

	enum_<StrokeAlign>("StrokeAlign")
		.value("Center", StrokeAlign::Center)
		.value("Inside", StrokeAlign::Inside)
		.value("Outside", StrokeAlign::Outside);


	enum_<FillRule>("FillRule")
		.value("NonZero", FillRule::NonZero)
		.value("EvenOdd", FillRule::EvenOdd);

	value_object<JsCommand>("PathCommand")
		.field("verb", &JsCommand::verb)
		.field("ctrl1", &JsCommand::ctrl1)
		.field("ctrl2", &JsCommand::ctrl2)
		.field("to", &JsCommand::to);

	value_object<JsStrokeSettings>("StrokeSettings")
		.field("lineWidth", &JsStrokeSettings::line_width)
		.field("lineJoin", &JsStrokeSettings::line_join)
		.field("lineCap", &JsStrokeSettings::line_cap)
		.field("miterLimit", &JsStrokeSettings::miter_limit)
		.field("align", &JsStrokeSettings::align)
		.field("fillRule", &JsStrokeSettings::fill_rule)
		.field("clipToShape", &JsStrokeSettings::clip_to_shape)
		.field("dashPattern", &JsStrokeSettings::dash_pattern)
		.field("dashOffset", &JsStrokeSettings::dash_offset);

	register_vector<JsCommand>("PathCommandVector");

	value_object<SvgParse>("SvgParse")
		.field("commands", &SvgParse::commands)
		.field("ok", &SvgParse::ok)
		.field("message", &SvgParse::message);

	function("buildStrokeOutline", &buildStroke);
	function("defaultStrokeSettings", &defaultStrokeSettings);
	function("parseSvgPath", &parseSvgPath);
	function("parseSvgPathChecked", &parseSvgPathChecked);
	function("pathToSvg", &pathToSvg);
	function("arcToCommands", &arcToCommands);
}
