// Helpers shared by the live demos: a crisp canvas, drawing an outline with
// its source path, and wiring form controls to re-render.
import type { Path } from 'stroke-outline';

export interface View {
	ctx: CanvasRenderingContext2D;
	width: number;
	height: number;
}

/** Size a canvas for the device pixel ratio; returns a context in CSS pixels. */
export function view(canvas: HTMLCanvasElement, width: number, height: number): View {
	const dpr = window.devicePixelRatio || 1;
	canvas.width = Math.round(width * dpr);
	canvas.height = Math.round(height * dpr);
	canvas.style.width = `${width}px`;
	canvas.style.height = `${height}px`;
	const ctx = canvas.getContext('2d')!;
	ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	return { ctx, width, height };
}

export function css(name: string): string {
	return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

export interface DrawOptions {
	/** Offset and scale applied before drawing. */
	at?: { x: number; y: number; scale?: number };
}

/** Draw a stroke outline (filled, with its edge) and the source path on top. */
export function drawOutline(v: View, outline: Path, source: Path, o: DrawOptions = {}) {
	const { ctx } = v;
	const scale = o.at?.scale ?? 1;
	ctx.save();
	ctx.translate(o.at?.x ?? 0, o.at?.y ?? 0);
	ctx.scale(scale, scale);
	const shape = new Path2D(source.toSVG(6));
	const path = new Path2D(outline.toSVG(6));
	ctx.fillStyle = css('--stroke-fill');
	ctx.fill(path);
	ctx.strokeStyle = css('--stroke-edge');
	ctx.lineWidth = 1 / scale;
	ctx.stroke(path);
	ctx.strokeStyle = css('--fg');
	ctx.globalAlpha = 0.55;
	ctx.setLineDash([4 / scale, 3 / scale]);
	ctx.stroke(shape);
	ctx.restore();
}

export function clear(v: View) {
	v.ctx.clearRect(0, 0, v.width, v.height);
}

/** Read every named control in a form as a plain object (numbers as numbers). */
export function values(form: HTMLFormElement): Record<string, string | number | boolean> {
	const out: Record<string, string | number | boolean> = {};
	for (const el of Array.from(form.elements) as HTMLInputElement[]) {
		if (!el.name) continue;
		if (el.type === 'checkbox') out[el.name] = el.checked;
		else if (el.type === 'range' || el.type === 'number') out[el.name] = Number(el.value);
		else out[el.name] = el.value;
	}
	return out;
}

/** Re-render whenever a control changes; shows range values in <output for>. */
export function bind(form: HTMLFormElement, render: () => void) {
	const sync = () => {
		for (const out of form.querySelectorAll<HTMLOutputElement>('output[for]')) {
			const input = form.elements.namedItem(out.htmlFor.value) as HTMLInputElement | null;
			if (input) out.value = input.value;
		}
		render();
	};
	form.addEventListener('input', sync);
	form.addEventListener('submit', e => e.preventDefault());
	sync();
}

/** Bounds of every coordinate (controls included) of some paths. */
function bounds(paths: Path[]) {
	let minX = Infinity,
		minY = Infinity,
		maxX = -Infinity,
		maxY = -Infinity;
	const add = (x: number, y: number) => {
		((minX = Math.min(minX, x)), (maxX = Math.max(maxX, x)));
		((minY = Math.min(minY, y)), (maxY = Math.max(maxY, y)));
	};
	for (const p of paths)
		for (const c of p.commands)
			if (c.type === 'quadTo') (add(c.cx, c.cy), add(c.x, c.y));
			else if (c.type === 'cubicTo')
				(add(c.c1x, c.c1y), add(c.c2x, c.c2y), add(c.x, c.y));
			else if (c.type !== 'close') add(c.x, c.y);
	return { minX, minY, maxX, maxY };
}

/** The transform that fits `paths` into the view (or a box within it), centered. */
export function fit(
	v: View,
	paths: Path[],
	box = { x: 0, y: 0, width: v.width, height: v.height },
	padding = 16,
) {
	const b = bounds(paths);
	if (!isFinite(b.minX)) return { x: box.x, y: box.y, scale: 1 };
	const w = Math.max(b.maxX - b.minX, 1e-9),
		h = Math.max(b.maxY - b.minY, 1e-9);
	const scale = Math.min((box.width - 2 * padding) / w, (box.height - 2 * padding) / h);
	return {
		x: box.x + (box.width - w * scale) / 2 - b.minX * scale,
		y: box.y + (box.height - h * scale) / 2 - b.minY * scale,
		scale,
	};
}
