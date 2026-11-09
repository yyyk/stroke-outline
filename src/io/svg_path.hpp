#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/path.hpp"

// Internal SVG path parse/format. The public entry points are Path::from_svg /
// Path::to_svg (include/stroke_outline/path.hpp).
namespace stroke_outline
{
	// Parse an SVG path "d" string into internal commands. Full grammar:
	// M/L/H/V/C/S/Q/T/A/Z, absolute + relative, smooth-curve reflection, arcs.
	// Stops at the first error and returns what parsed before it (SVG's error
	// handling); `error` says where and why, and is empty on success. Data not
	// starting with a moveto is an error at position 0 and yields nothing.
	std::vector<PathCommand>
		parse_svg_path(std::string_view d, std::optional<SvgParseError>& error);
	std::vector<PathCommand> parse_svg_path(std::string_view d);

	// Format commands into an SVG "d" string using absolute commands, with
	// coordinates rounded to at least `precision` decimals — more when needed to
	// keep rounding within ~1e-4 of the path's size — trailing zeros trimmed.
	std::string format_svg_path(const std::vector<PathCommand>& commands, int precision = 3);
}
