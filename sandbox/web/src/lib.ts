// The sandbox uses the stroke-outline npm package (js/, straight from source),
// so the scenes exercise the same API users get. Build the WASM module first:
//   cmake --preset wasm && cmake --build --preset wasm-release
import { init, type Path, type PathCommand } from 'stroke-outline';

export type { Path, PathCommand, StrokeOptions } from 'stroke-outline';

export const so = await init();

/** A Path from a list of commands, for scenes that describe paths as data. */
export function pathOf(commands: readonly PathCommand[]): Path {
	const path = new so.Path();
	for (const c of commands)
		if (c.type === 'moveTo') path.moveTo(c.x, c.y);
		else if (c.type === 'lineTo') path.lineTo(c.x, c.y);
		else if (c.type === 'quadTo') path.quadTo(c.cx, c.cy, c.x, c.y);
		else if (c.type === 'cubicTo') path.cubicTo(c.c1x, c.c1y, c.c2x, c.c2y, c.x, c.y);
		else path.close();
	return path;
}
