#include <iostream>

#include "stroke_outline/stroke_outline.hpp"

// Native counterpart to sandbox/web: a tiny runnable demo of the library.
// It uses only the public API (the Path builder + SVG helpers) — it has no
// access to the internal PathCommand/Vec2 types, which proves the public
// surface is self-contained.
int main()
{
	using namespace stroke_outline;

	StrokeSettings settings;
	settings.line_width = 20.0;

	// Build a closed square with the fluent Path builder and stroke it.
	const Path outline = build_outline(
		Path {}.move_to(0, 0).line_to(100, 0).line_to(100, 100).line_to(0, 100).close(),
		settings);

	std::size_t command_count = 0;
	outline.for_each(
		{
			.move_to = [&](double, double) { ++command_count; },
			.line_to = [&](double, double) { ++command_count; },
			.close = [&] { ++command_count; },
		});
	std::cout << "stroked square -> " << command_count << " path commands\n";

	// SVG in, SVG out — including an arc, expanded to cubics internally.
	const std::string svg =
		build_outline("M 10 10 H 90 A 20 20 0 0 1 90 50", settings).to_svg();
	std::cout << "stroked SVG -> " << svg << "\n";

	return 0;
}
