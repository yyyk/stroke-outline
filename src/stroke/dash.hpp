#pragma once
#include <vector>
#include "stroke_outline/settings.hpp"
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline
{
	// A zero-length dash: where it sits and the path's direction there. Drawn
	// as a dot by round/square caps (e.g. a [0, gap] pattern with round caps),
	// nothing by butt caps.
	struct DashDot
	{
		Vec2 position;
		Vec2 tangent;  // unit vector
	};

	// Split `commands` into dashes (open subpaths). Zero-length dashes are
	// returned in `dots` instead. The pattern restarts at every subpath. If the
	// pattern would produce an unbounded number of dashes, the path is returned
	// undashed.
	std::vector<PathCommand> dash_path(
		const std::vector<PathCommand>& commands,
		const DashSettings& settings,
		std::vector<DashDot>& dots);
}
