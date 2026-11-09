import { SHAPES, type Grid } from './compare';

// Wide strokes on concave shapes. Where the stroke is wider than a notch, the
// raw outer boundary loops back over itself: nonzero fill hides that, even-odd
// punches holes. clipToShape resolves the whole outline into simple contours,
// so it fills the same under both rules. An outside stroke can also reach
// across a notch into the shape, which clipToShape removes.
export const wideCenterGrid: Grid = {
	title: 'Wide strokes on concave shapes',
	subtitle:
		'With clipToShape off the outline overlaps itself (fine under nonzero only). With it on, the outline is simple.',
	columns: [
		{ kind: 'reference', title: 'Expected (Canvas2D)' },
		{ kind: 'chrome', title: 'Chrome (native curves)' },
		{
			kind: 'ours',
			title: 'clipToShape off · nonzero',
			clipToShape: false,
			fill: 'nonzero',
		},
		{
			kind: 'ours',
			title: 'clipToShape off · even-odd',
			clipToShape: false,
			fill: 'evenodd',
		},
		{
			kind: 'ours',
			title: 'clipToShape on · even-odd',
			clipToShape: true,
			fill: 'evenodd',
		},
		{ kind: 'ours', title: 'clipToShape on · outline', clipToShape: true, fill: 'outline' },
	],
	cases: [
		{ shape: SHAPES.uShape, lineWidth: 70, align: 'center', lineJoin: 'round' },
		{ shape: SHAPES.lShape, lineWidth: 70, align: 'center', lineJoin: 'round' },
		{ shape: SHAPES.star, lineWidth: 40, align: 'center', lineJoin: 'round' },
		{ shape: SHAPES.dumbbell, lineWidth: 90, align: 'center', lineJoin: 'round' },
		{
			shape: SHAPES.uShape,
			lineWidth: 60,
			align: 'outside',
			lineJoin: 'round',
			note: 'Outside stroke wider than the 40-wide notch reaches into the other arm',
		},
	],
	legend: [
		'Canvas fill() and SVG default to nonzero, where the raw outline is already right; clipToShape matters for even-odd',
		'renderers and for code that needs simple polygons.',
	],
};
