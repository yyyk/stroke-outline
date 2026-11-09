import './style.css';
import { init, type LineCap, type LineJoin, type StrokeAlign } from 'stroke-outline';
import { bind, clear, drawOutline, fit, values, view } from './demo';
import { highlightAll } from './highlight';

highlightAll();

const SHAPES: Record<string, string> = {
	curve: 'M 20 150 C 80 20 160 20 200 110 L 280 40 L 330 150 Q 420 230 500 90',
	frame: 'M 0 0 H 100 V 100 H 0 Z M 30 30 V 70 H 70 V 30 Z',
	star: 'M 50 0 L 61 35 L 98 35 L 68 57 L 79 91 L 50 70 L 21 91 L 32 57 L 2 35 L 39 35 Z',
};

const so = await init();
const form = document.querySelector<HTMLFormElement>('#demo-form')!;
const v = view(document.querySelector<HTMLCanvasElement>('#demo')!, 680, 280);
const code = document.querySelector<HTMLElement>('#demo-code'); // optional

bind(form, () => {
	const o = values(form);
	const source = so.pathFromSVG(SHAPES[o.shape as string]);
	const options = {
		lineWidth: o.width as number,
		lineJoin: o.join as LineJoin,
		lineCap: o.cap as LineCap,
		align: o.align as StrokeAlign,
		clipToShape: o.clip as boolean,
		dash: o.dashed ? [16, 8] : undefined,
	};
	const outline = so.buildOutline(source, options);
	clear(v);
	// Fit the source, not the outline, so the view doesn't jump as the width changes.
	const at = fit(v, [source], undefined, 40);
	drawOutline(v, outline, source, { at });
	if (code)
		code.textContent = `so.buildOutline(path, ${JSON.stringify(options)
			.replace(/"(\w+)":/g, '$1: ')
			.replace(/,/g, ', ')})`;
});
