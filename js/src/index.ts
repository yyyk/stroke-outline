// The stroke-outline JavaScript API: a thin, typed wrapper over the WASM module.
//
//   import { init } from 'stroke-outline';
//   const so = await init();
//   const outline = so.buildOutline('M 0 0 L 100 0', { lineWidth: 8, lineCap: 'round' });
//   ctx.fill(new Path2D(outline.toSVG()));
//
// Options are partial: anything omitted takes the C++ default. Nothing here
// needs freeing; the wrapper releases every WASM-side object itself.
import createModule from "./wasm/stroke_outline.js";
import type {
	MainModule,
	PathCommand as WasmCommand,
	PathCommandVector,
	StrokeSettings as WasmSettings,
} from "./wasm/stroke_outline.js";

export type LineJoin = "miter" | "bevel" | "round";
export type LineCap = "butt" | "square" | "round";
export type StrokeAlign = "center" | "inside" | "outside";
export type FillRule = "nonzero" | "evenodd";

export interface StrokeOptions {
	/** Stroke width; must be a positive finite number. Default 1. */
	lineWidth?: number;
	/** Default 'miter'. */
	lineJoin?: LineJoin;
	/** Default 'butt'. */
	lineCap?: LineCap;
	/** Miter length / line width beyond which a miter is beveled. Default 4. */
	miterLimit?: number;
	/** Inside/outside apply to closed subpaths, relative to the whole fill. Default 'center'. */
	align?: StrokeAlign;
	/** How subpaths combine into a fill (for align and clipToShape). Default 'nonzero'. */
	fillRule?: FillRule;
	/** Resolve over-wide/overlapping strokes into simple contours. Default false. */
	clipToShape?: boolean;
	/** Dash pattern, e.g. [10, 5]. Empty or omitted: solid. [0, gap] with round caps: dots. */
	dash?: number[];
	/** Where the dash pattern starts. Default 0. */
	dashOffset?: number;
}

/** One path command; coordinates are absolute. */
export type PathCommand =
	| { type: "moveTo"; x: number; y: number }
	| { type: "lineTo"; x: number; y: number }
	| { type: "quadTo"; cx: number; cy: number; x: number; y: number }
	| {
			type: "cubicTo";
			c1x: number;
			c1y: number;
			c2x: number;
			c2y: number;
			x: number;
			y: number;
	  }
	| { type: "close" };

/** Why SVG path data stopped parsing. */
export interface SvgError {
	/**
	 * What went wrong, and where (an index into the string):
	 * `'expected a number at position 11'`, or, when the data ends too early,
	 * `'expected a number at position 10 (end of data)'`.
	 */
	message: string;
}

export interface Path {
	moveTo(x: number, y: number): Path;
	lineTo(x: number, y: number): Path;
	quadTo(cx: number, cy: number, x: number, y: number): Path;
	cubicTo(
		c1x: number,
		c1y: number,
		c2x: number,
		c2y: number,
		x: number,
		y: number,
	): Path;
	/** SVG elliptical arc to (x, y), expanded to cubics. */
	arcTo(
		rx: number,
		ry: number,
		xAxisRotationDeg: number,
		largeArc: boolean,
		sweep: boolean,
		x: number,
		y: number,
	): Path;
	close(): Path;
	/** The commands so far (absolute coordinates). */
	readonly commands: readonly PathCommand[];
	/**
	 * For a path parsed from malformed SVG data, or the outline of one: why and
	 * where parsing stopped (the path holds everything before it, as SVG
	 * renders it). Otherwise null.
	 */
	readonly error: SvgError | null;
	/** SVG path data; `precision` is the minimum number of decimals (default 3). */
	toSVG(precision?: number): string;
}

export interface StrokeOutline {
	/** A new, empty path. */
	Path: new () => Path;
	/**
	 * Parse SVG path data. Malformed data keeps everything before the first
	 * error, as SVG renders it; `path.error` says whether, why and where.
	 */
	pathFromSVG(d: string): Path;
	/**
	 * Stroke a path, or SVG path data; returns the outline as a new path (fill
	 * it with nonzero). Malformed SVG data is stroked up to the first error, as
	 * SVG renders it, and the outline's `error` says why and where.
	 */
	buildOutline(path: Path | string, options?: StrokeOptions): Path;
	/** The underlying Embind module, for anything not wrapped here. */
	readonly module: MainModule;
}

