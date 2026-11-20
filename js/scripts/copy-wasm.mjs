// Prepare the WASM module for the package: from what the cmake `wasm` build
// published into src/wasm/ (the module's .js, its .wasm and their .d.ts), write
// the .wasm as base64 into a module of its own (src/wasm/stroke_outline_wasm_base64.js,
// which init() imports only when it isn't given the .wasm), then copy all of it
// next to the compiled wrapper in dist/wasm/, and the repository's license
// files into the package root. Runs before tsc, which needs the base64
// module's declaration.
import {
	copyFileSync,
	existsSync,
	mkdirSync,
	readFileSync,
	rmSync,
	statSync,
	writeFileSync,
} from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const from = join(root, 'src', 'wasm');
const to = join(root, 'dist', 'wasm');
const built = ['stroke_outline.js', 'stroke_outline.wasm', 'stroke_outline.d.ts'];
const base64Module = 'stroke_outline_wasm_base64';

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

// The embedded copy, for init() without a .wasm: in a module of its own, so
// bundlers put it in its own chunk, which apps that pass the .wasm never load.
const base64 = readFileSync(join(from, 'stroke_outline.wasm')).toString('base64');
writeFileSync(
	join(from, `${base64Module}.js`),
	'// The WebAssembly binary as base64, for init() when it is not given the .wasm.\n' +
		`export default ${JSON.stringify(base64)};\n`,
);
writeFileSync(
	join(from, `${base64Module}.d.ts`),
	'declare const base64: string;\nexport default base64;\n',
);

rmSync(to, { recursive: true, force: true }); // no stale files in the package
mkdirSync(to, { recursive: true });
for (const file of [...built, `${base64Module}.js`, `${base64Module}.d.ts`])
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
