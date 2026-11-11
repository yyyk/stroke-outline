# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/) (while the version is 0.x, minor
versions may change the API).

## [0.1.3] - 2026-11-11

### Fixed

- `Path.arcTo` (and C++ `Path::arc_to`) no longer adds a zero-length line when
  the arc ends where it starts; like an SVG `A` command with identical end
  points, it now adds nothing (SVG spec F.6.2).

## [0.1.2] - 2026-11-09

### Fixed

- Browser bundlers (e.g. Vite) no longer warn that `node:module` "has been
  externalized for browser compatibility" when building an app that uses the
  package. Node 20+ is still supported.

### Changed

- The npm package's homepage link now points to the docs site,
  https://yyyk.github.io/stroke-outline/.

## [0.1.1] - 2026-11-09

### Fixed

- The npm package of 0.1.0 contained a debug build of the WebAssembly module
  by mistake: about 9.4 MB unpacked instead of 0.3 MB, and unoptimized, so
  slower. 0.1.1 is the optimized build; the API and results are unchanged. The
  C++ library is unaffected.

## [0.1.0] - 2026-11-09

First release: a C++20 library, and an npm package built from it for
WebAssembly.

### Stroking

- Turns a path into the outline of its stroke, to be filled with the nonzero
  rule. Lines, quadratic and cubic Béziers and SVG arcs in; curves stay curves
  in the outline.
- Joins: miter (with a miter limit), bevel and round. Caps: butt, square and
  round. Zero-length subpaths draw dots with round or square caps.
- Dashes with an offset, restarting at every subpath, with dots for zero-length
  dashes. Patterns that would produce more than about 100,000 dashes stroke
  solid.
- Inside and outside strokes of closed subpaths, relative to the fill of the
  whole path (holes included) under the nonzero or even-odd rule.
- `clip_to_shape` / `clipToShape`: resolves strokes wider than the shape, or
  overlapping each other, into simple contours that fill the same under
  nonzero and even-odd.
- Results are the same at any scale.

### SVG

- Full SVG path syntax in. Malformed data is used up to the first error, as SVG
  renders it, and the error says what went wrong and where
  (`"expected a number at position 11"`).
- SVG path data out, with at least the requested number of decimals and more
  for small paths.

### C++ API

- `build_outline(path, settings)` and `build_outline(d, settings[, error])`
  return the outline as a `Path`.
- `Path`: a builder (`move_to`, `line_to`, `quad_to`, `cubic_to`, `arc_to`,
  `close`), `Path::from_svg`, `to_svg` and `for_each`.
- CMake install rules and a package: `find_package(stroke_outline)`, target
  `stroke_outline::stroke_outline`.

### JavaScript API (npm package `stroke-outline`)

- `init()`, then `buildOutline(path | d, options?)` returns the outline as a
  `Path` with `commands`, `toSVG()` and `error`.
- `Path` builder and `pathFromSVG(d)`. Options are partial, with defaults;
  unknown values throw a `TypeError`.
- TypeScript types included. The WebAssembly binary is embedded, so it works
  in browsers, workers and Node 20+ with no bundler setup.

[0.1.3]: https://github.com/yyyk/stroke-outline/releases/tag/v0.1.3
[0.1.2]: https://github.com/yyyk/stroke-outline/releases/tag/v0.1.2
[0.1.1]: https://github.com/yyyk/stroke-outline/releases/tag/v0.1.1
[0.1.0]: https://github.com/yyyk/stroke-outline/releases/tag/v0.1.0
