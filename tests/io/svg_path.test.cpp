// SVG path parsing and formatting (src/io/svg_path.cpp): the internal
// parser/formatter, parse errors, stroking SVG data and output precision.
#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <string>

#include "bezier/bezier.hpp"
#include "io/svg_path.hpp"
#include "stroke_outline/PathCommand.hpp"
#include "stroke_outline/stroke_outline.hpp"

using namespace stroke_outline;

namespace
{
	char verb_of(const PathCommand& c)
	{
		return std::visit(
			[](auto&& x) -> char {
				using T = std::decay_t<decltype(x)>;
				if constexpr (std::is_same_v<T, MoveTo>)
					return 'M';
				else if constexpr (std::is_same_v<T, LineTo>)
					return 'L';
				else if constexpr (std::is_same_v<T, QuadraticTo>)
					return 'Q';
				else if constexpr (std::is_same_v<T, CubicTo>)
					return 'C';
				else
					return 'Z';
			},
			c);
	}

	std::string verbs_of(const std::vector<PathCommand>& cmds)
	{
		std::string s;
		for (const auto& c: cmds)
			s.push_back(verb_of(c));
		return s;
	}
}

TEST_CASE("parse_svg_path reads absolute M/L/C/Q/Z")
{
	const auto p = parse_svg_path("M 0 0 L 10 0 Q 15 5 10 10 C 8 12 2 12 0 10 Z");
	CHECK(verbs_of(p) == "MLQCZ");
	CHECK(std::get<MoveTo>(p[0]).p == Vec2 { 0, 0 });
	CHECK(std::get<LineTo>(p[1]).p == Vec2 { 10, 0 });
	CHECK(std::get<QuadraticTo>(p[2]).control == Vec2 { 15, 5 });
	CHECK(std::get<CubicTo>(p[3]).end == Vec2 { 0, 10 });
}

TEST_CASE("parse_svg_path handles relative commands and H/V")
{
	// m/h/v are relative; the pen starts at (10,10) and traces a 80x80 box.
	const auto p = parse_svg_path("m10 10 h80 v80 h-80 z");
	CHECK(verbs_of(p) == "MLLLZ");
	CHECK(std::get<MoveTo>(p[0]).p == Vec2 { 10, 10 });
	CHECK(std::get<LineTo>(p[1]).p == Vec2 { 90, 10 });  // h80
	CHECK(std::get<LineTo>(p[2]).p == Vec2 { 90, 90 });  // v80
	CHECK(std::get<LineTo>(p[3]).p == Vec2 { 10, 90 });  // h-80
}

TEST_CASE("parse_svg_path expands implicit repeated M into L")
{
	// A MoveTo followed by extra coordinate pairs: the extras become LineTos.
	const auto p = parse_svg_path("M 0 0 10 10 20 0");
	CHECK(verbs_of(p) == "MLL");
	CHECK(std::get<LineTo>(p[1]).p == Vec2 { 10, 10 });
	CHECK(std::get<LineTo>(p[2]).p == Vec2 { 20, 0 });
}

TEST_CASE("parse_svg_path reflects the control point for smooth curves")
{
	// C then S: the S's first control point is the reflection of the C's second
	// control point (5,-5) about the join (10,0) -> (15,5).
	const auto p = parse_svg_path("M 0 0 C 0 -5 5 -5 10 0 S 20 5 20 0");
	REQUIRE(verbs_of(p) == "MCC");
	CHECK(std::get<CubicTo>(p[2]).control1 == Vec2 { 15, 5 });
}

