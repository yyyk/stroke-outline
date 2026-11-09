// Port of svg-arc-to-cubic-bezier, Copyright (c) 2017, Colin Meinke (ISC
// license); see THIRD_PARTY_NOTICES and io/svg_arc.hpp.
#include "io/svg_arc.hpp"
#include "math/Eps.hpp"

#include <array>
#include <cmath>
#include <numbers>

using namespace stroke_outline;

namespace
{
	constexpr double TAU = 2.0 * std::numbers::pi;

	// Map a point on the unit circle onto the actual (rotated, scaled, centered)
	// ellipse.
	Vec2 map_to_ellipse(Vec2 p, double rx, double ry, double cosphi, double sinphi, Vec2 center)
	{
		const double x = p.x * rx;
		const double y = p.y * ry;
		return { cosphi * x - sinphi * y + center.x, sinphi * x + cosphi * y + center.y };
	}

	// Control points of a single unit-circle arc segment spanning [ang1, ang1+ang2],
	// returned as { control1, control2, end }. `ang2` is the (signed) sweep and is
	// always <= 90 degrees by the time this is called.
	std::array<Vec2, 3> approx_unit_arc(double ang1, double ang2)
	{
		// For an exact quarter turn, Spencer Mortensen's constant is more precise
		// than the general k = 4/3 * tan(θ/4).
		const double a =
			ang2 == 1.5707963267948966 ?
				0.551915024494 :
			ang2 == -1.5707963267948966 ?
				-0.551915024494 :
				4.0 / 3.0 * std::tan(ang2 / 4.0);

		const double x1 = std::cos(ang1);
		const double y1 = std::sin(ang1);
		const double x2 = std::cos(ang1 + ang2);
		const double y2 = std::sin(ang1 + ang2);

		return {
			Vec2 { x1 - y1 * a, y1 + x1 * a },
			Vec2 { x2 + y2 * a, y2 - x2 * a },
			Vec2 { x2, y2 }
		};
	}

	// Signed angle from vector (ux,uy) to (vx,vy).
	double vector_angle(double ux, double uy, double vx, double vy)
	{
		const double sign = (ux * vy - uy * vx < 0.0) ? -1.0 : 1.0;
		double dot = ux * vx + uy * vy;
		dot = std::clamp(dot, -1.0, 1.0);
		return sign * std::acos(dot);
	}

	// Endpoint -> center parameterization: recover the ellipse center and the
	// arc's start angle / sweep from the SVG-style endpoint form. (px,py)/(cx,cy)
	// are the arc endpoints; (pxp,pyp) is the start point in the ellipse's
	// (rotated, half-difference) frame.
	struct ArcCenter
	{
		Vec2 center;
		double ang1, ang2;
	};

	ArcCenter get_arc_center(
		Vec2 p,
		Vec2 c,
		double rx,
		double ry,
		bool large_arc_flag,
		bool sweep_flag,
		double sinphi,
		double cosphi,
		double pxp,
		double pyp)
	{
		const double rxsq = rx * rx;
		const double rysq = ry * ry;
		const double pxpsq = pxp * pxp;
		const double pypsq = pyp * pyp;

		double radicant = rxsq * rysq - rxsq * pypsq - rysq * pxpsq;
		if (radicant < 0.0)
			radicant = 0.0;
		radicant /= rxsq * pypsq + rysq * pxpsq;
		radicant = std::sqrt(radicant) * (large_arc_flag == sweep_flag ? -1.0 : 1.0);

		const double centerxp = radicant * rx / ry * pyp;
		const double centeryp = radicant * -ry / rx * pxp;

		const Vec2 center {
			cosphi * centerxp - sinphi * centeryp + (p.x + c.x) / 2.0,
			sinphi * centerxp + cosphi * centeryp + (p.y + c.y) / 2.0
		};

		const double vx1 = (pxp - centerxp) / rx;
		const double vy1 = (pyp - centeryp) / ry;
		const double vx2 = (-pxp - centerxp) / rx;
		const double vy2 = (-pyp - centeryp) / ry;

		double ang1 = vector_angle(1.0, 0.0, vx1, vy1);
		double ang2 = vector_angle(vx1, vy1, vx2, vy2);

		if (!sweep_flag && ang2 > 0.0)
			ang2 -= TAU;
		if (sweep_flag && ang2 < 0.0)
			ang2 += TAU;

		return { center, ang1, ang2 };
	}
}

namespace stroke_outline::svg
{
	std::vector<CubicTo> arc_to_cubics(
		Vec2 from,
		double rx,
		double ry,
		double x_axis_rotation_deg,
		bool large_arc_flag,
		bool sweep_flag,
		Vec2 to)
	{
		std::vector<CubicTo> curves;

		if (rx == 0.0 || ry == 0.0)
			return curves;

		const double sinphi = std::sin(x_axis_rotation_deg * TAU / 360.0);
		const double cosphi = std::cos(x_axis_rotation_deg * TAU / 360.0);

		const double pxp = cosphi * (from.x - to.x) / 2.0 + sinphi * (from.y - to.y) / 2.0;
		const double pyp = -sinphi * (from.x - to.x) / 2.0 + cosphi * (from.y - to.y) / 2.0;

		if (pxp == 0.0 && pyp == 0.0)
			return curves;

		rx = std::abs(rx);
		ry = std::abs(ry);

		// If the radii are too small to span the endpoints, scale them up to fit
		// (SVG spec F.6.6 radius correction).
		const double lambda = pxp * pxp / (rx * rx) + pyp * pyp / (ry * ry);
		if (lambda > 1.0)
		{
			rx *= std::sqrt(lambda);
			ry *= std::sqrt(lambda);
		}

		auto [center, ang1, ang2] = get_arc_center(
			from,
			to,
			rx,
			ry,
			large_arc_flag,
			sweep_flag,
			sinphi,
			cosphi,
			pxp,
			pyp);

		// Split into segments each spanning at most a quarter turn.
		double ratio = std::abs(ang2) / (TAU / 4.0);
		if (std::abs(1.0 - ratio) < EPSILON.param)
			ratio = 1.0;
		const int segments = std::max(static_cast<int>(std::ceil(ratio)), 1);
		ang2 /= segments;

		curves.reserve(segments);
		for (int i = 0; i < segments; ++i)
		{
			const auto unit = approx_unit_arc(ang1, ang2);
			const Vec2 c1 = map_to_ellipse(unit[0], rx, ry, cosphi, sinphi, center);
			const Vec2 c2 = map_to_ellipse(unit[1], rx, ry, cosphi, sinphi, center);
			const Vec2 end = map_to_ellipse(unit[2], rx, ry, cosphi, sinphi, center);
			curves.push_back(CubicTo { c1, c2, end });
			ang1 += ang2;
		}

		return curves;
	}
}
