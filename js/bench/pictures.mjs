// Draws the benchmark's outlines (../bench/inputs/) as SVG pictures for the
// README, one per input: its outline with clipToShape, as a light fill with
// the outline's contours on top.
// Run after `npm run build`:
//   node bench/pictures.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { init } from '../dist/index.js';

const so = await init();
const dir = new URL('../../bench/inputs/', import.meta.url);
// The stroke widths of the input pictures (bench/generate-inputs.py), and the
// width of the contour lines drawn on each outline (thick enough to read at
// the README's size).
const inputs = { glyphs: [0.5, 0.15], icons: [2, 1 / 3], freehand: [4, 2 / 3] };
const STYLE =
	'<style>.fill{fill:#1f2328;fill-opacity:0.15}.line{fill:none;stroke:#1f2328}' +
	'@media (prefers-color-scheme:dark){.fill{fill:#e6edf3}.line{stroke:#e6edf3}}</style>';

for (const [name, [width, line]] of Object.entries(inputs)) {
	const svg = readFileSync(new URL(`${name}.svg`, dir), 'utf8');
	const open = svg.slice(0, svg.indexOf('>') + 1); // the input's <svg ...> frame
	const paths = [
		...svg.matchAll(
			/<path(?: class="[^"]*")?( transform="[^"]*")? stroke-width="[^"]*" d="([^"]*)"\/>/g,
		),
	];
	const body = paths
		.map(([, transform = '', d]) => {
			const out = so.buildOutline(d, { lineWidth: width, clipToShape: true }).toSVG(3);
			return (
				`<path${transform} class="fill" d="${out}"/>` +
				`<path${transform} class="line" stroke-width="${line}" d="${out}"/>`
			);
		})
		.join('');
	writeFileSync(new URL(`${name}-outline.svg`, dir), `${open}${STYLE}${body}</svg>\n`);
}
