import { pathOf, so, type PathCommand } from '../lib';
import { Color, drawLabel, drawSectionTitle, drawPathCommands } from '../utils';

type Align = 'center' | 'inside' | 'outside';

const ALIGNMENTS: Align[] = ['center', 'inside', 'outside'];
const COLUMN_WIDTH = 160;

// Draw a single alignment variant at the current origin: the filled stroke
// outline (light gray + red edge) plus the centerline path on top, so you
// can see where the band sits relative to the original path.
function renderCell(
	ctx: CanvasRenderingContext2D,
	path: PathCommand[],
	lineWidth: number,
	align: Align,
	lineJoin?: 'miter' | 'round' | 'bevel',
	clipToShape?: boolean,
) {
	const strokeOutline = so.buildOutline(pathOf(path), {
		lineWidth,
		lineJoin: lineJoin || 'miter',
		lineCap: 'butt',
		align,
		clipToShape: clipToShape || false,
	}).commands;

	// Filled stroke band + red outline.
	ctx.fillStyle = '#c7c7c7';
	ctx.strokeStyle = Color.Red;
	ctx.lineWidth = 1;
	drawPathCommands(ctx, strokeOutline);
	ctx.fill('nonzero');
	ctx.stroke();

	// Centerline (the original path) on top.
	ctx.strokeStyle = 'rgba(0, 0, 0, 0.55)';
	ctx.lineWidth = 1;
	drawPathCommands(ctx, path);
	ctx.stroke();

	drawLabel(ctx, `${strokeOutline.length} commands`, 0, 118, Color.Red);
}

// One row = one shape shown under all three alignments.
function renderRow(
	ctx: CanvasRenderingContext2D,
	test: {
		name: string;
		path: PathCommand[];
		lineWidth: number;
		lineJoin?: 'miter' | 'round' | 'bevel';
		clipToShape?: boolean;
	},
) {
	drawLabel(ctx, test.name, 0, -10);
	ALIGNMENTS.forEach((align, i) => {
		ctx.save();
		ctx.translate(i * COLUMN_WIDTH, 0);
		drawLabel(ctx, align, 0, 4, align === 'center' ? 'black' : Color.Blue);
		ctx.save();
		ctx.translate(0, 12);
		renderCell(ctx, test.path, test.lineWidth, align, test.lineJoin, test.clipToShape);
		ctx.restore();
		ctx.restore();
	});
}

export function strokeAlign(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(ctx, 'Stroke Alignment (closed subpaths only)');

	// All shapes are closed; alignment is only meaningful for closed subpaths.
	// Direction (winding) determines which side is "inside" vs "outside".
	const rows: {
		name: string;
		path: PathCommand[];
		lineWidth: number;
		lineJoin?: 'miter' | 'round' | 'bevel';
		clipToShape?: boolean;
	}[] = [
		{
			name: 'Square',
			path: [
				{ type: 'moveTo', x: 15, y: 15 },
				{ type: 'lineTo', x: 95, y: 15 },
				{ type: 'lineTo', x: 95, y: 95 },
				{ type: 'lineTo', x: 15, y: 95 },
				{ type: 'close' },
			],
			lineWidth: 16,
		},
		{
			name: 'Triangle',
			path: [
				{ type: 'moveTo', x: 55, y: 12 },
				{ type: 'lineTo', x: 98, y: 92 },
				{ type: 'lineTo', x: 12, y: 92 },
				{ type: 'close' },
			],
			lineWidth: 14,
			lineJoin: 'round',
		},
		{
			// Rounded rectangle (corner radius 20) with the seam on a straight
			// edge. A common real-world alignment case, and the corner radius
			// stays above the line width so every alignment offsets cleanly.
			name: 'Rounded rect (cubics)',
			path: [
				{ type: 'moveTo', x: 55, y: 15 },
				{ type: 'lineTo', x: 75, y: 15 },
				{
					type: 'cubicTo',
					c1x: 86.05,
					c1y: 15,
					c2x: 95,
					c2y: 23.95,
					x: 95,
					y: 35,
				},
				{ type: 'lineTo', x: 95, y: 75 },
				{
					type: 'cubicTo',
					c1x: 95,
					c1y: 86.05,
					c2x: 86.05,
					c2y: 95,
					x: 75,
					y: 95,
				},
				{ type: 'lineTo', x: 35, y: 95 },
				{
					type: 'cubicTo',
					c1x: 23.95,
					c1y: 95,
					c2x: 15,
					c2y: 86.05,
					x: 15,
					y: 75,
				},
				{ type: 'lineTo', x: 15, y: 35 },
				{
					type: 'cubicTo',
					c1x: 15,
					c1y: 23.95,
					c2x: 23.95,
					c2y: 15,
					x: 35,
					y: 15,
				},
				{ type: 'lineTo', x: 55, y: 15 },
				{ type: 'close' },
			],
			lineWidth: 14,
		},
		{
			// Half-disc: flat base + gentle semicircle (radius 40, well above the
			// stroke width). Isolates the two 90° line↔curve corners (curvature is
			// gentle, so no apex self-intersection) — they should trim cleanly in
			// all three alignments.
			name: 'Half-disc (line + arc)',
			path: [
				{ type: 'moveTo', x: 10, y: 90 },
				{ type: 'lineTo', x: 90, y: 90 },
				{
					type: 'cubicTo',
					c1x: 90,
					c1y: 67.91,
					c2x: 72.09,
					c2y: 50,
					x: 50,
					y: 50,
				},
				{
					type: 'cubicTo',
					c1x: 27.91,
					c1y: 50,
					c2x: 10,
					c2y: 67.91,
					x: 10,
					y: 90,
				},
				{ type: 'close' },
			],
			lineWidth: 20,
			clipToShape: true,
		},
	];

	ctx.save();
	ctx.translate(0, 50);
	rows.forEach(row => {
		renderRow(ctx, row);
		ctx.translate(0, 160);
	});
	ctx.restore();

	ctx.save();
	ctx.translate(0, 50 + rows.length * 160);
	drawLabel(ctx, 'Gray/Red = stroke band, Black = centerline (original path)', 0, 0);
	drawLabel(
		ctx,
		'inside/outside are relative to the closed shape; open paths ignore align and stay centered',
		0,
		14,
		Color.Blue,
	);
	ctx.restore();
}
