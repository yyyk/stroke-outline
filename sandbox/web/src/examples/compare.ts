import { so } from '../lib';
import { Color, drawLabel, drawSectionTitle } from '../utils';

// Shared grid for the "expected vs ours" sections. Each row is one stroke case;
// each column renders it either with Canvas2D's own stroker (the reference) or
// with the WASM module under a given clipToShape setting and fill rule. Every
// "ours" cell is also rasterized offscreen against the reference and labelled
// with the pixel difference, so a wrong result shows up as a number, not just
// as something to squint at.

export type FillRule = 'nonzero' | 'evenodd';
export type Join = 'miter' | 'round' | 'bevel';
export type Align = 'center' | 'inside' | 'outside';

export interface Shape {
	name: string;
	d: string;
	width: number; // bounding box, used to fit the shape in its cell
	height: number;
}

export interface Case {
	shape: Shape;
	lineWidth: number;
	align: Align;
	lineJoin?: Join;
	// How the shape's subpaths combine into its fill (defaults to nonzero).
	fillRule?: FillRule;
	lineCap?: 'butt' | 'square' | 'round';
	dash?: number[];
	note?: string;
}

export type Column =
	| { kind: 'reference'; title: string; align?: Align }
	// Chrome's own Canvas2D stroker on the real curves (no flattening), diffed
	// against the reference like our output — it has the same inside-out bug.
	| { kind: 'chrome'; title: string; align?: Align }
	| {
			kind: 'ours';
			title: string;
			clipToShape: boolean;
			// 'outline' draws only the boundary, to show loops and overlaps.
			fill: FillRule | 'outline';
			align?: Align;
	  };

export interface Grid {
	title: string;
	subtitle?: string;
	columns: Column[];
	cases: Case[];
	legend?: string[];
}

export const SHAPES = {
	square: { name: 'Square', d: 'M 0 0 H 100 V 100 H 0 Z', width: 100, height: 100 },
	circle: {
		name: 'Circle',
		d: 'M 10 50 A 40 40 0 0 1 90 50 A 40 40 0 0 1 10 50 Z',
		width: 100,
		height: 100,
	},
	lShape: {
		name: 'L-shape',
		d: 'M 0 0 H 100 V 30 H 30 V 100 H 0 Z',
		width: 100,
		height: 100,
	},
	uShape: {
		name: 'U-shape',
		d: 'M 0 0 H 30 V 70 H 70 V 0 H 100 V 100 H 0 Z',
		width: 100,
		height: 100,
	},
	triangle: { name: 'Triangle', d: 'M 0 0 L 100 0 L 50 80 Z', width: 100, height: 80 },
	dumbbell: {
		name: 'Dumbbell',
		d: 'M 0 0 H 100 V 40 H 150 V 0 H 250 V 100 H 150 V 60 H 100 V 100 H 0 Z',
		width: 250,
		height: 100,
	},
	lens: {
		name: 'Lens (2 quads)',
		d: 'M 0 50 Q 50 -30 100 50 Q 50 130 0 50 Z',
		width: 100,
		height: 100,
	},
	star: {
		name: 'Star',
		d: 'M 50 0 L 61 35 L 98 35 L 68 57 L 79 91 L 50 70 L 21 91 L 32 57 L 2 35 L 39 35 Z',
		width: 100,
		height: 91,
	},
	// Compound shapes: the hole subpath is wound opposite to the outer one.
	frame: {
		name: 'Frame (wall 30)',
		d: 'M 0 0 H 100 V 100 H 0 Z M 30 30 V 70 H 70 V 30 Z',
		width: 100,
		height: 100,
	},
	thinFrame: {
		name: 'Frame (wall 15)',
		d: 'M 0 0 H 100 V 100 H 0 Z M 15 15 V 85 H 85 V 15 Z',
		width: 100,
		height: 100,
	},
	sameDirFrame: {
		name: 'Icon-style frame (hole wound the same way)',
		d: 'M 0 0 H 100 V 100 H 0 Z M 30 30 H 70 V 70 H 30 Z',
		width: 100,
		height: 100,
	},
	overlapping: {
		name: 'Two overlapping squares',
		d: 'M 0 0 H 60 V 60 H 0 Z M 40 40 H 100 V 100 H 40 Z',
		width: 100,
		height: 100,
	},
	line: { name: 'Open line', d: 'M 0 50 L 100 50', width: 100, height: 100 },
	arch: { name: 'Open curve', d: 'M 0 90 Q 50 -10 100 90', width: 100, height: 100 },
	ring: {
		name: 'Ring',
		d: 'M 0 50 A 50 50 0 1 1 100 50 A 50 50 0 1 1 0 50 Z M 25 50 A 25 25 0 1 0 75 50 A 25 25 0 1 0 25 50 Z',
		width: 100,
		height: 100,
	},
} satisfies Record<string, Shape>;

