import { pathOf, so } from '../lib';
import { Color, drawLabel, drawSectionTitle, drawPathCommands } from '../utils';

function renderTest(ctx: CanvasRenderingContext2D, test: any) {
	ctx.save();
	ctx.translate(test.x, 0);

	const lineCap = test.lineCap || 'round';
	const lineJoin = test.lineJoin || 'round';
	const miterLimit = test.miterLimit || 4;

	// Canvas2D stroke (reference)
	ctx.save();
	{
		drawLabel(ctx, `${test.name} (Canvas2D)`, 0, -5, Color.Blue);

		ctx.strokeStyle = Color.Blue;
		ctx.lineWidth = test.lineWidth;
		ctx.lineCap = lineCap;
		ctx.lineJoin = lineJoin;
		ctx.miterLimit = miterLimit;

		ctx.beginPath();
		drawPathCommands(ctx, test.path);
		ctx.stroke();

		// Draw the centerline
		ctx.strokeStyle = 'white';
		ctx.lineWidth = 1;
		ctx.beginPath();
		drawPathCommands(ctx, test.path);
		ctx.stroke();
	}
	ctx.restore();

	// stroke outline
	ctx.save();
	ctx.translate(150, 0);
	{
		drawLabel(ctx, 'Stroke outline outline', 0, -5, Color.Red);

		const strokeOutline = so.buildOutline(pathOf(test.path), {
			lineWidth: test.lineWidth,
			lineJoin,
			lineCap,
			miterLimit,
		}).commands;

		// Draw the stroke outline outline
		ctx.strokeStyle = Color.Red;
		ctx.fillStyle = '#c7c7c7';
		ctx.lineWidth = 1;
		ctx.beginPath();
		drawPathCommands(ctx, strokeOutline);
		ctx.fill('nonzero');
		ctx.stroke();

		// Draw the centerline
		ctx.strokeStyle = 'rgba(0, 0, 0, 0.45)';
		ctx.lineWidth = 1;
		ctx.beginPath();
		drawPathCommands(ctx, test.path);
		ctx.stroke();

		drawLabel(ctx, `${strokeOutline.length} commands`, 0, 105, Color.Red);
		// console.log(strokeOutline.length, strokeOutline);

		// ctx.save();
		// ctx.fillStyle = 'rgba(0, 0, 0, 0.45)';
		// for (let i = 0; i < strokeOutline.length; i++) {
		// 	const cmd = strokeOutline[i];
		// 	if (cmd.type === 'close') continue;
		// 	ctx.font = '6px sans-serif';
		// 	ctx.fillText(`${i}`, cmd.x, cmd.y);
		// }
		// ctx.restore();
	}
	ctx.restore();

	ctx.restore();
}