TEST_CASE("parse_svg_path converts an elliptical arc to cubics")
{
	// Semicircle from (0,0) to (100,0), radius 50, sweep=1. It splits into two
	// quarter-arc cubics whose shared join sits at the top of the arc.
	const auto p = parse_svg_path("M 0 0 A 50 50 0 0 1 100 0");
	REQUIRE(verbs_of(p) == "MCC");

	const Vec2 join = std::get<CubicTo>(p[1]).end;  // top of the semicircle
	const Vec2 end = std::get<CubicTo>(p[2]).end;   // arc endpoint
	CHECK(join.x == doctest::Approx(50.0).epsilon(0.01));
	CHECK(std::abs(join.y) == doctest::Approx(50.0).epsilon(0.01));
	CHECK(end.x == doctest::Approx(100.0).epsilon(0.01));
	CHECK(end.y == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("parse_svg_path treats a zero-radius arc as a line")
{
	const auto p = parse_svg_path("M 0 0 A 0 0 0 0 1 10 10");
	CHECK(verbs_of(p) == "ML");
	CHECK(std::get<LineTo>(p[1]).p == Vec2 { 10, 10 });
}

TEST_CASE("format_svg_path round-trips through parse and is stable")
{
	const std::string d = "M 0 0 C 0 -5 5 -5 10 0 Q 12 3 10 8 Z";
	const std::string once = format_svg_path(parse_svg_path(d));
	const std::string twice = format_svg_path(parse_svg_path(once));
	CHECK(once == twice);
	// The formatter emits absolute commands with trimmed decimals.
	CHECK(once == "M 0 0 C 0 -5 5 -5 10 0 Q 12 3 10 8 Z");
}

TEST_CASE("build_outline strokes SVG data, the same as its parsed Path")
{
	const std::string d = "M 10 10 L 90 10 L 90 90 Z";
	const std::string out = build_outline(d, { .line_width = 8.0 }).to_svg();
	CHECK(out == build_outline(Path::from_svg(d), { .line_width = 8.0 }).to_svg());
	CHECK(!out.empty());
	CHECK(out.front() == 'M');
	CHECK(std::count(out.begin(), out.end(), 'M') == 2);
	CHECK(std::count(out.begin(), out.end(), 'Z') == 2);
}

TEST_CASE("from_svg reports no error for valid path data")
{
	for (const char* d:
		 { "M 0 0 L 10 10 Z", "m1 2 3 4", "M.5.5L10-10", "M 0 0 a10 10 0 1110 10", "" })
	{
		CAPTURE(std::string(d));
		std::optional<SvgParseError> error;
		Path::from_svg(d, error);
		CHECK(!error);
	}
}

TEST_CASE("from_svg reports where and why parsing stopped, keeping what came before")
{
	struct Case
	{
		std::string d;
		std::string at;  // the position the message must give
		std::string message_part;
		std::string kept;  // what still parsed, as SVG
	};
	const Case cases[] = {
		{ "M 0 0 L 10 10 X 5 5", "at position 14", "unknown command 'X'", "M 0 0 L 10 10" },
		{ "M 0 0 L 10 10 L 20",
		  "at position 18 (end of data)",
		  "expected a number",
		  "M 0 0 L 10 10" },
		{ "M 0 0 A 10 10 0 2 0 20 0", "at position 16", "arc flag", "M 0 0" },
		{ "M 0 0 L 1e999 0", "at position 8", "expected a number", "M 0 0" },
		// Data must begin with a moveto; anything else renders nothing.
		{ "L 10 10", "at position 0", "moveto", "" },
		{ "10 10", "at position 0", "moveto", "" },
		{ "hello", "at position 0", "moveto", "" },
	};
	for (const Case& c: cases)
	{
		CAPTURE(c.d);
		std::optional<SvgParseError> error;
		const Path p = Path::from_svg(c.d, error);
		REQUIRE(error);
		CHECK(error->message.find(c.message_part) != std::string::npos);
		CHECK(error->message.ends_with(c.at));
		CHECK(p.to_svg() == c.kept);
	}
}

TEST_CASE("build_outline on SVG data reports parse errors and strokes what parsed")
{
	std::optional<SvgParseError> error;
	const std::string out =
		build_outline("M 0 0 L 100 0 X", { .line_width = 10.0 }, error).to_svg();
	REQUIRE(error);
	CHECK(error->message == "unknown command 'X' at position 14");
	CHECK(out == build_outline("M 0 0 L 100 0", { .line_width = 10.0 }).to_svg());

	build_outline("M 0 0 L 100 0", { .line_width = 10.0 }, error);
	CHECK(!error);  // a valid parse clears it
}

TEST_CASE("parse error messages say where parsing stopped")
{
	std::optional<SvgParseError> error;
	Path::from_svg("M 0 0 L 10 X", error);
	REQUIRE(error);
	CHECK(error->message == "expected a number at position 11");

	// Data that ends too early: the position is the end of the string.
	Path::from_svg("M 0 0 L 10", error);
	REQUIRE(error);
	CHECK(error->message == "expected a number at position 10 (end of data)");
}

TEST_CASE("to_svg keeps a path's detail whatever its size")
{
	SUBCASE("with the default precision, paths 10 units or larger are unaffected")
	{
		CHECK(Path {}.move_to(0, 0).line_to(100.123456, 0).to_svg() == "M 0 0 L 100.123 0");
	}
	SUBCASE("precision is a minimum: a coarser request is raised to ~1e-4 of the size")
	{
		// A 100-unit path needs 2 decimals; asking for 1 gets 2.
		CHECK(Path {}.move_to(0, 0).line_to(100.123456, 0).to_svg(1) == "M 0 0 L 100.12 0");
		CHECK(Path {}.move_to(0, 0).line_to(100.123456, 0).to_svg(5) == "M 0 0 L 100.12346 0");
	}
	SUBCASE("smaller paths get the decimals they need (rounding <= ~1e-4 of their size)")
	{
		// Size 1: at least 4 decimals.
		CHECK(Path {}.move_to(0, 0).line_to(1.234567, 0).to_svg() == "M 0 0 L 1.2346 0");
		// Size ~1e-4: at 3 decimals this used to print as "M 0 0 L 0 0".
		CHECK(Path {}.move_to(0, 0).line_to(0.0001234, 0).to_svg() == "M 0 0 L 0.0001234 0");
	}
	SUBCASE("a stroked tiny path round-trips through SVG intact")
	{
		const std::string d =
			build_outline("M 0 0 L 0.01 0 L 0.01 0.01", { .line_width = 0.001 }).to_svg();
		const std::string again = Path::from_svg(d).to_svg();
		CHECK(d == again);
		CHECK(d.find("0.0005") != std::string::npos);  // the half-width offsets survive
	}
}

TEST_CASE("parse_svg_path reflects the control point for smooth quadratics (T)")
{
	// T's control is the previous Q control reflected about the current point;
	// after anything but Q/T it is the current point itself.
	const auto p = parse_svg_path("M 0 0 Q 10 10 20 0 T 40 0");
	REQUIRE(p.size() == 3);
	const auto& t = std::get<QuadraticTo>(p[2]);
	CHECK(t.control.x == doctest::Approx(30));
	CHECK(t.control.y == doctest::Approx(-10));

	const auto alone = parse_svg_path("M 5 5 T 40 0");
	REQUIRE(alone.size() == 2);
	CHECK(std::get<QuadraticTo>(alone[1]).control.x == doctest::Approx(5));
	CHECK(std::get<QuadraticTo>(alone[1]).control.y == doctest::Approx(5));
}

TEST_CASE("parse_svg_path reads numbers with exponents")
{
	const auto p = parse_svg_path("M 1e+1 2E-1 L 1.5e1 0");
	REQUIRE(p.size() == 2);
	CHECK(std::get<MoveTo>(p[0]).p.x == doctest::Approx(10));
	CHECK(std::get<MoveTo>(p[0]).p.y == doctest::Approx(0.2));
	CHECK(std::get<LineTo>(p[1]).p.x == doctest::Approx(15));
}

TEST_CASE("every command reports a missing number, keeping what came before")
{
	// "1e" with no exponent digits is the number 1 followed by a stray "e".
	for (
		const char* d:
		{ "M 0 0 H",
		  "M 0 0 V",
		  "M 0 0 C 1 2 3",
		  "M 0 0 S 1 2",
		  "M 0 0 Q 1 2",
		  "M 0 0 T",
		  "M 0 0 L 1",
		  "M 0 0 1" })
	{
		CAPTURE(std::string(d));
		std::optional<SvgParseError> error;
		const auto p = parse_svg_path(d, error);
		REQUIRE(error);
		CHECK(error->message.find("expected a number") != std::string::npos);
		CHECK(p.size() == 1);  // the M
	}
	std::optional<SvgParseError> error;
	parse_svg_path("M 1e 2", error);
	REQUIRE(error);
	CHECK(error->message.ends_with("at position 3"));
}
