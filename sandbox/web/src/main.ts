// The scenes use the stroke-outline npm package (see lib.ts). Importing lib
// first loads the WASM module (top-level await) before any scene runs.
import './lib';
import { strokeOutline } from './examples/strokeOutline';
import { dash } from './examples/dash';
import { strokeAlign } from './examples/strokeAlign';
import { strokeSvg } from './examples/strokeSvg';
import { gridHeight, renderGrid } from './examples/compare';
import { clipToShapeGrid } from './examples/clipToShape';
import { wideCenterGrid } from './examples/wideCenter';
import { holesGrid } from './examples/holes';
import { dashesGrid } from './examples/dashes';
import { svgErrors, SVG_ERRORS_HEIGHT } from './examples/svgErrors';
import { smallScale, SMALL_SCALE_HEIGHT } from './examples/smallScale';

// Expected-vs-ours comparison sections, stacked below the fixed-layout ones.
const comparisonsTop = 2550;
const comparisons = [clipToShapeGrid, wideCenterGrid, holesGrid, dashesGrid];

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const status = $<HTMLParagraphElement>('status');
const canvas = $<HTMLCanvasElement>('canvas');

const dpr = window.devicePixelRatio;
const width = 1000;
const height =
	comparisonsTop +
	comparisons.reduce((sum, grid) => sum + gridHeight(grid), 0) +
	SVG_ERRORS_HEIGHT +
	SMALL_SCALE_HEIGHT +
	60;
canvas.width = Math.floor(width * dpr);
canvas.height = Math.floor(height * dpr);
canvas.style.width = `${width}px`;
canvas.style.height = `${height}px`;

const ctx = canvas.getContext('2d')!;

ctx.fillStyle = 'white';
ctx.fillRect(0, 0, canvas.width, canvas.height);
ctx.scale(dpr, dpr);
ctx.translate(50, 50);

ctx.save();
ctx.translate(0, 0);
strokeOutline(ctx);
ctx.restore();

ctx.save();
ctx.translate(0, 1150);
dash(ctx);
ctx.restore();

ctx.save();
ctx.translate(0, 1560);
strokeAlign(ctx);
ctx.restore();

ctx.save();
ctx.translate(0, 2300);
strokeSvg(ctx);
ctx.restore();

ctx.save();
ctx.translate(0, comparisonsTop);
for (const grid of comparisons) {
	renderGrid(ctx, grid);
	ctx.translate(0, gridHeight(grid));
}
svgErrors(ctx);
ctx.translate(0, SVG_ERRORS_HEIGHT);
smallScale(ctx);
ctx.restore();

status.textContent = 'Module ready.';
