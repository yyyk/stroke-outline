#pragma once
#include <variant>
#include "stroke_outline/Vec2.hpp"

namespace stroke_outline
{
	struct MoveTo
	{
		Vec2 p;
	};
	struct LineTo
	{
		Vec2 p;
	};
	struct QuadraticTo
	{
		Vec2 control;
		Vec2 end;
	};
	struct CubicTo
	{
		Vec2 control1;
		Vec2 control2;
		Vec2 end;
	};
	struct Close
	{};

	using PathCommand = std::variant<MoveTo, LineTo, QuadraticTo, CubicTo, Close>;
}
