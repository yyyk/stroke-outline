// Copy the WASM module (published into src/wasm/ by the cmake `wasm` build; the
// .wasm is embedded in the .js) next to the compiled wrapper in dist/wasm/, and
// the repository's license files into the package root.
import { copyFileSync, existsSync, mkdirSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const from = join(root, 'src', 'wasm');
const to = join(root, 'dist', 'wasm');
const files = ['stroke_outline.js', 'stroke_outline.d.ts'];

for (const file of files)
  if (!existsSync(join(from, file))) {
    console.error(
      `missing ${join('src', 'wasm', file)}: build the WASM module first\n` +
        '  cmake --preset wasm && cmake --build --preset wasm-release',
    );
    process.exit(1);
  }
rmSync(to, { recursive: true, force: true }); // no stale files in the package
mkdirSync(to, { recursive: true });
for (const file of files) copyFileSync(join(from, file), join(to, file));

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
