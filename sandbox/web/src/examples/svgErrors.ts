import { so } from '../lib';
import { Color, drawLabel, drawSectionTitle } from '../utils';

// What pathFromSVG reports (path.error) for some malformed path data.
// Everything before the first error is still used, as SVG renders it.
const SAMPLES = [
	'M 10 10 L 90 10 Z',
	'M 0 0 L 10 10 X 5 5',
	'M 0 0 L 10 10 L 20',
	'M 0 0 A 10 10 0 2 0 20 0',
	'L 10 10',
	'hello',
];

export const SVG_ERRORS_HEIGHT = 60 + SAMPLES.length * 16 + 20;

export function svgErrors(ctx: CanvasRenderingContext2D) {
	drawSectionTitle(ctx, 'SVG path errors (path.error)');
	drawLabel(
		ctx,
		'Malformed data is used up to the first error, as SVG does; path.error says why and where.',
		0,
		18,
		'#555',
	);
	SAMPLES.forEach((d, i) => {
		const y = 46 + i * 16;
		const { error } = so.pathFromSVG(d);
		drawLabel(ctx, JSON.stringify(d), 0, y);
		drawLabel(
			ctx,
			error ? `✗ ${error.message}` : '✓ ok',
			220,
			y,
			error ? Color.Red : '#1a8a1a',
		);
	});
}
