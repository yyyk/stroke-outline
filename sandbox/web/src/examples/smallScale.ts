import { pathOf, so, type PathCommand, type StrokeOptions } from '../lib';
import { Color, drawLabel, drawSectionTitle } from '../utils';

// Scale invariance: each row strokes the same shape at a smaller scale and
// scales the result back up for display; a correct result looks exactly like
// the scale-1 column. (Fixed thresholds used to break this for tiny geometry;
// a few remain in the curve code, so curves at 1e-6 are a few percent off.)
//
// Paths go in and out as commands, at full precision, to show the stroker on
// its own. (toSVG keeps small paths' detail too: its precision is raised as
// needed for the path's size.)

const SCALES = [1, 1e-2, 1e-4, 1e-6];

const CASES: { name: string; d: string; opts: StrokeOptions & { lineWidth: number } }[] = [
	{
		name: 'Curves, round joins (width 8)',
		d: 'M 0 50 C 0 0 100 0 100 50 C 100 100 50 100 0 50 Z',
		opts: { lineWidth: 8, lineJoin: 'round', lineCap: 'round' },
	},
	{
		name: 'Polyline, miter joins (width 10)',
		d: 'M 0 0 L 100 0 L 100 100 L 30 60',
		opts: { lineWidth: 10, lineJoin: 'miter', lineCap: 'butt' },
	},
	{
		name: 'Frame, inside + clipToShape (width 25)',
		d: 'M 0 0 H 100 V 100 H 0 Z M 15 15 V 85 H 85 V 15 Z',
		opts: { lineWidth: 25, lineJoin: 'round', align: 'inside', clipToShape: true },
	},
];

const CELL = 150;
const ROW = 170;
const BOX = 100; // shape bounds
const MARGIN = 25;
const PROBE = 2; // offscreen pixels per unit for the diff

// Absolute M/L/H/V/C/Z path data (all these shapes use) as commands, scaled.
function commands(d: string, s: number): PathCommand[] {
	const tokens = d.match(/[A-Za-z]|-?\d*\.?\d+/g) ?? [];
	const out: PathCommand[] = [];
	let i = 0;
	let cmd = '';
	let pen = { x: 0, y: 0 };
	const num = () => Number(tokens[i++]) * s;
	const pt = () => ({ x: num(), y: num() });
	while (i < tokens.length) {
		if (/[A-Za-z]/.test(tokens[i])) cmd = tokens[i++];
		if (cmd === 'M') {
			pen = pt();
			out.push({ type: 'moveTo', ...pen });
			cmd = 'L';
		} else if (cmd === 'L') out.push({ type: 'lineTo', ...(pen = pt()) });
		else if (cmd === 'H') out.push({ type: 'lineTo', ...(pen = { x: num(), y: pen.y }) });
		else if (cmd === 'V') out.push({ type: 'lineTo', ...(pen = { x: pen.x, y: num() }) });
		else if (cmd === 'C') {
			const c1 = pt();
			const c2 = pt();
			pen = pt();
			out.push({ type: 'cubicTo', c1x: c1.x, c1y: c1.y, c2x: c2.x, c2y: c2.y, ...pen });
		} else if (cmd === 'Z') out.push({ type: 'close' });
		else throw new Error(`smallScale: unsupported command ${cmd}`);
	}
	return out;
}

function toPath2D(cmds: readonly PathCommand[]): Path2D {
	const p = new Path2D();
	for (const c of cmds)
		if (c.type === 'moveTo') p.moveTo(c.x, c.y);
		else if (c.type === 'lineTo') p.lineTo(c.x, c.y);
		else if (c.type === 'quadTo') p.quadraticCurveTo(c.cx, c.cy, c.x, c.y);
		else if (c.type === 'cubicTo') p.bezierCurveTo(c.c1x, c.c1y, c.c2x, c.c2y, c.x, c.y);
		else p.closePath();
	return p;
}

// The outline of `c` stroked at `scale`, ready to draw at scale 1 after
// ctx.scale(1 / scale).
function outlineAt(c: (typeof CASES)[number], scale: number): Path2D {
	const opts = { ...c.opts, lineWidth: c.opts.lineWidth * scale };
	return toPath2D(so.buildOutline(pathOf(commands(c.d, scale)), opts).commands);
}

function mask(path: Path2D, scale: number): Uint8Array {
	const size = (BOX + 2 * MARGIN) * PROBE;
	const canvas = document.createElement('canvas');
	canvas.width = canvas.height = size;
	const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
	ctx.scale(PROBE, PROBE);
	ctx.translate(MARGIN, MARGIN);
	ctx.scale(1 / scale, 1 / scale);
	ctx.fill(path, 'nonzero');
	const { data } = ctx.getImageData(0, 0, size, size);
	const m = new Uint8Array(size * size);
	for (let i = 0; i < m.length; i++) m[i] = data[i * 4 + 3] >= 128 ? 1 : 0;
	return m;
}

function difference(a: Uint8Array, b: Uint8Array): number {
	let diff = 0;
	let union = 0;
	for (let i = 0; i < a.length; i++) {
		diff += a[i] ^ b[i];
		union += a[i] | b[i];
	}
	return union === 0 ? 0 : diff / union;
}

export const SMALL_SCALE_HEIGHT = 80 + CASES.length * ROW + 30;

export function smallScale(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(ctx, 'Any size — the same stroke from scale 1 down to 1e-6');
	drawLabel(
		ctx,
		'Each column strokes the shape at that scale; results are scaled back up for display. Curves at 1e-6 are a few % of the width off.',
		0,
		18,
		'#555',
	);
	SCALES.forEach((scale, i) =>
		drawLabel(
			ctx,
			scale === 1 ? 'scale 1' : `scale ${scale.toExponential(0)}`,
			i * CELL,
			50,
		),
	);

	ctx.save();
	ctx.translate(0, 70);
	for (const c of CASES) {
		drawLabel(ctx, c.name, 0, 0);
		const reference = mask(outlineAt(c, 1), 1);
		SCALES.forEach((scale, i) => {
			const path = outlineAt(c, scale);
			const fit = (CELL - 20) / (BOX + 2 * MARGIN);
			ctx.save();
			ctx.translate(i * CELL, 12);
			ctx.scale(fit, fit);
			ctx.translate(MARGIN, MARGIN);

			ctx.save();
			ctx.scale(1 / scale, 1 / scale);
			ctx.fillStyle = '#c7c7c7';
			ctx.fill(path, 'nonzero');
			ctx.strokeStyle = Color.Red;
			ctx.lineWidth = scale / fit;
			ctx.stroke(path);
			ctx.restore();

			ctx.strokeStyle = 'rgba(0, 0, 0, 0.55)';
			ctx.lineWidth = 1 / fit;
			ctx.setLineDash([3 / fit, 2 / fit]);
			ctx.stroke(new Path2D(c.d));
			ctx.restore();

			if (scale !== 1) {
				const diff = difference(reference, mask(path, scale));
				const ok = diff < 0.02;
				drawLabel(
					ctx,
					`${ok ? '✓ same as scale 1' : '✗ differs'} (${(diff * 100).toFixed(1)}%)`,
					i * CELL,
					ROW - 18,
					ok ? '#1a8a1a' : Color.Red,
				);
			}
		});
		ctx.translate(0, ROW);
	}
	ctx.restore();
}
