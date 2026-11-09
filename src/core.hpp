#pragma once

#include <vector>

#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/settings.hpp"

// Internal core stroking entry point. It works in terms of the internal
// PathCommand representation and is not part of the public API — public callers
// go through Path / build_outline (include/stroke_outline/path.hpp).
namespace stroke_outline
{
	std::vector<PathCommand> build_stroke_outline(
		const std::vector<PathCommand>& commands,
		const StrokeSettings& settings);
}