export function strokeOutline(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(ctx, 'Stroke Outline Generation');

	ctx.save();
	ctx.translate(0, 50);
	drawLabel(ctx, 'Line Caps:', 0, -20);
	[
		{
			name: 'Butt cap',
			path: [
				{ type: 'moveTo', x: 20, y: 50 },
				{ type: 'lineTo', x: 80, y: 50 },
			],
			lineWidth: 20,
			lineCap: 'butt' as const,
			lineJoin: 'round' as const,
			x: 0,
		},
		{
			name: 'Square cap',
			path: [
				{ type: 'moveTo', x: 20, y: 50 },
				{ type: 'lineTo', x: 80, y: 50 },
			],
			lineWidth: 20,
			lineCap: 'square' as const,
			lineJoin: 'round' as const,
			x: 320,
		},
		{
			name: 'Round cap',
			path: [
				{ type: 'moveTo', x: 20, y: 50 },
				{ type: 'lineTo', x: 80, y: 50 },
			],
			lineWidth: 20,
			lineCap: 'round' as const,
			lineJoin: 'round' as const,
			x: 640,
		},
	].forEach(test => {
		renderTest(ctx, test);
	});
	ctx.restore();

	ctx.save();
	ctx.translate(0, 220);
	drawLabel(ctx, 'Line Joins:', 0, -20);
	[
		{
			name: 'Miter join',
			path: [
				{ type: 'moveTo', x: 20, y: 20 },
				{ type: 'lineTo', x: 20, y: 80 },
				{ type: 'lineTo', x: 80, y: 80 },
				{ type: 'lineTo', x: 80, y: 20 },
				{ type: 'lineTo', x: 50, y: 50 },
				{ type: 'close' },
			],
			lineWidth: 10,
			lineCap: 'butt' as const,
			lineJoin: 'miter' as const,
			x: 0,
		},
		{
			name: 'Bevel join',
			path: [
				{ type: 'moveTo', x: 20, y: 20 },
				{ type: 'lineTo', x: 50, y: 50 },
				{ type: 'lineTo', x: 80, y: 20 },
				{ type: 'lineTo', x: 80, y: 80 },
				{ type: 'lineTo', x: 20, y: 80 },
				{ type: 'close' },
			],
			lineWidth: 10,
			lineCap: 'butt' as const,
			lineJoin: 'bevel' as const,
			x: 320,
		},
		{
			name: 'Round join',
			path: [
				{ type: 'moveTo', x: 20, y: 20 },
				{ type: 'lineTo', x: 20, y: 80 },
				{ type: 'lineTo', x: 80, y: 80 },
				{ type: 'lineTo', x: 80, y: 20 },
				{ type: 'lineTo', x: 50, y: 50 },
				{ type: 'close' },
			],
			lineWidth: 10,
			lineCap: 'butt' as const,
			lineJoin: 'round' as const,
			x: 640,
		},
	].forEach(test => {
		renderTest(ctx, test);
	});
	ctx.restore();

	ctx.save();
	ctx.translate(0, 390);
	{
		// Edge cases - comprehensive tests
		const edgeCaseTests = [
			// 0° turns (straight line, collinear segments)
			{
				name: '0° - Miter',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 90, y: 50 },
				],
				lineWidth: 12,
				lineCap: 'butt' as const,
				lineJoin: 'miter' as const,
				x: 0,
			},
			{
				name: '0° - Bevel',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 90, y: 50 },
				],
				lineWidth: 12,
				lineCap: 'butt' as const,
				lineJoin: 'bevel' as const,
				x: 320,
			},
			{
				name: '0° - Round',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 90, y: 50 },
				],
				lineWidth: 12,
				lineCap: 'butt' as const,
				lineJoin: 'round' as const,
				x: 640,
			},
			// 180° turns (complete reversal)
			{
				name: '180° - Miter',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 10, y: 50 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'miter' as const,
				x: 0,
			},
			{
				name: '180° - Bevel',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 10, y: 50 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'bevel' as const,
				x: 320,
			},
			{
				name: '180° - Round',
				path: [
					{ type: 'moveTo', x: 10, y: 50 },
					{ type: 'lineTo', x: 50, y: 50 },
					{ type: 'lineTo', x: 10, y: 50 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'round' as const,
				x: 640,
			},
			// Miter limit cases
			{
				name: 'Miter OK',
				path: [
					{ type: 'moveTo', x: 10, y: 60 },
					{ type: 'lineTo', x: 60, y: 60 },
					{ type: 'lineTo', x: 20, y: 30 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'miter' as const,
				miterLimit: 4,
				x: 0,
			},
			{
				name: 'Miter → Bevel',
				path: [
					{ type: 'moveTo', x: 60, y: 60 },
					{ type: 'lineTo', x: 10, y: 60 },
					{ type: 'lineTo', x: 50, y: 40 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'miter' as const,
				miterLimit: 4,
				x: 320,
			},
			{
				name: 'Miter → Bevel',
				path: [
					{ type: 'moveTo', x: 10, y: 60 },
					{ type: 'lineTo', x: 60, y: 60 },
					{ type: 'lineTo', x: 20, y: 48 },
				],
				lineWidth: 15,
				lineCap: 'butt' as const,
				lineJoin: 'miter' as const,
				miterLimit: 4,
				x: 640,
			},
		];

		// 0° turns row
		drawLabel(ctx, '0° Turns (collinear segments):', 0, 0);
		ctx.save();
		ctx.translate(0, 20);
		for (let i = 0; i < 3; i++) {
			renderTest(ctx, edgeCaseTests[i]);
		}
		ctx.restore();

		// 180° turns row
		ctx.translate(0, 170);
		drawLabel(ctx, '180° Turns (complete reversal):', 0, 0);
		ctx.save();
		ctx.translate(0, 20);
		for (let i = 3; i < 6; i++) {
			renderTest(ctx, edgeCaseTests[i]);
		}
		ctx.restore();

		// Miter limit row
		ctx.translate(0, 170);
		drawLabel(ctx, 'Miter Limit Cases:', 0, 0);
		ctx.save();
		ctx.translate(0, 20);
		for (let i = 6; i < 9; i++) {
			// if (i !== 8) continue;
			renderTest(ctx, edgeCaseTests[i]);
		}
		ctx.restore();
	}
	ctx.restore();

	ctx.save();
	ctx.translate(0, 920);
	drawLabel(ctx, 'Curves:', 0, -20);
	[
		{
			name: 'Quadratic path',
			path: [
				{ type: 'moveTo', x: 0, y: 25 },
				{ type: 'quadTo', cx: 50, cy: 75, x: 100, y: 25 },
			],
			lineWidth: 20,
			lineCap: 'round' as const,
			lineJoin: 'miter' as const,
			x: 0,
		},
		{
			name: 'Cubic path',
			path: [
				{ type: 'moveTo', x: 0, y: 25 },
				{
					type: 'cubicTo',
					c1x: 0,
					c1y: 75,
					c2x: 100,
					c2y: 25,
					x: 100,
					y: 75,
				},
			],
			lineWidth: 20,
			lineCap: 'round' as const,
			lineJoin: 'miter' as const,
			x: 320,
		},
		{
			name: 'Cubic path',
			path: [
				{ type: 'moveTo', x: 10, y: 25 },
				{
					type: 'cubicTo',
					c1x: -10,
					c1y: 75,
					c2x: 25,
					c2y: 65,
					x: 50,
					y: 50,
				},
				{
					type: 'cubicTo',
					c1x: 75,
					c1y: 35,
					c2x: 110,
					c2y: 25,
					x: 90,
					y: 75,
				},
			],
			lineWidth: 10,
			lineCap: 'square' as const,
			lineJoin: 'miter' as const,
			x: 640,
		},
	].forEach(test => {
		renderTest(ctx, test);
	});
	ctx.restore();

	ctx.save();
	ctx.translate(0, 1070);
	drawLabel(ctx, 'Blue = Canvas2D stroke (filled)', 0, 0, Color.Blue);
	drawLabel(ctx, 'Red = stroke outline outline', 0, 14, Color.Red);
	drawLabel(ctx, 'White/Black = centerline path', 0, 28);
	ctx.restore();
}
