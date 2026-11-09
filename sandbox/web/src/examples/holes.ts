import { SHAPES, type Grid } from './compare';

// Inside/outside strokes on compound shapes. Alignment is relative to the
// fill of the whole path (under its fill rule), so a hole is stroked into the
// material around it. Strokes narrower than every wall are right with
// clipToShape off; wider ones, and subpaths that overlap, need it on.
export const holesGrid: Grid = {
	title: 'Shapes with holes and overlapping subpaths',
	subtitle:
		"Inside/outside follow the whole shape's fill. clipToShape resolves strokes wider than a wall and overlapping subpaths.",
	columns: [
		{ kind: 'reference', title: 'Expected (Canvas2D)' },
		{ kind: 'chrome', title: 'Chrome (native curves)' },
		{
			kind: 'ours',
			title: 'clipToShape off · nonzero',
			clipToShape: false,
			fill: 'nonzero',
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
			shape: SHAPES.frame,
			lineWidth: 12,
			align: 'inside',
			lineJoin: 'round',
			note: 'The hole is stroked into the material (it used to go into the hole)',
		},
		{ shape: SHAPES.frame, lineWidth: 12, align: 'outside', lineJoin: 'round' },
		{ shape: SHAPES.ring, lineWidth: 12, align: 'inside' },
		{
			shape: SHAPES.sameDirFrame,
			lineWidth: 12,
			align: 'inside',
			lineJoin: 'round',
			fillRule: 'evenodd',
			note: 'Typical SVG icon: the hole only exists under fill-rule="evenodd"',
		},
		{
			shape: SHAPES.thinFrame,
			lineWidth: 25,
			align: 'inside',
			lineJoin: 'round',
			note: 'Wider than the 15-wide wall: without clipping it spills into the hole and outside',
		},
		{
			shape: SHAPES.overlapping,
			lineWidth: 10,
			align: 'inside',
			lineJoin: 'round',
			note: 'Edges inside the other square are not boundaries: the full double-width band applies there',
		},
	],
	legend: [
		'Inside/outside = the stroke at twice the width, clipped to the fill (or its complement) of the whole shape.',
	],
};
