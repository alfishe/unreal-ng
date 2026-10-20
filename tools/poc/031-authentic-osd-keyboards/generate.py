#!/usr/bin/env python3
"""POC 031: draws the keyboard of every model in each style, with its key map, and the gallery.

    generate.py [names]           write keyboards/out/<id>.<style>.svg, <id>.keys.json, gallery.html
    generate.py --check [names]   also check the maps and render each SVG with Qt (qtrender) and rsvg into
                                  scratch/2026-10-19-poc-031/render/

A keyboard is a module in keyboards/ with `KEYBOARD` (id, name, models, size in mm, matrix) and `layout()`, which
returns the model: data only, no drawing (model.py). A style (styles/) draws a model; every SVG has the same named
layers (`case`, `caps`, `highlights`, `legends`, `hints`), so a hybrid can take each layer from another style, and Qt can draw one
layer alone (QSvgRenderer::render(painter, elementId)).
"""

import argparse
import importlib
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from model import ZX_MATRIX  # noqa: E402

OUT = HERE / "keyboards" / "out"
REPO = HERE.parents[2]
SCRATCH = REPO / "scratch" / "2026-10-19-poc-031"
QTRENDER = SCRATCH / "qtrender-build" / "qtrender"

KEYBOARDS = ["zx48k", "zx128", "zxplus2", "zxplus2a", "zxplus3", "pentagon", "pentagon2plus", "scorpion", "quorum", "orel", "zxnext"]
STYLES = ["photo"]


def check(keyboard, model):
    """Every key presses something; with a ZX matrix, each of the 40 positions is one key's alone (or as many as
    `twins` says: the Next's two CAPS SHIFT and two SYMBOL SHIFT keys)"""
    errors = []
    for key in model.keys:
        if not key.zx and not key.pc and not key.line:
            errors.append(f"{key.id}: presses nothing")
    for i, a in enumerate(model.keys):  # the overlay's hit areas: no point belongs to two keys
        for b in model.keys[i + 1:]:
            if overlap(area(a), area(b)) > 0.01:
                errors.append(f"{a.id} and {b.id} overlap")
    if keyboard.get("matrix") == "zx":
        alone = [k.zx[0] for k in model.keys if k.zx and len(k.zx) == 1]
        for row, keys in ZX_MATRIX.items():
            for name in keys:
                if alone.count(name) != keyboard.get("twins", {}).get(name, 1):
                    errors.append(f"matrix {row} {name}: {alone.count(name)} keys")
    return errors


def area(key):
    """A key's hit area as rectangles: its rect, or for an L shape the leg and the bar"""
    if not key.shape:
        return [key.rect]
    (lx, ly), (rx, _), (_, by), (bx, _), (_, my), _ = key.shape
    return [(lx, ly, rx - lx, my - ly), (bx, my, rx - bx, by - my)]


def overlap(first, second):
    total = 0.0
    for ax, ay, aw, ah in first:
        for bx, by, bw, bh in second:
            total += max(0, min(ax + aw, bx + bw) - max(ax, bx)) * max(0, min(ay + ah, by + bh) - max(ay, by))
    return total


def render(path):
    SCRATCH.joinpath("render").mkdir(parents=True, exist_ok=True)
    stem = SCRATCH / "render" / path.stem
    ok = subprocess.run(["rsvg-convert", "-w", "1600", str(path), "-o", f"{stem}.rsvg.png"]).returncode == 0
    if QTRENDER.exists():
        ok &= subprocess.run([str(QTRENDER), str(path), f"{stem}.qt.png", "1600"]).returncode == 0
        ok &= same(f"{stem}.qt.png", f"{stem}.rsvg.png", path.name)
    return ok


def same(a, b, name, limit=2.0):
    """Qt and the browser renderer draw the picture alike: the mean difference per channel under @p limit of 255,
    and no more than 1 % of the pixels off by more than 64. Both are blurred by a pixel first: the check is for what
    one renderer leaves out, not for their antialiasing of fine detail (heatsink fins, small print)"""
    from PIL import Image, ImageChops, ImageFilter
    qt, rsvg = (Image.open(p).convert("RGB").filter(ImageFilter.GaussianBlur(1)) for p in (a, b))
    if qt.size != rsvg.size:
        rsvg = rsvg.resize(qt.size)
    diff = ImageChops.difference(qt, rsvg)
    values = list(diff.convert("L").getdata())
    mean = sum(values) / len(values)
    far = sum(v > 64 for v in values) / len(values)
    print(f"{name}: Qt vs rsvg mean {mean:.2f}, {far * 100:.2f} % far")
    return mean < limit and far < 0.01


def gallery(built):
    rows = []
    for keyboard, model in built:
        images = "".join(f'<figure><img src="keyboards/out/{keyboard["id"]}.{style}.svg"><figcaption>{style}'
                         f"</figcaption></figure>" for style in STYLES)
        rows.append(f'<section><h2>{keyboard["name"]}</h2><p>{", ".join(keyboard["models"])} · '
                    f"{len(model.keys)} keys</p>{images}</section>")
    return ("<!doctype html><meta charset=utf-8><title>POC 031 keyboards</title><style>"
            "body{background:#1e1f22;color:#ddd;font:14px sans-serif;margin:24px}section{margin:0 0 48px}"
            "figure{margin:0 0 16px}img{width:100%;max-width:1400px;display:block}figcaption{color:#888}"
            "h2{margin:0 0 4px;font-weight:600}p{margin:0 0 10px;color:#999}</style>"
            "<h1>POC 031: authentic keyboards</h1>" + "".join(rows))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    built, failed = [], False
    for name in args.names or KEYBOARDS:
        module = importlib.import_module(f"keyboards.{name}")
        keyboard, model = module.KEYBOARD, module.layout()
        (OUT / f"{name}.keys.json").write_text(json.dumps(model.keymap(keyboard), indent=1, ensure_ascii=False) + "\n")
        for style in STYLES:
            svg = importlib.import_module(f"styles.{style}").draw(keyboard, model)
            path = OUT / f"{name}.{style}.svg"
            path.write_text(svg)
            if args.check:
                failed |= not render(path)
        if args.check:
            for error in check(keyboard, model):
                print(f"{name}: {error}")
                failed = True
        built.append((keyboard, model))
    if not args.names:
        (HERE / "gallery.html").write_text(gallery(built))
    print(f"wrote {len(built)} keyboards x {len(STYLES)} styles")
    return 1 if failed else 0


if __name__ == "__main__":
    os.chdir(HERE)
    sys.exit(main())
