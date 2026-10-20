#!/usr/bin/env python3
"""POC 031: the atlas of every generated keyboard in every render mode, for review.

    atlas.py [width]   renders keyboards/out/*.svg: each mode into scratch/2026-10-19-poc-031/atlas/, the sheet
                       of each keyboard and style into atlas/

Per keyboard and style: Qt's QSvgRenderer (as unreal-qt draws it), rsvg (as a browser draws it), the picture
without its highlights layer, each named layer alone in Qt, the key map over the Qt picture (the overlay's rectangles and what each key presses) and the Qt vs
rsvg difference. One sheet per keyboard and style puts them side by side.
"""

import json
import re
import pathlib
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from generate import QTRENDER, SCRATCH  # noqa: E402
from styles.common import LAYERS  # noqa: E402

OUT = HERE / "keyboards" / "out"
ATLAS = SCRATCH / "atlas"
SHEETS = HERE / "atlas"
LABEL = "/System/Library/Fonts/Helvetica.ttc"
BACKGROUND = (30, 31, 34)


def font(size):
    return ImageFont.truetype(LABEL, size)


def run(*args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{args[0]}: {result.stderr.strip()}")


def on_background(path, colour=BACKGROUND):
    image = Image.open(path).convert("RGBA")
    base = Image.new("RGBA", image.size, colour + (255,))
    return Image.alpha_composite(base, image).convert("RGB")


def keymap(picture, keys, size_mm):
    """The overlay's view: each key's rectangle and what it presses, over the picture"""
    image = picture.copy()
    draw = ImageDraw.Draw(image, "RGBA")
    scale = image.width / size_mm[0]
    small = font(max(9, int(scale * 2.2)))
    for key in keys:
        x, y, w, h = (v * scale for v in key["rect"])
        combo = "+".join(key.get("zx", [])) or key.get("pc", "") or key.get("line", "")
        colour = (90, 200, 255) if len(key.get("zx", [])) == 1 or "pc" in key or "line" in key else (255, 170, 60)
        if "shape" in key:  # an L-shaped ENTER: its hit area is the polygon, not the bounding rectangle
            draw.polygon([(px * scale, py * scale) for px, py in key["shape"]], outline=colour + (255,), width=2,
                         fill=colour + (40,))
        else:
            draw.rectangle((x + 2, y + 2, x + w - 2, y + h - 2), outline=colour + (255,), width=2,
                           fill=colour + (40,))
        draw.text((x + 5, y + h - 5), combo, fill=(255, 255, 255, 255), font=small, anchor="ls")
    return image


def difference(a, b):
    diff = ImageChops.difference(a, b.resize(a.size)).convert("L")
    return Image.eval(diff, lambda v: min(255, v * 8)).convert("RGB")


def sheet(title, tiles, columns=2):
    """@p tiles (caption, image) in a grid under @p title"""
    width = tiles[0][1].width
    height = tiles[0][1].height
    pad, caption = 24, 34
    rows = (len(tiles) + columns - 1) // columns
    page = Image.new("RGB", (columns * width + (columns + 1) * pad, 70 + rows * (height + caption + pad)), (18, 18, 20))
    draw = ImageDraw.Draw(page)
    draw.text((pad, 20), title, fill=(230, 230, 230), font=font(30))
    for i, (name, image) in enumerate(tiles):
        x = pad + (i % columns) * (width + pad)
        y = 70 + (i // columns) * (height + caption + pad)
        draw.text((x, y + 4), name, fill=(160, 160, 160), font=font(20))
        page.paste(image.resize((width, height)), (x, y + caption))
    return page


def main():
    width = int(sys.argv[1]) if len(sys.argv) > 1 else 1600
    ATLAS.mkdir(parents=True, exist_ok=True)
    SHEETS.mkdir(exist_ok=True)
    for svg in sorted(OUT.glob("*.svg")):
        keyboard, style = svg.stem.split(".", 1)
        keys = json.loads((OUT / f"{keyboard}.keys.json").read_text())
        stem = ATLAS / svg.stem
        run(QTRENDER, svg, f"{stem}.qt.png", width)
        run("rsvg-convert", "-w", width, svg, "-o", f"{stem}.rsvg.png")
        qt, rsvg = on_background(f"{stem}.qt.png"), on_background(f"{stem}.rsvg.png")
        tiles = [("Qt QSvgRenderer", qt), ("rsvg (browser)", rsvg)]
        text, layers = svg.read_text(), {}
        for layer in LAYERS:
            if re.search(rf'<g id="{layer}"[^>]*>\n\n</g>', text):
                continue  # an empty layer: the hints come with phase 3
            path = f"{stem}.layer-{layer}.png"
            run(QTRENDER, svg, path, width, layer)
            layers[layer] = Image.open(path).convert("RGBA")
            tiles.append((f"layer: {layer} (Qt)", on_background(path)))
        if "highlights" in layers:  # what an app draws with the highlights off: every other layer, in order
            plain = Image.new("RGBA", qt.size, BACKGROUND + (255,))
            for layer, image in layers.items():
                if layer != "highlights":
                    plain = Image.alpha_composite(plain, image)
            plain = plain.convert("RGB")
            plain.save(f"{stem}.no-highlights.png")
            tiles.insert(2, ("without the highlights layer (Qt, layer by layer)", plain))
        mapped = keymap(qt, keys["keys"], keys["size"])
        mapped.save(f"{stem}.keymap.png")
        diff = difference(qt, rsvg)
        diff.save(f"{stem}.qt-vs-rsvg.png")
        tiles += [("key map: blue = one matrix key, orange = a combo", mapped), ("Qt vs rsvg, difference x8", diff)]
        sheet(f"{keys['id']} · {style} · {', '.join(keys['models'])} · {len(keys['keys'])} keys", tiles).save(
            SHEETS / f"{svg.stem}.atlas.png")
        print(f"{svg.stem}: {len(tiles)} modes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
