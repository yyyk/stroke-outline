import { SHAPES, type Grid } from './compare';

// Dots and dashed alignment. A zero-length dash is a dot with round/square
// caps (so [0, gap] is a dotted line), and the dashes of an inside/outside
// stroke stay on their side: butt-capped ones are offset there, round/square
// ones are clipped to the shape since their caps would cross its edge at
// corners.
export const dashesGrid: Grid = {
	title: 'Dots and dashed strokes',
	subtitle:
		'Zero-length dashes are dots with round/square caps. Dashed inside/outside strokes stay on their side, holes included.',
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
			title: 'clipToShape on · even-odd',
			clipToShape: true,
			fill: 'evenodd',
		},
	],
	cases: [
		{
			shape: SHAPES.line,
			lineWidth: 8,
			align: 'center',
			lineCap: 'round',
			dash: [0, 16],
			note: "Dotted line. The small difference: Chrome's round dots are ~4% larger than a true disc",
		},
		{
			shape: SHAPES.arch,
			lineWidth: 8,
			align: 'center',
			lineCap: 'square',
			dash: [0, 14],
			note: 'Ours turn with the curve; Chrome keeps square dots axis-aligned, so this row differs by design',
		},
		{
			shape: SHAPES.square,
			lineWidth: 10,
			align: 'inside',
			dash: [20, 10],
			note: 'Butt-capped dashes are offset inside (they used to straddle the edge)',
		},
		{
			shape: SHAPES.square,
			lineWidth: 10,
			align: 'inside',
			lineCap: 'round',
			dash: [0, 25],
			note: 'Inside dots: the stroked half of each dot, quarters at the corners',
		},
		{
			shape: SHAPES.frame,
			lineWidth: 8,
			align: 'inside',
			lineJoin: 'round',
			lineCap: 'round',
			dash: [15, 8],
			note: 'Round-capped dashes around a hole stay in the material',
		},
		{
			shape: SHAPES.ring,
			lineWidth: 8,
			align: 'outside',
			lineCap: 'square',
			dash: [10, 8],
		},
	],
	legend: [
		'The pattern restarts at every subpath (SVG 2, Skia). Patterns that would produce >100,000 dashes stroke solid.',
	],
};
