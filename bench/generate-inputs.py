"""Generates the benchmark inputs in bench/inputs/, one SVG path per line:

- glyphs.txt: a paragraph set in Source Sans Pro Regular at 16 px (one path
  per line of text), from the font's outlines (OFL-1.1; see
  bench/inputs/glyphs-LICENSE.md). Its outlines are cubics, as in OpenType/CFF.
- icons.txt: twelve outline icons on a 24-unit grid, written for this
  benchmark, with lines, arcs and curves.
- freehand.txt: one smooth open path of 400 cubics through a random walk.

Each also gets an SVG picture (glyphs.svg, ...) for the README: the glyphs
filled, as text is drawn; the icons and the freehand path stroked at the
benchmark's width.

Needs fontTools and the font file:
  PYTHONPATH=/path/to/fonttools/Lib python3 bench/generate-inputs.py /path/to/SourceSansPro-Regular.otf
The font used is version 2.020, as in HarfBuzz's test fonts
(test/api/fonts/SourceSansPro-Regular.otf).
"""

import math
import random
import re
import sys
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = Path(__file__).parent / "inputs"

TEXT = (
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen "
    "liquor jugs. How vexingly quick daft zebras jump! Sphinx of black quartz, "
    "judge my vow. The five boxing wizards jump quickly. Jackdaws love my big "
    "sphinx of quartz. Bright vixens jump; dozy fowl quack. Quick wafting "
    "zephyrs vex bold Jim. Waltz, bad nymph, for quick jigs vex."
)
SIZE = 16  # px per em
LINE_HEIGHT = 22
WIDTH = 600  # wrap width, px


