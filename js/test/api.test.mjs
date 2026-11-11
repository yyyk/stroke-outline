// Tests for the JS wrapper, against the built package (dist/).
import { test } from "node:test";
import assert from "node:assert/strict";
import { init } from "../dist/index.js";

const so = await init();

// Bounds of all coordinates in an SVG path string.
function bounds(d) {
	const n = d.match(/-?\d*\.?\d+(?:e[-+]?\d+)?/g).map(Number);
	const xs = n.filter((_, i) => i % 2 === 0),
		ys = n.filter((_, i) => i % 2 === 1);
	return {
		minX: Math.min(...xs),
		maxX: Math.max(...xs),
		minY: Math.min(...ys),
		maxY: Math.max(...ys),
	};
}
const subpaths = (d) => (d.match(/M/g) ?? []).length;

test("options are partial: omitted ones take the C++ defaults", () => {
	// The raw module with its complete default settings, for comparison.
	const m = so.module;
	const parsed = m.parseSvgPathChecked("M 0 0 L 100 0 L 100 100");
	const raw = m.buildStrokeOutline(parsed.commands, m.defaultStrokeSettings());
	const full = m.pathToSvg(raw, 3);
	parsed.commands.delete();
	raw.delete();
	assert.equal(so.buildOutline("M 0 0 L 100 0 L 100 100").toSVG(), full);
	assert.equal(
		so.buildOutline("M 0 0 L 100 0", { lineWidth: 10 }).toSVG(),
		"M 0 5 L 100 5 L 100 -5 L 0 -5 Z",
	);
});

test("enums are plain strings", () => {
	assert.match(
		so
			.buildOutline("M 0 0 L 100 0", { lineWidth: 10, lineCap: "round" })
			.toSVG(),
		/C/,
	);
	const inside = bounds(
		so
			.buildOutline("M 0 0 H 100 V 100 H 0 Z", {
				lineWidth: 10,
				align: "inside",
			})
			.toSVG(),
	);
	assert.deepEqual(inside, { minX: 0, maxX: 100, minY: 0, maxY: 100 });
	// An even-odd icon: the hole is wound the same way as the outer edge.
	const frame = "M 0 0 H 100 V 100 H 0 Z M 30 30 H 70 V 70 H 30 Z";
	assert.notEqual(
		so
			.buildOutline(frame, {
				lineWidth: 10,
				align: "inside",
				fillRule: "evenodd",
			})
			.toSVG(),
		so
			.buildOutline(frame, {
				lineWidth: 10,
				align: "inside",
				fillRule: "nonzero",
			})
			.toSVG(),
	);
});

test("an unknown option value is a TypeError, not a silent default", () => {
	assert.throws(
		() => so.buildOutline("M 0 0 L 1 0", { lineJoin: "rounded" }),
		TypeError,
	);
	assert.throws(
		() => so.buildOutline("M 0 0 L 1 0", { align: "middle" }),
		TypeError,
	);
});

test("dashes and dots", () => {
	assert.equal(
		subpaths(
			so
				.buildOutline("M 0 0 L 100 0", { lineWidth: 2, dash: [20, 10] })
				.toSVG(),
		),
		4,
	);
	assert.equal(
		subpaths(
			so
				.buildOutline("M 0 0 L 100 0", {
					lineWidth: 4,
					lineCap: "round",
					dash: [0, 20],
				})
				.toSVG(),
		),
		6,
	);
	// dashOffset 25 into [20, 10]: the first dash starts at x = 5.
	const offset = so
		.buildOutline("M 0 0 L 100 0", {
			lineWidth: 2,
			dash: [20, 10],
			dashOffset: 25,
		})
		.toSVG();
	assert.equal(bounds(offset.split("Z")[0]).minX, 5);
});

test("Path builder records commands and renders SVG", () => {
	const p = new so.Path()
		.moveTo(0, 0)
		.lineTo(10, 0)
		.quadTo(15, 5, 10, 10)
		.close();
	assert.deepEqual(p.commands, [
		{ type: "moveTo", x: 0, y: 0 },
		{ type: "lineTo", x: 10, y: 0 },
		{ type: "quadTo", cx: 15, cy: 5, x: 10, y: 10 },
		{ type: "close" },
	]);
	assert.equal(p.toSVG(), "M 0 0 L 10 0 Q 15 5 10 10 Z");
});

