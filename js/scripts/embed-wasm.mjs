// Write the WebAssembly binary as base64 into a module of its own,
// stroke_outline_wasm_base64.js (with its .d.ts), next to stroke_outline.wasm
// in `dir`: the copy init() imports only when it isn't given the .wasm.
//
// The cmake `wasm` build runs it after publishing the module into js/src/wasm,
// so that folder is complete for anything compiling the package from source
// (the docs site, the web sandbox); the package build runs it again.
//
//   node scripts/embed-wasm.mjs [dir]   (default: src/wasm)
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const BASE64_MODULE = 'stroke_outline_wasm_base64';

export function embedWasm(dir) {
	const base64 = readFileSync(join(dir, 'stroke_outline.wasm')).toString('base64');
	writeFileSync(
		join(dir, `${BASE64_MODULE}.js`),
		'// The WebAssembly binary as base64, for init() when it is not given the .wasm.\n' +
			`export default ${JSON.stringify(base64)};\n`,
	);
	writeFileSync(
		join(dir, `${BASE64_MODULE}.d.ts`),
		'declare const base64: string;\nexport default base64;\n',
	);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	const root = join(dirname(fileURLToPath(import.meta.url)), '..');
	embedWasm(resolve(process.argv[2] ?? join(root, 'src', 'wasm')));
}
