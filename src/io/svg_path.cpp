#include "io/svg_path.hpp"

#include <array>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core.hpp"
#include "io/svg_arc.hpp"
#include "math/Eps.hpp"
#include "stroke_outline/path.hpp"

using namespace stroke_outline;

namespace
{
	// ---- Parser ---------------------------------------------------------------

	// A small scanner over an SVG path "d" string. Numbers are separated by
	// whitespace and/or commas; a sign or a leading '.' can also start a new
	// number without any separator (e.g. "1.5.5" is two numbers, "10-20" is two).
	struct Scanner
	{
		std::string_view s;
		std::size_t i = 0;

		bool at_end() const { return i >= s.size(); }

		void skip_separators()
		{
			while (i < s.size())
			{
				const char c = s[i];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == ',')
					++i;
				else
					break;
			}
		}

		// Peek past separators: is the next token the start of a number?
		bool has_number()
		{
			const std::size_t save = i;
			skip_separators();
			const bool yes =
				i < s.size()
				&& (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '+' || s[i] == '-'
					|| s[i] == '.');
			i = save;
			return yes;
		}

		// Parse a number in SVG syntax. Returns false (and leaves i put) if the
		// next token isn't a number.
		bool number(double& out)
		{
			skip_separators();
			const std::size_t start = i;
			if (i < s.size() && (s[i] == '+' || s[i] == '-'))
				++i;
			bool digits = false;
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
			{
				++i;
				digits = true;
			}
			if (i < s.size() && s[i] == '.')
			{
				++i;
				while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
				{
					++i;
					digits = true;
				}
			}
			if (!digits)
			{
				i = start;
				return false;
			}
			if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
			{
				const std::size_t save = i;
				++i;
				if (i < s.size() && (s[i] == '+' || s[i] == '-'))
					++i;
				bool exp_digits = false;
				while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
				{
					++i;
					exp_digits = true;
				}
				if (!exp_digits)
					i = save;  // "e" wasn't part of a valid exponent
			}
			// strtod (not std::from_chars<double>, which Emscripten's libc++
			// leaves unimplemented) on a null-terminated copy of the token.
			const std::string token(s.data() + start, i - start);
			char* endp = nullptr;
			out = std::strtod(token.c_str(), &endp);
			if (endp == token.c_str() || !std::isfinite(out))
			{
				i = start;
				return false;
			}
			return true;
		}

		// Arc flags are a single '0' or '1' and may be packed with no separator
		// (e.g. "...0 1 50..."), so they get their own one-character scan.
		bool flag(bool& out)
		{
			skip_separators();
			if (i < s.size() && (s[i] == '0' || s[i] == '1'))
			{
				out = s[i] == '1';
				++i;
				return true;
			}
			return false;
		}
	};

	// Reflection of the previous control point about `cur`, used by smooth
	// curve commands (S/T) to make the join tangent-continuous.
	Vec2 reflect(Vec2 cur, Vec2 prev_control)
	{
		return { 2.0 * cur.x - prev_control.x, 2.0 * cur.y - prev_control.y };
	}
}

