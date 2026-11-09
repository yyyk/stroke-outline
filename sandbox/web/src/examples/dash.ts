import { pathOf, so, type PathCommand } from '../lib';
import { Color, drawLabel, drawSectionTitle, drawPathCommands } from '../utils';

export function dash(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(ctx, 'Dashed Paths (Curve-Preserving)');

	ctx.save();
	ctx.translate(0, 50);

	[
		{
			name: 'Line [10, 5]',
			path: [
				{ type: 'moveTo', x: 0, y: 30 },
				{ type: 'lineTo', x: 120, y: 30 },
			],
			dashArray: [10, 5],
			x: 0,
		},
		{
			name: 'Cubic [15, 8]',
			path: [
				{ type: 'moveTo', x: 0, y: 50 },
				{
					type: 'cubicTo',
					c1x: 0,
					c1y: 0,
					c2x: 100,
					c2y: 0,
					x: 100,
					y: 50,
				},
			],
			dashArray: [15, 8],
			x: 160,
		},
		{
			name: 'Cubic [12, 6, 3]',
			path: [
				{ type: 'moveTo', x: 0, y: 0 },
				{
					type: 'cubicTo',
					c1x: 80,
					c1y: 0,
					c2x: 20,
					c2y: 80,
					x: 100,
					y: 80,
				},
			],
			dashArray: [12, 6, 3],
			x: 320,
		},
		{
			name: 'Quadratic [8, 4]',
			path: [
				{ type: 'moveTo', x: 0, y: 50 },
				{ type: 'quadTo', cx: 50, cy: 0, x: 100, y: 50 },
			],
			dashArray: [8, 4],
			x: 480,
		},
		{
			name: 'Multi-segment [10, 5]',
			path: [
				{ type: 'moveTo', x: 0, y: 0 },
				{ type: 'lineTo', x: 50, y: 0 },
				{
					type: 'cubicTo',
					c1x: 80,
					c1y: 0,
					c2x: 100,
					c2y: 30,
					x: 100,
					y: 60,
				},
				{ type: 'lineTo', x: 50, y: 60 },
			],
			dashArray: [10, 5],
			x: 640,
		},
		{
			name: 'Closed Rectangle [15, 8]',
			path: [
				{ type: 'moveTo', x: 0, y: 0 },
				{ type: 'lineTo', x: 80, y: 0 },
				{ type: 'lineTo', x: 80, y: 60 },
				{ type: 'lineTo', x: 0, y: 60 },
				{ type: 'close' },
			],
			dashArray: [15, 8],
			x: 800,
		},
	].forEach(test => {
		ctx.save();
		ctx.translate(test.x, 0);

		{
			drawLabel(ctx, test.name, 0, -10);

			// Draw original path (faded)
			ctx.strokeStyle = 'rgba(0, 0, 255, 0.2)';
			ctx.lineWidth = 2;
			ctx.beginPath();
			drawPathCommands(ctx, test.path as any);
			ctx.stroke();

			// Canvas2D dashed stroke (reference)
			ctx.strokeStyle = Color.Blue;
			ctx.lineWidth = 2;
			ctx.setLineDash(test.dashArray);
			ctx.beginPath();
			drawPathCommands(ctx, test.path as any);
			ctx.stroke();
			ctx.setLineDash([]);
		}

		// dashed stroke outline (dashing applied by the WASM module)
		ctx.translate(0, 130);
		{
			drawLabel(ctx, 'dashed stroke outline', 0, -10, Color.Red);

			// Draw original path (faded) for comparison
			ctx.strokeStyle = 'rgba(255, 0, 0, 0.2)';
			ctx.lineWidth = 2;
			ctx.beginPath();
			drawPathCommands(ctx, test.path as any);
			ctx.stroke();

			const outline = so.buildOutline(pathOf(test.path as PathCommand[]), {
				lineWidth: 2,
				lineCap: 'butt',
				lineJoin: 'miter',
				dash: test.dashArray,
			}).commands;

			ctx.strokeStyle = Color.Red;
			ctx.fillStyle = '#c7c7c7';
			ctx.lineWidth = 1;
			ctx.beginPath();
			drawPathCommands(ctx, outline);
			ctx.fill('nonzero');
			ctx.stroke();

			drawLabel(ctx, `${outline.length} commands`, 0, 110, Color.Red);
		}

		ctx.restore();
	});

	ctx.save();
	ctx.translate(0, 290);
	drawLabel(ctx, 'Blue = Canvas2D dashed stroke', 0, 0, Color.Blue);
	drawLabel(ctx, 'Red = dashed stroke outline outline', 0, 14, Color.Red);
	ctx.restore();

	ctx.restore();
}
