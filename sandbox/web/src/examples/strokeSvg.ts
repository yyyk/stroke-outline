import { so } from '../lib';
import { Color, drawLabel, drawSectionTitle } from '../utils';

// Showcases strings end to end: an SVG path "d" string goes into buildOutline,
// toSVG() gives the outline back as an SVG "d" string, and the browser renders
// both directly via Path2D.
// Each input exercises a different bit of the SVG grammar (arcs, smooth curves).
const SHAPES = [
	{
		name: 'Arc (A) — open',
		d: 'M 20 80 L 55 80 A 30 30 0 0 1 55 20',
		lineWidth: 12,
	},
	{
		name: 'Rounded box (H/V/A)',
		d: 'M 30 20 H 80 A 15 15 0 0 1 95 35 V 75 A 15 15 0 0 1 80 90 H 30 A 15 15 0 0 1 15 75 V 35 A 15 15 0 0 1 30 20 Z',
		lineWidth: 8,
	},
	{
		name: 'Smooth curves (C/S)',
		d: 'M 20 55 C 20 30 50 30 50 55 S 80 80 80 55 C 80 30 50 30 50 55 S 20 80 20 55 Z',
		lineWidth: 6,
	},
];

const CELL_WIDTH = 260;

function renderCell(ctx: CanvasRenderingContext2D, d: string, lineWidth: number) {
	// The whole trip is strings: SVG in -> SVG out.
	const outline = so
		.buildOutline(d, { lineWidth, lineJoin: 'round', lineCap: 'round' })
		.toSVG();

	// Filled outline + red boundary, rendered straight from the returned string.
	const outlinePath = new Path2D(outline);
	ctx.fillStyle = '#c7c7c7';
	ctx.fill(outlinePath, 'nonzero');
	ctx.strokeStyle = Color.Red;
	ctx.lineWidth = 1;
	ctx.stroke(outlinePath);

	// Input path centerline for reference.
	ctx.strokeStyle = 'rgba(0, 0, 0, 0.45)';
	ctx.lineWidth = 1;
	ctx.stroke(new Path2D(d));

	drawLabel(ctx, `${outline.length} chars`, 0, 110, Color.Red);
}

export function strokeSvg(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(
		ctx,
		'Stroke from SVG string (buildOutline(d).toSVG() — string in, string out)',
	);

	ctx.save();
	ctx.translate(0, 60);
	SHAPES.forEach((shape, i) => {
		ctx.save();
		ctx.translate(i * CELL_WIDTH, 0);
		drawLabel(ctx, shape.name, 0, -10, Color.Red);
		renderCell(ctx, shape.d, shape.lineWidth);
		ctx.restore();
	});
	ctx.restore();
}