namespace stroke_outline
{
	std::vector<PathCommand>
		parse_svg_path(std::string_view d, std::optional<SvgParseError>& error)
	{
		error.reset();
		std::vector<PathCommand> out;
		Scanner sc { d };

		// Stop at the first error, keeping what parsed before it (SVG's error
		// handling: render up to the error). The message includes the position,
		// so it reads on its own: "expected a number at position 11".
		const auto fail = [&](std::string message) {
			sc.skip_separators();
			message += " at position " + std::to_string(sc.i);
			if (sc.at_end())
				message += " (end of data)";
			error = SvgParseError { std::move(message) };
			return out;
		};

		Vec2 cur { 0.0, 0.0 };    // current point
		Vec2 start { 0.0, 0.0 };  // subpath start (target of Z)
		Vec2 last_cubic_ctrl { 0.0, 0.0 };
		Vec2 last_quad_ctrl { 0.0, 0.0 };
		char cmd = 0;       // command currently being repeated
		char prev_cmd = 0;  // last command actually emitted (for S/T reflection)

		const auto read_pair = [&](Vec2& p) {
			double x, y;
			if (!sc.number(x) || !sc.number(y))
				return false;
			p = { x, y };
			return true;
		};

		while (true)
		{
			sc.skip_separators();
			if (sc.at_end())
				break;

			const char c = sc.s[sc.i];
			if (std::isalpha(static_cast<unsigned char>(c)))
			{
				if (out.empty() && c != 'M' && c != 'm')
					return fail("path data must start with a moveto (M or m)");
				cmd = c;
				++sc.i;
			}
			else if (cmd == 0)
			{
				return fail(
					out.empty() ?
						"path data must start with a moveto (M or m)" :
						"expected a command");
			}
			// else: implicit repeat of the previous command (numbers only).

			const bool rel = std::islower(static_cast<unsigned char>(cmd));
			const Vec2 origin = rel ? cur : Vec2 { 0.0, 0.0 };

			switch (std::toupper(static_cast<unsigned char>(cmd)))
			{
				case 'M':
				{
					Vec2 p;
					if (!read_pair(p))
						return fail("expected a number");
					cur = origin + p;
					start = cur;
					out.push_back(MoveTo { cur });
					// Subsequent coordinate pairs after an M are implicit L/l.
					while (sc.has_number())
					{
						Vec2 q;
						if (!read_pair(q))
							return fail("expected a number");
						cur = (rel ? cur : Vec2 { 0.0, 0.0 }) + q;
						out.push_back(LineTo { cur });
					}
					prev_cmd = rel ? 'l' : 'L';
					cmd = 0;  // handled the whole run inline
					break;
				}
				case 'L':
				{
					Vec2 p;
					if (!read_pair(p))
						return fail("expected a number");
					cur = origin + p;
					out.push_back(LineTo { cur });
					prev_cmd = cmd;
					break;
				}
				case 'H':
				{
					double x;
					if (!sc.number(x))
						return fail("expected a number");
					cur = { (rel ? cur.x : 0.0) + x, cur.y };
					out.push_back(LineTo { cur });
					prev_cmd = cmd;
					break;
				}
				case 'V':
				{
					double y;
					if (!sc.number(y))
						return fail("expected a number");
					cur = { cur.x, (rel ? cur.y : 0.0) + y };
					out.push_back(LineTo { cur });
					prev_cmd = cmd;
					break;
				}
				case 'C':
				{
					Vec2 c1, c2, end;
					if (!read_pair(c1) || !read_pair(c2) || !read_pair(end))
						return fail("expected a number");
					c1 = origin + c1;
					c2 = origin + c2;
					end = origin + end;
					out.push_back(CubicTo { c1, c2, end });
					cur = end;
					last_cubic_ctrl = c2;
					prev_cmd = cmd;
					break;
				}
				case 'S':
				{
					Vec2 c2, end;
					if (!read_pair(c2) || !read_pair(end))
						return fail("expected a number");
					c2 = origin + c2;
					end = origin + end;
					const char up = std::toupper(static_cast<unsigned char>(prev_cmd));
					const Vec2 c1 =
						(up == 'C' || up == 'S') ? reflect(cur, last_cubic_ctrl) : cur;
					out.push_back(CubicTo { c1, c2, end });
					cur = end;
					last_cubic_ctrl = c2;
					prev_cmd = cmd;
					break;
				}
				case 'Q':
				{
					Vec2 ctrl, end;
					if (!read_pair(ctrl) || !read_pair(end))
						return fail("expected a number");
					ctrl = origin + ctrl;
					end = origin + end;
					out.push_back(QuadraticTo { ctrl, end });
					cur = end;
					last_quad_ctrl = ctrl;
					prev_cmd = cmd;
					break;
				}
				case 'T':
				{
					Vec2 end;
					if (!read_pair(end))
						return fail("expected a number");
					end = origin + end;
					const char up = std::toupper(static_cast<unsigned char>(prev_cmd));
					const Vec2 ctrl =
						(up == 'Q' || up == 'T') ? reflect(cur, last_quad_ctrl) : cur;
					out.push_back(QuadraticTo { ctrl, end });
					cur = end;
					last_quad_ctrl = ctrl;
					prev_cmd = cmd;
					break;
				}
				case 'A':
				{
					double rx, ry, rot;
					bool large, sweep;
					Vec2 end;
					if (!sc.number(rx) || !sc.number(ry) || !sc.number(rot))
						return fail("expected a number");
					if (!sc.flag(large) || !sc.flag(sweep))
						return fail("expected an arc flag (0 or 1)");
					if (!read_pair(end))
						return fail("expected a number");
					end = origin + end;
					const auto cubics = svg::arc_to_cubics(cur, rx, ry, rot, large, sweep, end);
					if (cubics.empty())
					{
						// Degenerate arc: straight line (or nothing if end == cur).
						if (end.x != cur.x || end.y != cur.y)
							out.push_back(LineTo { end });
					}
					else
					{
						for (const auto& cubic: cubics)
							out.push_back(cubic);
					}
					cur = end;
					prev_cmd = cmd;
					break;
				}
				case 'Z':
				{
					out.push_back(Close {});
					cur = start;
					prev_cmd = cmd;
					cmd = 0;  // Z takes no coordinates; don't try to repeat it
					break;
				}
				default:
					--sc.i;  // point at the letter itself
					return fail(std::string("unknown command '") + cmd + "'");
			}
		}

		return out;
	}

