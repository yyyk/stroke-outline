// Tests for init(): every way of handing it the WebAssembly binary, against the
// built package (dist/).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { init } from '../dist/index.js';

const wasmPath = fileURLToPath(new URL('../dist/wasm/stroke_outline.wasm', import.meta.url));
const bytes = readFileSync(wasmPath);
const dataUrl = `data:application/wasm;base64,${bytes.toString('base64')}`;

// The same stroke from every API, compared with the one from the embedded binary.
const PATH = 'M 10 10 H 90 A 20 20 0 0 1 90 50';
const OPTIONS = { lineWidth: 8, lineCap: 'round' };
const expected = (await init()).buildOutline(PATH, OPTIONS).toSVG();
const strokes = so => so.buildOutline(PATH, OPTIONS).toSVG();

/** Fails instead of waiting forever if `promise` never settles. */
function within(promise, ms = 5000) {
	let timer;
	const timeout = new Promise((_, reject) => {
		timer = setTimeout(() => reject(new Error(`still pending after ${ms} ms`)), ms);
	});
	return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

test('with no argument, uses the binary embedded in the package', async () => {
	assert.equal(strokes(await init()), expected);
});

test('takes the bytes: a Uint8Array, an ArrayBuffer or a DataView', async () => {
	const copy = new Uint8Array(bytes);
	assert.equal(strokes(await init(copy)), expected);
	assert.equal(strokes(await init(copy.buffer)), expected);
	assert.equal(strokes(await init(new DataView(copy.buffer))), expected);
});

test('takes a URL, as a string or a URL object, and fetches it', async () => {
	assert.equal(strokes(await init(dataUrl)), expected);
	assert.equal(strokes(await init(new URL(dataUrl))), expected);
});

test('takes a Response, labelled as WebAssembly or not', async () => {
	const labelled = new Response(bytes, { headers: { 'Content-Type': 'application/wasm' } });
	assert.equal(strokes(await init(labelled)), expected);
	// without the label, streaming compilation refuses it: compiled from the whole body
	assert.equal(strokes(await init(new Response(bytes))), expected);
});

test('takes a compiled WebAssembly.Module, and can reuse it', async () => {
	const compiled = await WebAssembly.compile(bytes);
	assert.equal(strokes(await init(compiled)), expected);
	assert.equal(strokes(await init(compiled)), expected);
});

test('takes a promise of any of them', async () => {
	assert.equal(strokes(await init(Promise.resolve(new Uint8Array(bytes)))), expected);
	assert.equal(strokes(await init(fetch(dataUrl))), expected);
});

test('rejects, rather than waiting forever, when the binary is not valid', async () => {
	await assert.rejects(within(init(new Uint8Array([0, 1, 2, 3]))), WebAssembly.CompileError);
	await assert.rejects(within(init('data:application/wasm;base64,AAAA')), error => {
		assert.doesNotMatch(error.message, /still pending/);
		return true;
	});
});

test('rejects on an HTTP error, saying which', async () => {
	const notFound = new Response('not found', { status: 404 });
	await assert.rejects(within(init(notFound)), /404/);
});

test('exports the .wasm from the package, for bundlers to serve', () => {
	// resolved through the package's own "exports", as an app would
	const resolved = fileURLToPath(import.meta.resolve('stroke-outline/stroke_outline.wasm'));
	assert.ok(existsSync(resolved));
	assert.deepEqual(readFileSync(resolved), bytes);
});