/**
 * Load the WASM module and return the API. Call once and reuse the result.
 * The WebAssembly binary is embedded in the package, so there is no file to
 * serve or locate.
 */
export async function init(): Promise<StrokeOutline> {
	return createApi(await createModule());
}

function createApi(module: MainModule): StrokeOutline {
	const ZERO = { x: 0, y: 0 };
	const joins = {
		miter: module.LineJoin.Miter,
		bevel: module.LineJoin.Bevel,
		round: module.LineJoin.Round,
	};
	const caps = {
		butt: module.LineCap.Butt,
		square: module.LineCap.Square,
		round: module.LineCap.Round,
	};
	const aligns = {
		center: module.StrokeAlign.Center,
		inside: module.StrokeAlign.Inside,
		outside: module.StrokeAlign.Outside,
	};
	const fillRules = {
		nonzero: module.FillRule.NonZero,
		evenodd: module.FillRule.EvenOdd,
	};

	function pick<T>(
		table: Record<string, T>,
		value: string | undefined,
		name: string,
		fallback: T,
	): T {
		if (value === undefined) return fallback;
		if (!(value in table))
			throw new TypeError(`stroke-outline: unknown ${name} '${value}'`);
		return table[value];
	}

	function settings(options: StrokeOptions = {}): WasmSettings {
		const d = module.defaultStrokeSettings();
		return {
			lineWidth: options.lineWidth ?? d.lineWidth,
			lineJoin: pick(joins, options.lineJoin, "lineJoin", d.lineJoin),
			lineCap: pick(caps, options.lineCap, "lineCap", d.lineCap),
			miterLimit: options.miterLimit ?? d.miterLimit,
			align: pick(aligns, options.align, "align", d.align),
			fillRule: pick(fillRules, options.fillRule, "fillRule", d.fillRule),
			clipToShape: options.clipToShape ?? d.clipToShape,
			dashPattern: options.dash ?? d.dashPattern,
			dashOffset: options.dashOffset ?? d.dashOffset,
		};
	}

	function toWasm(commands: readonly PathCommand[]): PathCommandVector {
		const vec = new module.PathCommandVector();
		for (const c of commands) {
			let command: WasmCommand;
			switch (c.type) {
				case "moveTo":
					command = {
						verb: module.Verb.MoveTo,
						ctrl1: ZERO,
						ctrl2: ZERO,
						to: { x: c.x, y: c.y },
					};
					break;
				case "lineTo":
					command = {
						verb: module.Verb.LineTo,
						ctrl1: ZERO,
						ctrl2: ZERO,
						to: { x: c.x, y: c.y },
					};
					break;
				case "quadTo":
					command = {
						verb: module.Verb.QuadraticTo,
						ctrl1: { x: c.cx, y: c.cy },
						ctrl2: ZERO,
						to: { x: c.x, y: c.y },
					};
					break;
				case "cubicTo":
					command = {
						verb: module.Verb.CubicTo,
						ctrl1: { x: c.c1x, y: c.c1y },
						ctrl2: { x: c.c2x, y: c.c2y },
						to: { x: c.x, y: c.y },
					};
					break;
				case "close":
					command = {
						verb: module.Verb.Close,
						ctrl1: ZERO,
						ctrl2: ZERO,
						to: ZERO,
					};
					break;
			}
			vec.push_back(command);
		}
		return vec;
	}

	// Read a vector into plain commands and free it.
	function fromWasm(vec: PathCommandVector): PathCommand[] {
		try {
			const out: PathCommand[] = [];
			for (let i = 0; i < vec.size(); i++) {
				const c = vec.get(i)!;
				switch (c.verb.value) {
					case module.Verb.MoveTo.value:
						out.push({ type: "moveTo", x: c.to.x, y: c.to.y });
						break;
					case module.Verb.LineTo.value:
						out.push({ type: "lineTo", x: c.to.x, y: c.to.y });
						break;
					case module.Verb.QuadraticTo.value:
						out.push({
							type: "quadTo",
							cx: c.ctrl1.x,
							cy: c.ctrl1.y,
							x: c.to.x,
							y: c.to.y,
						});
						break;
					case module.Verb.CubicTo.value:
						out.push({
							type: "cubicTo",
							c1x: c.ctrl1.x,
							c1y: c.ctrl1.y,
							c2x: c.ctrl2.x,
							c2y: c.ctrl2.y,
							x: c.to.x,
							y: c.to.y,
						});
						break;
					case module.Verb.Close.value:
						out.push({ type: "close" });
						break;
				}
			}
			return out;
		} finally {
			vec.delete();
		}
	}

	function withWasm<T>(
		commands: readonly PathCommand[],
		use: (vec: PathCommandVector) => T,
	): T {
		const vec = toWasm(commands);
		try {
			return use(vec);
		} finally {
			vec.delete();
		}
	}

	class PathImpl implements Path {
		private readonly list: PathCommand[] = [];
		// Current point and subpath start, for arcs and close.
		private x = 0;
		private y = 0;
		private startX = 0;
		private startY = 0;

		readonly error: SvgError | null;

		constructor(
			commands: readonly PathCommand[] = [],
			error: SvgError | null = null,
		) {
			for (const c of commands) this.push(c);
			this.error = error;
		}

		private push(c: PathCommand): this {
			this.list.push(c);
			if (c.type === "close") {
				this.x = this.startX;
				this.y = this.startY;
			} else {
				this.x = c.x;
				this.y = c.y;
				if (c.type === "moveTo") {
					this.startX = c.x;
					this.startY = c.y;
				}
			}
			return this;
		}

		get commands(): readonly PathCommand[] {
			return this.list;
		}

		moveTo(x: number, y: number) {
			return this.push({ type: "moveTo", x, y });
		}
		lineTo(x: number, y: number) {
			return this.push({ type: "lineTo", x, y });
		}
		quadTo(cx: number, cy: number, x: number, y: number) {
			return this.push({ type: "quadTo", cx, cy, x, y });
		}
		cubicTo(
			c1x: number,
			c1y: number,
			c2x: number,
			c2y: number,
			x: number,
			y: number,
		) {
			return this.push({ type: "cubicTo", c1x, c1y, c2x, c2y, x, y });
		}
		arcTo(
			rx: number,
			ry: number,
			rotation: number,
			largeArc: boolean,
			sweep: boolean,
			x: number,
			y: number,
		) {
			// From the current point. A degenerate arc comes back as a straight line,
			// or nothing when (x, y) is the current point.
			for (const c of fromWasm(
				module.arcToCommands(
					this.x,
					this.y,
					rx,
					ry,
					rotation,
					largeArc,
					sweep,
					x,
					y,
				),
			))
				this.push(c);
			return this;
		}
		close() {
			return this.push({ type: "close" });
		}
		toSVG(precision = 3) {
			return withWasm(this.list, (vec) => module.pathToSvg(vec, precision));
		}
	}

	return {
		Path: PathImpl,
		pathFromSVG: (d) => {
			const parsed = module.parseSvgPathChecked(d);
			const error = parsed.ok ? null : { message: String(parsed.message) };
			return new PathImpl(fromWasm(parsed.commands), error);
		},
		buildOutline: (path, options) => {
			const s = settings(options);
			if (typeof path !== "string")
				return new PathImpl(
					withWasm(path.commands, (vec) =>
						fromWasm(module.buildStrokeOutline(vec, s)),
					),
				);
			// SVG data: stroke the parsed commands where they are, in WASM memory.
			const parsed = module.parseSvgPathChecked(path);
			try {
				const error = parsed.ok ? null : { message: String(parsed.message) };
				return new PathImpl(
					fromWasm(module.buildStrokeOutline(parsed.commands, s)),
					error,
				);
			} finally {
				parsed.commands.delete();
			}
		},
		module,
	};
}