	std::vector<PathCommand> parse_svg_path(std::string_view d)
	{
		std::optional<SvgParseError> ignored;
		return parse_svg_path(d, ignored);
	}

	// ---- Formatter ------------------------------------------------------------

	std::string format_svg_path(const std::vector<PathCommand>& commands, int precision)
	{
		std::string out;

		// `precision` is a minimum number of decimals, raised when needed so
		// rounding never exceeds ~1e-4 of the path's size: with a fixed count, a
		// path 0.01 across at 3 decimals would round each coordinate by up to 10%
		// of its size. With the default of 3, paths 10 units or larger are
		// unaffected.
		double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
		bool any = false;
		const auto grow = [&](Vec2 p) {
			if (!any)
			{
				min_x = max_x = p.x;
				min_y = max_y = p.y;
				any = true;
			}
			min_x = std::min(min_x, p.x), max_x = std::max(max_x, p.x);
			min_y = std::min(min_y, p.y), max_y = std::max(max_y, p.y);
		};
		for (const auto& c: commands)
			std::visit(
				[&](auto&& x) {
					using T = std::decay_t<decltype(x)>;
					if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
						grow(x.p);
					else if constexpr (std::is_same_v<T, QuadraticTo>)
					{
						grow(x.control);
						grow(x.end);
					}
					else if constexpr (std::is_same_v<T, CubicTo>)
					{
						grow(x.control1);
						grow(x.control2);
						grow(x.end);
					}
				},
				c);
		const double extent = std::max(max_x - min_x, max_y - min_y);
		int decimals = std::max(precision, 0);
		if (extent > 0.0 && std::isfinite(extent))
		{
			// Decimals for rounding <= svg_rounding × extent (4 − log10(extent) for
			// 1e-4). The constant's digit count is rounded to an exact integer so
			// a path whose size is a power of ten doesn't pick up an extra digit.
			const double rounding_digits =
				static_cast<double>(std::lround(-std::log10(EPSILON.svg_rounding)));
			decimals = std::max(
				decimals,
				static_cast<int>(std::ceil(rounding_digits - std::log10(extent))));
		}
		decimals = std::min(decimals, 17);

		const auto append_num = [&](double v) {
			char buf[400];
			std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
			std::string_view sv { buf };
			// Trim trailing zeros (and a dangling '.') from the fixed-point text.
			if (sv.find('.') != std::string_view::npos)
			{
				std::size_t end = sv.size();
				while (end > 0 && sv[end - 1] == '0')
					--end;
				if (end > 0 && sv[end - 1] == '.')
					--end;
				sv = sv.substr(0, end);
			}
			// Normalize "-0" to "0".
			if (sv == "-0")
				sv = "0";
			out.append(sv);
		};

		const auto emit = [&](char letter, std::initializer_list<Vec2> pts) {
			if (!out.empty())
				out.push_back(' ');
			out.push_back(letter);
			for (const Vec2 p: pts)
			{
				out.push_back(' ');
				append_num(p.x);
				out.push_back(' ');
				append_num(p.y);
			}
		};

		for (const auto& c: commands)
			std::visit(
				[&](auto&& x) {
					using T = std::decay_t<decltype(x)>;
					if constexpr (std::is_same_v<T, MoveTo>)
						emit('M', { x.p });
					else if constexpr (std::is_same_v<T, LineTo>)
						emit('L', { x.p });
					else if constexpr (std::is_same_v<T, QuadraticTo>)
						emit('Q', { x.control, x.end });
					else if constexpr (std::is_same_v<T, CubicTo>)
						emit('C', { x.control1, x.control2, x.end });
					else
					{
						if (!out.empty())
							out.push_back(' ');
						out.push_back('Z');
					}
				},
				c);

		return out;
	}
}
