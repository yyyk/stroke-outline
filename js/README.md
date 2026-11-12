# stroke-outline

Turn vector paths into the filled outline of their stroke — the geometry a
canvas draws for `stroke()`, returned as a path you can fill, cache, hit-test
or tessellate. A WebAssembly build of a C++ library; works in browsers, workers
and Node.

```js
import { init } from 'stroke-outline';

const so = await init(); // loads the WASM once; reuse the result

// stroke SVG path data
const outline = so.buildOutline('M 10 10 H 90 A 20 20 0 0 1 90 50', {
  lineWidth: 8,
  lineCap: 'round',
});
outline.toSVG(); // SVG path data — <path d>, new Path2D(...), a file
outline.commands; // [{ type: 'moveTo', x, y }, { type: 'cubicTo', c1x, ... }, ...]
outline.error; // null, or { message } if the SVG data was malformed

// parse SVG once, then stroke it as often as you like
const icon = so.pathFromSVG('M 0 0 H 100 V 100 H 0 Z M 30 30 V 70 H 70 V 30 Z');
const thin = so.buildOutline(icon, { lineWidth: 2 });
const thick = so.buildOutline(icon, { lineWidth: 12, align: 'inside' });
thinEl.setAttribute('d', thin.toSVG());
thickEl.setAttribute('d', thick.toSVG());

// or build paths in code
const path = new so.Path().moveTo(0, 0).lineTo(100, 0).arcTo(30, 30, 0, false, true, 100, 60);
const dashed = so.buildOutline(path, { lineWidth: 6, align: 'inside', dash: [12, 6] });
ctx.fill(new Path2D(dashed.toSVG())); // each dash is its own subpath
```

**Fill outlines with the nonzero rule** (the default for canvas `fill()` and
SVG). Outlines may overlap themselves; nonzero renders that correctly. With
`clipToShape: true`, closed paths come out as simple contours that also fill
correctly with even-odd.

## API

- `init()` → `Promise<StrokeOutline>`. Loads the WASM module; call it once and
  reuse the result.
- `so.buildOutline(pathOrSvg, options?)` → the outline as a `Path`. Takes a `Path` or
  SVG path data; malformed data is stroked up to the first error, and the
  outline's `error` says why (as for `pathFromSVG`, below).
- `new so.Path()` with `moveTo`, `lineTo`, `quadTo`, `cubicTo`,
  `arcTo(rx, ry, xAxisRotationDeg, largeArc, sweep, x, y)` and `close`, all
  chainable; `.commands` and `.toSVG(precision = 3)`.
- `so.pathFromSVG(d)` → `Path`. Malformed path data keeps everything before
  the first error (as SVG renders it), and `path.error` is
  `{ message }`, saying what went wrong and where
  (`'expected a number at position 11'`, an index into `d`). `error` is `null`
  for valid data and for paths built in code. Use it to parse once and stroke
  many times, or to read the input's commands.
- `precision` is a minimum number of decimals; small paths get more, so
  rounding stays within ~1e-4 of their size.
- `so.module` is the raw Embind module, for anything not wrapped here.

Nothing needs freeing: the wrapper releases every WASM-side object itself.

### Options

All optional; omitted ones take these defaults.

| Option        | Default     |                                                                                                    |
| ------------- | ----------- | -------------------------------------------------------------------------------------------------- |
| `lineWidth`   | `1`         | must be a positive finite number, else nothing is stroked                                          |
| `lineJoin`    | `'miter'`   | `'miter'`, `'bevel'`, `'round'`                                                                    |
| `lineCap`     | `'butt'`    | `'butt'`, `'square'`, `'round'`                                                                    |
| `miterLimit`  | `4`         | below 1 behaves as 1 (always bevel)                                                                |
| `align`       | `'center'`  | `'center'`, `'inside'`, `'outside'` — closed subpaths, relative to the whole fill (holes included) |
| `fillRule`    | `'nonzero'` | `'nonzero'`, `'evenodd'` — how subpaths form the fill, for `align` and `clipToShape`               |
| `clipToShape` | `false`     | resolve over-wide or overlapping strokes into simple contours                                      |
| `dash`        | none        | e.g. `[10, 5]`; `[0, gap]` with round caps draws dots                                              |
| `dashOffset`  | `0`         |                                                                                                    |

An unknown option value (e.g. `lineJoin: 'rounded'`) throws a `TypeError`.

## Bundlers

The WebAssembly binary is embedded in the package's JavaScript, so there is no
`.wasm` file to serve or configure: it works as is in Vite (dev and build),
webpack, Rollup, esbuild and Node.

## Building from source

```sh
# from the repository root: build the WASM module (publishes it into js/src/wasm)
cmake --preset wasm && cmake --build --preset wasm-release
cd js
npm install
npm test        # builds dist/ and runs the tests in Node
npm pack        # the tarball that would be published
```

## License

MIT — see `LICENSE`. Includes code ported from svg-arc-to-cubic-bezier
(Colin Meinke, ISC license) and based on bezierjs (Pomax, MIT license); see `THIRD_PARTY_NOTICES`.