const CELL_WIDTH = 150;
const CELL_HEIGHT = 150;
const HEADER_HEIGHT = 70;
const LEGEND_LINE = 14;
const REFERENCE_FILL = 'rgba(68, 68, 255, 0.35)';
const CHROME_FILL = 'rgba(230, 140, 0, 0.4)';
const OURS_FILL = '#c7c7c7';
const MATCH_THRESHOLD = 0.02; // anti-aliased edges alone stay well under this
const PROBE_SCALE = 2; // offscreen pixels per shape unit for the diff

export function gridHeight(grid: Grid): number {
	return (
		HEADER_HEIGHT +
		grid.cases.length * CELL_HEIGHT +
		(grid.legend?.length ?? 0) * LEGEND_LINE +
		30
	);
}

// How far a case's stroke can reach outside the shape's bounding box.
function margin(c: Case, align: Align): number {
	const reach = align === 'center' ? c.lineWidth / 2 : align === 'outside' ? c.lineWidth : 0;
	return reach + 4;
}

type Pt = [number, number];

// Points along an SVG elliptical arc (endpoint parameterization, SVG spec F.6.5).
function arcPoints(
	p0: Pt,
	rx: number,
	ry: number,
	rotationDeg: number,
	large: boolean,
	sweep: boolean,
	p1: Pt,
	steps: number,
): Pt[] {
	const phi = (rotationDeg * Math.PI) / 180;
	const cos = Math.cos(phi);
	const sin = Math.sin(phi);
	const dx = (p0[0] - p1[0]) / 2;
	const dy = (p0[1] - p1[1]) / 2;
	const x1 = cos * dx + sin * dy;
	const y1 = -sin * dx + cos * dy;
	rx = Math.abs(rx);
	ry = Math.abs(ry);
	const lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
	if (lambda > 1) {
		rx *= Math.sqrt(lambda);
		ry *= Math.sqrt(lambda);
	}
	const num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
	const den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	const coef = (large === sweep ? -1 : 1) * Math.sqrt(Math.max(0, num / den));
	const cxp = (coef * rx * y1) / ry;
	const cyp = (-coef * ry * x1) / rx;
	const cx = cos * cxp - sin * cyp + (p0[0] + p1[0]) / 2;
	const cy = sin * cxp + cos * cyp + (p0[1] + p1[1]) / 2;
	const angle = (ux: number, uy: number, vx: number, vy: number) =>
		Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
	const ux = (x1 - cxp) / rx;
	const uy = (y1 - cyp) / ry;
	const start = angle(1, 0, ux, uy);
	let delta = angle(ux, uy, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
	if (!sweep && delta > 0) delta -= 2 * Math.PI;
	else if (sweep && delta < 0) delta += 2 * Math.PI;
	const points: Pt[] = [];
	for (let i = 1; i <= steps; i++) {
		const t = start + (delta * i) / steps;
		points.push([
			cx + rx * Math.cos(t) * cos - ry * Math.sin(t) * sin,
			cy + rx * Math.cos(t) * sin + ry * Math.sin(t) * cos,
		]);
	}
	return points;
}

// Flatten an absolute-command SVG path (M L H V Q C A Z — all the shapes above
// use) into a dense polyline, keeping every original vertex.
function flatten(d: string): Path2D {
	const STEPS = 96;
	const tokens = d.match(/[A-Za-z]|-?\d*\.?\d+(?:e[-+]?\d+)?/g) ?? [];
	const out = new Path2D();
	let i = 0;
	let cmd = '';
	let pen: Pt = [0, 0];
	let start: Pt = [0, 0];
	const num = () => Number(tokens[i++]);
	const line = (p: Pt) => {
		out.lineTo(p[0], p[1]);
		pen = p;
	};
	while (i < tokens.length) {
		if (/[A-Za-z]/.test(tokens[i])) cmd = tokens[i++];
		switch (cmd) {
			case 'M':
				pen = start = [num(), num()];
				out.moveTo(pen[0], pen[1]);
				cmd = 'L'; // implicit repeats are line-tos
				break;
			case 'L':
				line([num(), num()]);
				break;
			case 'H':
				line([num(), pen[1]]);
				break;
			case 'V':
				line([pen[0], num()]);
				break;
			case 'Q': {
				const c: Pt = [num(), num()];
				const p: Pt = [num(), num()];
				const p0 = pen;
				for (let k = 1; k <= STEPS; k++) {
					const t = k / STEPS;
					const u = 1 - t;
					line([
						u * u * p0[0] + 2 * u * t * c[0] + t * t * p[0],
						u * u * p0[1] + 2 * u * t * c[1] + t * t * p[1],
					]);
				}
				break;
			}
			case 'C': {
				const c1: Pt = [num(), num()];
				const c2: Pt = [num(), num()];
				const p: Pt = [num(), num()];
				const p0 = pen;
				for (let k = 1; k <= STEPS; k++) {
					const t = k / STEPS;
					const u = 1 - t;
					const b = [u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t];
					line([
						b[0] * p0[0] + b[1] * c1[0] + b[2] * c2[0] + b[3] * p[0],
						b[0] * p0[1] + b[1] * c1[1] + b[2] * c2[1] + b[3] * p[1],
					]);
				}
				break;
			}
			case 'A': {
				const [rx, ry, rot, large, sweep] = [num(), num(), num(), num(), num()];
				const p: Pt = [num(), num()];
				for (const q of arcPoints(pen, rx, ry, rot, large !== 0, sweep !== 0, p, STEPS))
					line(q);
				break;
			}
			case 'Z':
			case 'z':
				out.closePath();
				pen = start;
				break;
			default:
				throw new Error(`flatten: unsupported SVG command "${cmd}"`);
		}
	}
	return out;
}

// Expected coverage, from Canvas2D's stroker. Inside/outside strokes are
// emulated the standard way: stroke twice as wide, clipped to the shape (or to
// its complement). The stroke is applied to a dense polyline of the shape, not
// the curves themselves: Chrome's own curve stroker turns inside out when the
// stroke is wider than the curvature (a circle r=40 stroked 140 wide leaves a
// hole), which is exactly the bug being demonstrated. Straight pieces can't.
// `nativeCurves` strokes the real curves instead, to show Chrome's result.
function paintReference(
	ctx: CanvasRenderingContext2D,
	c: Case,
	align: Align,
	color: string,
	nativeCurves = false,
) {
	const shape = new Path2D(c.shape.d);
	const stroked = nativeCurves ? shape : flatten(c.shape.d);
	ctx.save();
	ctx.lineJoin = c.lineJoin ?? 'miter';
	ctx.miterLimit = 4;
	ctx.lineCap = c.lineCap ?? 'butt';
	ctx.setLineDash(c.dash ?? []);
	ctx.strokeStyle = color;
	const rule = c.fillRule ?? 'nonzero';
	ctx.lineWidth = align === 'center' ? c.lineWidth : c.lineWidth * 2;
	if (align === 'inside') ctx.clip(shape, rule);
	ctx.stroke(stroked);
	if (align === 'outside') {
		// Erase the shape's fill (a complement clip is only right for simple
		// fills; this handles holes and overlaps under either rule).
		ctx.globalCompositeOperation = 'destination-out';
		ctx.fillStyle = 'black';
		ctx.fill(shape, rule);
	}
	ctx.restore();
}

function outline(c: Case, align: Align, clipToShape: boolean): Path2D {
	return new Path2D(
		so
			.buildOutline(c.shape.d, {
				lineWidth: c.lineWidth,
				lineJoin: c.lineJoin ?? 'miter',
				lineCap: c.lineCap ?? 'butt',
				align,
				fillRule: c.fillRule ?? 'nonzero',
				clipToShape,
				dash: c.dash,
			})
			.toSVG(),
	);
}

// Rasterize a painter offscreen and return which pixels it covers.
function coverage(
	c: Case,
	m: number,
	paint: (ctx: CanvasRenderingContext2D) => void,
): Uint8Array {
	const canvas = document.createElement('canvas');
	canvas.width = Math.ceil((c.shape.width + 2 * m) * PROBE_SCALE);
	canvas.height = Math.ceil((c.shape.height + 2 * m) * PROBE_SCALE);
	const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
	ctx.scale(PROBE_SCALE, PROBE_SCALE);
	ctx.translate(m, m);
	paint(ctx);
	const { data } = ctx.getImageData(0, 0, canvas.width, canvas.height);
	const covered = new Uint8Array(canvas.width * canvas.height);
	for (let i = 0; i < covered.length; i++) covered[i] = data[i * 4 + 3] >= 128 ? 1 : 0;
	return covered;
}

// Pixels that differ, as a share of pixels covered by either side.
function difference(a: Uint8Array, b: Uint8Array): number {
	let diff = 0;
	let union = 0;
	for (let i = 0; i < a.length; i++) {
		diff += a[i] ^ b[i];
		union += a[i] | b[i];
	}
	return union === 0 ? 0 : diff / union;
}

function renderCell(ctx: CanvasRenderingContext2D, c: Case, column: Column) {
	const align = column.align ?? c.align;
	const m = margin(c, align);
	const scale = Math.min(
		1,
		(CELL_WIDTH - 20) / (c.shape.width + 2 * m),
		(CELL_HEIGHT - 50) / (c.shape.height + 2 * m),
	);

	ctx.save();
	// Keep each cell to its own box: a raw over-wide outline can reach far
	// outside the shape and would otherwise draw over its neighbours.
	ctx.beginPath();
	ctx.rect(-6, -6, CELL_WIDTH - 8, CELL_HEIGHT - 40);
	ctx.clip();
	ctx.scale(scale, scale);
	ctx.translate(m, m);

	if (column.kind === 'reference') paintReference(ctx, c, align, REFERENCE_FILL);
	else if (column.kind === 'chrome') paintReference(ctx, c, align, CHROME_FILL, true);
	else {
		const path = outline(c, align, column.clipToShape);
		if (column.fill !== 'outline') {
			ctx.fillStyle = OURS_FILL;
			ctx.fill(path, column.fill);
		}
		ctx.strokeStyle = Color.Red;
		ctx.lineWidth = 1 / scale;
		ctx.stroke(path);
	}

	// The original shape on top, for orientation.
	ctx.strokeStyle = 'rgba(0, 0, 0, 0.55)';
	ctx.lineWidth = 1 / scale;
	ctx.setLineDash([3 / scale, 2 / scale]);
	ctx.stroke(new Path2D(c.shape.d));
	ctx.restore();

	let paintActual: ((ctx: CanvasRenderingContext2D) => void) | undefined;
	if (column.kind === 'chrome')
		paintActual = ctx => paintReference(ctx, c, align, 'black', true);
	else if (column.kind === 'ours' && column.fill !== 'outline') {
		const { fill, clipToShape } = column;
		paintActual = ctx => {
			ctx.fillStyle = 'black';
			ctx.fill(outline(c, align, clipToShape), fill);
		};
	}
	if (paintActual) {
		const expected = coverage(c, m, ctx => paintReference(ctx, c, align, 'black'));
		const diff = difference(expected, coverage(c, m, paintActual));
		const ok = diff < MATCH_THRESHOLD;
		drawLabel(
			ctx,
			`${ok ? '✓ matches' : '✗ differs'} (${(diff * 100).toFixed(1)}%)`,
			0,
			CELL_HEIGHT - 32,
			ok ? '#1a8a1a' : Color.Red,
		);
	}
}

export function renderGrid(ctx: CanvasRenderingContext2D, grid: Grid) {
	drawSectionTitle(ctx, grid.title);
	if (grid.subtitle) drawLabel(ctx, grid.subtitle, 0, 18, '#555');

	ctx.save();
	ctx.translate(0, 50);
	grid.columns.forEach((column, i) => {
		const color =
			column.kind === 'reference'
				? Color.Blue
				: column.kind === 'chrome'
					? '#b06000'
					: 'black';
		drawLabel(ctx, column.title, i * CELL_WIDTH, 0, color);
	});

	ctx.translate(0, HEADER_HEIGHT - 50);
	for (const c of grid.cases) {
		const join =
			(c.lineJoin && c.lineJoin !== 'miter' ? `, ${c.lineJoin} join` : '') +
			(c.fillRule === 'evenodd' ? ', even-odd fill' : '') +
			(c.lineCap && c.lineCap !== 'butt' ? `, ${c.lineCap} cap` : '') +
			(c.dash ? `, dash [${c.dash.join(', ')}]` : '');
		drawLabel(ctx, `${c.shape.name} — ${c.align}, width ${c.lineWidth}${join}`, 0, 0);
		if (c.note) drawLabel(ctx, c.note, 0, 12, '#777');
		grid.columns.forEach((column, i) => {
			ctx.save();
			ctx.translate(i * CELL_WIDTH, 20);
			renderCell(ctx, c, column);
			ctx.restore();
		});
		ctx.translate(0, CELL_HEIGHT);
	}

	grid.legend?.forEach((line, i) => drawLabel(ctx, line, 0, i * LEGEND_LINE));
	ctx.restore();
}