test("Path.arcTo expands to cubics ending at the target; a degenerate arc is a line", () => {
	const p = new so.Path().moveTo(0, 0).arcTo(50, 50, 0, false, true, 100, 0);
	const last = p.commands.at(-1);
	assert.equal(last.type, "cubicTo");
	assert.ok(Math.abs(last.x - 100) < 1e-9 && Math.abs(last.y) < 1e-9);
	const line = new so.Path().moveTo(0, 0).arcTo(0, 0, 0, false, true, 10, 0);
	assert.deepEqual(line.commands.at(-1), { type: "lineTo", x: 10, y: 0 });
	// Ending where it starts, an arc adds nothing (as the SVG parser does).
	const none = new so.Path().moveTo(5, 5).arcTo(20, 20, 0, false, true, 5, 5);
	assert.deepEqual(none.commands, [{ type: "moveTo", x: 5, y: 5 }]);
});

test("stroking a Path or its SVG data gives the same outline", () => {
	const d = "M 0 0 C 50 -40 100 40 150 0 L 150 60 Z";
	const options = { lineWidth: 6, lineJoin: "round" };
	assert.equal(
		so.buildOutline(so.pathFromSVG(d), options).toSVG(),
		so.buildOutline(d, options).toSVG(),
	);
	const outline = so.buildOutline(new so.Path().moveTo(0, 0).lineTo(100, 0), {
		lineWidth: 10,
	});
	assert.deepEqual(outline.commands[0], { type: "moveTo", x: 0, y: 5 });
});

test("a path parsed from bad SVG data carries the error; good data and built paths have none", () => {
	assert.equal(so.pathFromSVG("M 0 0 L 10 10").error, null);
	assert.equal(new so.Path().moveTo(0, 0).error, null);
	const bad = so.pathFromSVG("M 0 0 L 10 10 X");
	assert.deepEqual(bad.error, {
		message: "unknown command 'X' at position 14",
	});
	assert.equal(bad.toSVG(), "M 0 0 L 10 10"); // everything before the error
	// Not starting with a moveto: nothing renders.
	assert.equal(so.pathFromSVG("L 10 10").commands.length, 0);
	assert.match(so.pathFromSVG("L 10 10").error.message, /at position 0$/);
});

test("stroking bad SVG data reports the error and strokes what parsed", () => {
	assert.equal(so.buildOutline("M 0 0 L 100 0").error, null);
	assert.equal(
		so.buildOutline(new so.Path().moveTo(0, 0).lineTo(1, 0)).error,
		null,
	);
	const bad = so.buildOutline("M 0 0 L 100 0 X", { lineWidth: 10 });
	assert.deepEqual(bad.error, {
		message: "unknown command 'X' at position 14",
	});
	assert.equal(
		bad.toSVG(),
		so.buildOutline("M 0 0 L 100 0", { lineWidth: 10 }).toSVG(),
	);
});

test("precision is a minimum; small paths keep their detail", () => {
	assert.equal(
		new so.Path().moveTo(0, 0).lineTo(100.123456, 0).toSVG(5),
		"M 0 0 L 100.12346 0",
	);
	assert.match(
		so.buildOutline("M 0 0 L 0.01 0", { lineWidth: 0.001 }).toSVG(),
		/0\.0005/,
	);
});

test("every WASM-side vector the wrapper touches is freed", () => {
	// Record each PathCommandVector the wrapper creates or receives from the
	// module, then check all of them were deleted. (Leaked Embind objects keep
	// their C++ memory forever: there's no garbage collection across the boundary.)
	const m = so.module;
	const seen = [];
	const track = (v) => (seen.push(v), v);
	const saved = {};
	for (const name of [
		"buildStrokeOutline",
		"arcToCommands",
		"parseSvgPathChecked",
	]) {
		saved[name] = m[name];
		m[name] = (...args) => {
			const out = saved[name](...args);
			track(name === "parseSvgPathChecked" ? out.commands : out);
			return out;
		};
	}
	const Vector = m.PathCommandVector;
	// Embind classes can't be subclassed; `new` on a function returning an
	// object yields that object.
	m.PathCommandVector = function () {
		return track(new Vector());
	};
	try {
		const path = new so.Path()
			.moveTo(0, 0)
			.lineTo(50, 0)
			.arcTo(20, 20, 0, false, true, 90, 0);
		so.buildOutline(path, { lineWidth: 4, dash: [5, 3] }).toSVG();
		so.buildOutline(so.pathFromSVG("M 0 0 Q 50 50 100 0 X"), { lineWidth: 2 });
		so.buildOutline("M 0 0 Q 50 50 100 0 X", { lineWidth: 2 });
		path.toSVG();
	} finally {
		Object.assign(m, saved);
		m.PathCommandVector = Vector;
	}
	assert.ok(seen.length >= 8, `expected to see vectors, saw ${seen.length}`);
	assert.ok(
		seen.every((v) => v.isDeleted()),
		"a PathCommandVector was not deleted",
	);
});
