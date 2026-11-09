import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';

const local = (path: string) => fileURLToPath(new URL(path, import.meta.url));

export default defineConfig({
	// GitHub Pages serves a project site under /<repo>/; the deploy workflow sets
	// BASE_PATH. Locally the site is served from the root.
	base: process.env.BASE_PATH ?? '/',
	resolve: {
		// Use the npm package straight from its source in js/, so the site always
		// matches the library (the WASM module is published into js/src/wasm by
		// the cmake `wasm` build).
		alias: { 'stroke-outline': local('../js/src/index.ts') },
	},
	build: {
		target: 'esnext', // top-level await
	},
});