def num(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def glyphs(font_path):
    font = TTFont(font_path)
    scale = SIZE / font["head"].unitsPerEm
    cmap = font.getBestCmap()
    gs = font.getGlyphSet()
    advance = lambda ch: font["hmtx"][cmap[ord(ch)]][0] * scale
    # Greedy word wrap.
    lines, line = [], ""
    for word in TEXT.split(" "):
        candidate = f"{line} {word}" if line else word
        if sum(advance(c) for c in candidate) > WIDTH and line:
            lines.append(line)
            line = word
        else:
            line = candidate
    lines.append(line)
    out = []
    for i, text in enumerate(lines):
        pen = SVGPathPen(gs, ntos=num)
        x, y = 0.0, (i + 1) * LINE_HEIGHT
        for ch in text:
            name = cmap[ord(ch)]
            # Font units are y-up; the path is y-down, at (x, baseline y).
            gs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
            x += advance(ch)
        out.append(pen.getCommands())
    return out


ICONS = [
    # home
    "M3 10.5 L12 3 L21 10.5 V20 A1 1 0 0 1 20 21 H15 V14 H9 V21 H4 A1 1 0 0 1 3 20 Z",
    # search
    "M10.5 3 A7.5 7.5 0 1 1 10.5 18 A7.5 7.5 0 1 1 10.5 3 Z M16 16 L21 21",
    # heart
    "M12 20 C5 15 2 11.5 2 8 A4.5 4.5 0 0 1 12 6 A4.5 4.5 0 0 1 22 8 C22 11.5 19 15 12 20 Z",
    # gear
    "M12 8.5 A3.5 3.5 0 1 1 12 15.5 A3.5 3.5 0 1 1 12 8.5 Z M12 2 V5 M12 19 V22 M2 12 H5"
    " M19 12 H22 M4.9 4.9 L7 7 M17 17 L19.1 19.1 M4.9 19.1 L7 17 M17 7 L19.1 4.9",
    # bell
    "M6 16 V11 A6 6 0 0 1 18 11 V16 L20 18 H4 Z M10 21 Q12 22.5 14 21",
    # mail
    "M3 6 H21 V18 H3 Z M3 6 L12 13 L21 6",
    # user
    "M12 3 A4 4 0 1 1 12 11 A4 4 0 1 1 12 3 Z M4 21 C4 16 8 14 12 14 C16 14 20 16 20 21",
    # star
    "M12 2.5 L14.9 8.6 L21.5 9.3 L16.6 13.8 L18 20.4 L12 17 L6 20.4 L7.4 13.8 L2.5 9.3"
    " L9.1 8.6 Z",
    # cloud
    "M7 19 H17.5 A4.5 4.5 0 0 0 18 10 A6 6 0 0 0 6.5 9 A5 5 0 0 0 7 19 Z",
    # calendar
    "M4 5 H20 A1 1 0 0 1 21 6 V20 A1 1 0 0 1 20 21 H4 A1 1 0 0 1 3 20 V6 A1 1 0 0 1 4 5 Z"
    " M3 10 H21 M8 3 V7 M16 3 V7",
    # chat bubble
    "M4 4 H20 A2 2 0 0 1 22 6 V15 A2 2 0 0 1 20 17 H9 L4 21 V17 A2 2 0 0 1 2 15 V6"
    " A2 2 0 0 1 4 4 Z",
    # refresh
    "M20 12 A8 8 0 1 1 17.7 6.3 M20 3 V8 H15",
]


def freehand():
    # A random walk, smoothed into cubics through its points (Catmull–Rom).
    rnd = random.Random(7)
    pts, x, y, a = [], 500.0, 400.0, 0.0
    for _ in range(401):
        a += rnd.uniform(-0.9, 0.9)
        # Near the edges, turn back toward the middle (no clamping, which
        # would run straight along them).
        if not (100 < x < 900 and 100 < y < 700):
            toward = math.atan2(400 - y, 500 - x)
            a += 0.5 * math.atan2(math.sin(toward - a), math.cos(toward - a))
        x += 25 * math.cos(a)
        y += 25 * math.sin(a)
        pts.append((x, y))
    d = [f"M{num(pts[0][0])} {num(pts[0][1])}"]
    for i in range(len(pts) - 1):
        p0 = pts[max(i - 1, 0)]
        p1, p2 = pts[i], pts[i + 1]
        p3 = pts[min(i + 2, len(pts) - 1)]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        d.append("C" + " ".join(num(v) for v in (*c1, *c2, *p2)))
    return ["".join(d)]


# The stroke widths the benchmarks use (bench/native/main.cpp, js/bench/bench.mjs).
WIDTHS = {"glyphs": 0.5, "icons": 2, "freehand": 4}
STYLE = (
    "<style>path{fill:none;stroke:#1f2328;stroke-linejoin:miter}"
    ".filled{fill:#1f2328;stroke:none}"
    "@media (prefers-color-scheme:dark){path{stroke:#e6edf3}.filled{fill:#e6edf3}}</style>"
)


def picture(name, paths, view_box, width, transforms=None, filled=False):
    """An SVG of the paths, filled or stroked at the benchmark's width."""
    w = WIDTHS[name]
    # The stroke width stays on filled paths too, for js/bench/pictures.mjs.
    cls = ' class="filled"' if filled else ""
    body = "".join(
        f'<path{cls} transform="{t}" stroke-width="{w}" d="{d}"/>'
        if t
        else f'<path{cls} stroke-width="{w}" d="{d}"/>'
        for d, t in zip(paths, transforms or [None] * len(paths))
    )
    vb = " ".join(num(v) for v in view_box)
    height = width * view_box[3] / view_box[2]
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" '
        f'width="{num(width)}" height="{num(height)}">{STYLE}{body}</svg>\n'
    )


if __name__ == "__main__":
    HERE.mkdir(exist_ok=True)
    (HERE / "glyphs.txt").write_text("\n".join(glyphs(sys.argv[1])) + "\n")
    (HERE / "icons.txt").write_text("\n".join(ICONS) + "\n")
    (HERE / "freehand.txt").write_text("\n".join(freehand()) + "\n")

    lines = glyphs(sys.argv[1])
    (HERE / "glyphs.svg").write_text(
        picture(
            "glyphs", lines, (-2, 2, WIDTH + 4, len(lines) * LINE_HEIGHT + 6), 600, filled=True
        )
    )
    # The icons in a row, 32 units apart.
    (HERE / "icons.svg").write_text(
        picture(
            "icons",
            ICONS,
            (-2, -2, 32 * len(ICONS) - 4, 28),
            32 * len(ICONS) * 1.5,
            [f"translate({32 * i} 0)" for i in range(len(ICONS))],
        )
    )
    # Framed to the path, with a margin.
    nums = [float(v) for v in re.findall(r"-?[0-9.]+", freehand()[0])]
    xs, ys = nums[0::2], nums[1::2]
    box = (min(xs) - 10, min(ys) - 10, max(xs) - min(xs) + 20, max(ys) - min(ys) + 20)
    (HERE / "freehand.svg").write_text(picture("freehand", freehand(), box, 400))
