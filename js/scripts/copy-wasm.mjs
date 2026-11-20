// Prepare the WASM module for the package: from what the cmake `wasm` build
// published into src/wasm/ (the module's .js, its .wasm and their .d.ts), write
// the base64 copy of the .wasm again (embed-wasm.mjs: cmake already does, but
// this keeps the package build complete on its own), then copy all of it next
// to the compiled wrapper in dist/wasm/, and the repository's license files
// into the package root. Runs before tsc, which needs the base64 module's
// declaration.
import { copyFileSync, existsSync, mkdirSync, rmSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { BASE64_MODULE, embedWasm } from './embed-wasm.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const from = join(root, 'src', 'wasm');
const to = join(root, 'dist', 'wasm');
const built = ['stroke_outline.js', 'stroke_outline.wasm', 'stroke_outline.d.ts'];

for (const file of built)
	if (!existsSync(join(from, file))) {
		console.error(
			`missing ${join('src', 'wasm', file)}: build the WASM module first\n` +
				'  cmake --preset wasm && cmake --build --preset wasm-release',
		);
		process.exit(1);
	}
// A Release .wasm is ~0.25 MB; a Debug one is several MB (unoptimized, with
// debug info) and must never be packaged. 0.1.0 was published that way by accident.
const MAX_WASM_BYTES = 2_000_000;
const size = statSync(join(from, 'stroke_outline.wasm')).size;
if (size > MAX_WASM_BYTES) {
	console.error(
		`src/wasm/stroke_outline.wasm is ${(size / 1e6).toFixed(1)} MB: that looks like a Debug build.\n` +
			'  cmake --build --preset wasm-release',
	);
	process.exit(1);
}

embedWasm(from);

rmSync(to, { recursive: true, force: true }); // no stale files in the package
mkdirSync(to, { recursive: true });
for (const file of [...built, `${BASE64_MODULE}.js`, `${BASE64_MODULE}.d.ts`])
	copyFileSync(join(from, file), join(to, file));

// The repository's license files go into the package root too (npm only packs
// files inside js/).
for (const file of ['LICENSE', 'THIRD_PARTY_NOTICES']) {
	const source = join(root, '..', file);
	if (!existsSync(source)) {
		console.error(`missing ${file} at the repository root`);
		process.exit(1);
	}
	copyFileSync(source, join(root, file));
}
