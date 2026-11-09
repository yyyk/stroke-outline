import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';

const local = (path: string) => fileURLToPath(new URL(path, import.meta.url));

export default defineConfig({
	// Relative base so the built site works from any sub-path.
	base: './',
	resolve: {
		// The npm package straight from its source in js/ (the WASM module is
		// published into js/src/wasm by the cmake `wasm` build).
		alias: { 'stroke-outline': local('../../js/src/index.ts') },
	},
	build: {
		target: 'esnext', // top-level await in lib.ts
		outDir: 'dist',
	},
});
