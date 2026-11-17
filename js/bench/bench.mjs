// Times buildOutline on the inputs in ../bench/inputs/, the same way as the
// native benchmark (bench/native/main.cpp), and prints one line per input and
// settings: the time to stroke all of the input's paths once (the median of 7
// runs, each repeated for at least 100 ms), and its segments in and out.
// Run after `npm run build`:
//   npm run bench
import { readFileSync } from 'node:fs';
import { init } from '../dist/index.js';

const so = await init();
const dir = new URL('../../bench/inputs/', import.meta.url);

// Lines, quadratics and cubics: the drawn segments.
const countSegments = commands =>
	commands.filter(c => c.type !== 'moveTo' && c.type !== 'close').length;

const variants = {
	default: w => ({ lineWidth: w }),
	round: w => ({ lineWidth: w, lineJoin: 'round', lineCap: 'round' }),
	dashed: w => ({ lineWidth: w, dash: [4 * w, 2 * w] }),
	clipToShape: w => ({ lineWidth: w, clipToShape: true }),
};
const inputs = { glyphs: 0.5, icons: 2, freehand: 4 };

console.log('input     settings     time (us)  segments in  segments out');
for (const [name, width] of Object.entries(inputs)) {
	const paths = readFileSync(new URL(`${name}.txt`, dir), 'utf8')
		.split('\n')
		.filter(Boolean)
		.map(d => so.pathFromSVG(d));
	const segmentsIn = paths.reduce((n, p) => n + countSegments(p.commands), 0);

	for (const [variant, make] of Object.entries(variants)) {
		const options = make(width);
		const strokeAll = () =>
			paths.reduce((n, p) => n + countSegments(so.buildOutline(p, options).commands), 0);
		const segmentsOut = strokeAll(); // warm up
		const runs = [];
		for (let r = 0; r < 7; r++) {
			let reps = 0;
			const start = performance.now();
			let elapsed = 0;
			do {
				strokeAll();
				reps++;
				elapsed = (performance.now() - start) * 1000;
			} while (elapsed < 100000);
			runs.push(elapsed / reps);
		}
		runs.sort((a, b) => a - b);
		console.log(
			`${name.padEnd(9)} ${variant.padEnd(12)} ${runs[3].toFixed(1).padStart(9)}  ` +
				`${String(segmentsIn).padStart(11)}  ${String(segmentsOut).padStart(12)}`,
		);
	}
}
