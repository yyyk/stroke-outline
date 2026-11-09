import { SHAPES, type Grid } from './compare';

// The clipToShape fixes: inside strokes wider than the shape. With clipToShape
// off, the raw inner boundary turns inside out and crosses itself, which only
// looks right under nonzero (if at all). With it on, the stroke fills the shape
// and both fill rules agree with the Canvas2D reference.
export const clipToShapeGrid: Grid = {
	title: 'clipToShape — inside strokes wider than the shape',
	subtitle:
		'"off" is the raw outline. The old clipper (src/stroke/clip_weiler_atherton.cpp) is no longer wired in, so it is not shown.',
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
		{ kind: 'ours', title: 'clipToShape on · nonzero', clipToShape: true, fill: 'nonzero' },
		{
			kind: 'ours',
			title: 'clipToShape on · even-odd',
			clipToShape: true,
			fill: 'evenodd',
		},
	],
	cases: [
		{
			shape: SHAPES.square,
			lineWidth: 20,
			align: 'inside',
			note: 'Normal width: nothing to clip, output unchanged',
		},
		{
			shape: SHAPES.square,
			lineWidth: 60,
			align: 'inside',
			note: 'Inner edge turns inside out',
		},
		{
			shape: SHAPES.square,
			lineWidth: 150,
			align: 'inside',
			note: 'Inner edge crosses itself and escapes; the old clipper emitted points outside the square',
		},
		{
			shape: SHAPES.circle,
			lineWidth: 70,
			align: 'inside',
			note: 'Inside-out loop that never crosses the shape; the old clipper kept it as a hole',
		},
		{ shape: SHAPES.lShape, lineWidth: 40, align: 'inside', lineJoin: 'round' },
		{ shape: SHAPES.triangle, lineWidth: 50, align: 'inside' },
		{
			shape: SHAPES.dumbbell,
			lineWidth: 30,
			align: 'inside',
			note: 'Narrow corridor fills while both rooms keep their holes',
		},
		{
			shape: SHAPES.lens,
			lineWidth: 10,
			align: 'inside',
			note: 'Direction fix: used to be drawn outside the shape. The ~3% left is quadratic offset accuracy',
		},
		{ shape: SHAPES.lens, lineWidth: 60, align: 'inside' },
	],
	legend: [
		'Blue = expected coverage (Canvas2D stroke at 2× width on a dense polyline of the shape, clipped to it). Gray/red = our outline. Dashed = original shape.',
		"Orange = the same Canvas2D emulation on the real curves: Chrome's stroker turns inside out on curves tighter than the stroke (see the circle).",
		'The % is the share of pixels that differ from the expected coverage; anti-aliased edges alone stay under 2%.',
		'Lens (2 quads) at width 10 differs slightly even when correct: quadratic offsets are only accurate to ~7% of the offset.',
	],
};
