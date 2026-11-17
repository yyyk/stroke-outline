// Times build_outline on the inputs in bench/inputs/, the same way as the
// JavaScript benchmark (js/bench/bench.mjs), and prints one line per input and
// settings: the time to stroke all of the input's paths once (the median of 7
// runs, each repeated for at least 100 ms), and its segments in and out.
#include <stroke_outline/stroke_outline.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace stroke_outline;

namespace
{
	std::vector<std::string> read_lines(const std::string& file)
	{
		std::ifstream in(file);
		std::vector<std::string> lines;
		for (std::string line; std::getline(in, line);)
			if (!line.empty())
				lines.push_back(line);
		return lines;
	}

	// Lines, quadratics and cubics: the drawn segments.
	int count_segments(const Path& path)
	{
		int n = 0;
		Path::Sink sink;
		sink.line_to = [&](double, double) { ++n; };
		sink.quad_to = [&](double, double, double, double) { ++n; };
		sink.cubic_to = [&](double, double, double, double, double, double) { ++n; };
		path.for_each(sink);
		return n;
	}

	struct Variant
	{
		const char* name;
		StrokeSettings (*make)(double width);
	};

	const Variant variants[] = {
		{ "default", [](double w) { return StrokeSettings { .line_width = w }; } },
		{ "round",
		  [](double w) {
			  return StrokeSettings {
				  .line_width = w,
				  .line_join = LineJoin::Round,
				  .line_cap = LineCap::Round,
			  };
		  } },
		{ "dashed",
		  [](double w) {
			  return StrokeSettings {
				  .line_width = w,
				  .dash = DashSettings { .dash_pattern = { 4 * w, 2 * w } },
			  };
		  } },
		{ "clipToShape",
		  [](double w) { return StrokeSettings { .line_width = w, .clip_to_shape = true }; } },
	};
}

int main()
{
	const std::string dir = STROKE_OUTLINE_BENCH_INPUTS;
	struct Input
	{
		const char* name;
		double width;
	};
	const Input inputs[] = { { "glyphs", 0.5 }, { "icons", 2 }, { "freehand", 4 } };

	std::printf("input     settings     time (us)  segments in  segments out\n");
	for (const auto& input: inputs)
	{
		std::vector<Path> paths;
		for (const auto& d: read_lines(dir + "/" + input.name + ".txt"))
			paths.push_back(Path::from_svg(d));
		int in = 0;
		for (const auto& p: paths)
			in += count_segments(p);

		for (const auto& variant: variants)
		{
			const StrokeSettings settings = variant.make(input.width);
			int out = 0;
			const auto stroke_all = [&] {
				int n = 0;
				for (const auto& p: paths)
					n += count_segments(build_outline(p, settings));
				return n;
			};
			out = stroke_all();  // warm up
			std::vector<double> runs;
			for (int r = 0; r < 7; ++r)
			{
				using clock = std::chrono::steady_clock;
				int reps = 0;
				const auto start = clock::now();
				std::chrono::duration<double, std::micro> elapsed {};
				do
				{
					stroke_all();
					++reps;
					elapsed = clock::now() - start;
				}
				while (elapsed.count() < 100000.0);
				runs.push_back(elapsed.count() / reps);
			}
			std::sort(runs.begin(), runs.end());
			std::printf(
				"%-9s %-12s %9.1f  %11d  %12d\n",
				input.name,
				variant.name,
				runs[3],
				in,
				out);
		}
	}
}
