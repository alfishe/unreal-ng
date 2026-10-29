#!/usr/bin/env python3
"""POC 033: render the controller pictures for review and check Qt against rsvg.

    render.py [set ...]     every set (or the named ones) of generate.SETS

Per set, into scratch/2026-10-23-poc-033/:
  <set>/qt.png, rsvg.png        the SVG alone at 1600 px, Qt's QSvgRenderer (PySide6) and rsvg-convert
  <set>/sheet.png               the review sheet: the dialog's picture on the dark and the light theme, every control
                                held at once, the hotspot map, Qt vs rsvg
  <set>/controls.png            one tile per control, that control alone held, as the dialog lights it
  overview.png                  every set on the dark and the light theme
  check.json                    Qt vs rsvg per set: mean absolute difference per channel, the share of pixels off
                                by more than 32

The dialog's painting is ControllerPicture::paintEvent (unreal-qt/src/controllers/controllerpicture.cpp) redone with
the same constants: 170 px callout columns, the lit colour (255, 170, 0, 150), the selected colour, the stick dot, the
trigger fill. A change there must be mirrored here.
"""

import json
import os
import pathlib
import subprocess
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PIL import Image, ImageChops, ImageDraw, ImageFont  # noqa: E402
from PySide6.QtCore import QPointF, QRectF, Qt  # noqa: E402
from PySide6.QtGui import (QColor, QFont, QFontMetrics, QGuiApplication, QImage, QPainter, QPen,  # noqa: E402
                           QPolygonF)
from PySide6.QtSvg import QSvgRenderer  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import generate  # noqa: E402

SCRATCH = generate.ROOT / "scratch" / "2026-10-23-poc-033"
LABEL = "/System/Library/Fonts/Helvetica.ttc"
THEMES = {  # the window, the text, Mid (leader lines), Highlight (an active callout's text)
    "dark": {"window": (40, 41, 44), "text": (230, 230, 232), "mid": (110, 112, 118), "highlight": (64, 150, 255)},
    "light": {"window": (236, 236, 236), "text": (28, 28, 30), "mid": (160, 160, 164), "highlight": (10, 96, 220)},
}
CALLOUT = 170
PRESSED = 0.5

app = QGuiApplication.instance() or QGuiApplication(sys.argv)


def font(size):
    return ImageFont.truetype(LABEL, size)


def qimage_to_pil(image):
    image = image.convertToFormat(QImage.Format.Format_RGBA8888)
    data = bytes(image.constBits())
    return Image.frombuffer("RGBA", (image.width(), image.height()), data, "raw", "RGBA", image.bytesPerLine(), 1)


def render_qt(svg_path, width):
    renderer = QSvgRenderer(str(svg_path))
    size = renderer.viewBoxF().size()
    image = QImage(width, round(width * size.height() / size.width()), QImage.Format.Format_ARGB32_Premultiplied)
    image.fill(Qt.GlobalColor.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    renderer.render(painter)
    painter.end()
    return qimage_to_pil(image)


def render_rsvg(svg_path, width, out):
    subprocess.run(["rsvg-convert", "-w", str(width), str(svg_path), "-o", str(out)], check=True)
    return Image.open(out).convert("RGBA")


def on(color, image):
    base = Image.new("RGBA", image.size, color + (255,))
    return Image.alpha_composite(base, image).convert("RGB")


def is_pressed(value):
    return value >= PRESSED or value <= -PRESSED


def dialog(svg_path, hotspots, theme, live=None, selected=None, width=1100, height=600, captions=None):
    """ControllerPicture::paintEvent at @p width x @p height on @p theme with the live values @p live and the
    callouts' @p captions (control -> the binding's text)"""
    live = live or {}
    captions = captions or {}
    colors = THEMES[theme]
    image = QImage(width, height, QImage.Format.Format_ARGB32_Premultiplied)
    image.fill(QColor(*colors["window"]))
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    qfont = QFont("Helvetica", 12)
    painter.setFont(qfont)
    renderer = QSvgRenderer(str(svg_path))
    size_w, size_h = hotspots["size"]
    area = QRectF(CALLOUT, 4, width - 2 * CALLOUT, height - 8)
    scale = min(area.width() / size_w, area.height() / size_h)
    img = QRectF(area.center().x() - size_w * scale / 2, area.center().y() - size_h * scale / 2, size_w * scale,
                 size_h * scale)
    renderer.render(painter, img)
    lit = QColor(255, 170, 0, 150)
    sel = QColor(0, 140, 255, 120)

    def to_widget(x, y):
        return QPointF(img.left() + x * scale, img.top() + y * scale)

    spots = []
    for c in hotspots["controls"]:
        shape = c["shape"]
        polygon = None
        if "circle" in shape:
            cx, cy, r = shape["circle"]
            rect, circle = QRectF(cx - r, cy - r, 2 * r, 2 * r), True
        elif "polygon" in shape:
            polygon = QPolygonF([QPointF(x, y) for x, y in shape["polygon"]])
            rect, circle = polygon.boundingRect(), False
        else:
            rect, circle = QRectF(*shape["rect"]), False
        analog = c.get("analog", {})
        spots.append({"control": c["control"], "label": c.get("label", c["control"]), "rect": rect, "circle": circle, "polygon": polygon,
                      "anchor": QPointF(*c["anchor"]), "left": c.get("callout", "left") == "left",
                      "analog": analog.get("kind", ""), "pair": analog.get("pair", ""),
                      "radius": analog.get("radius", 0), "quiet": c.get("quiet", False)})
    value = lambda name: live.get(name, 0.0)  # noqa: E731
    for s in spots:
        box = QRectF(to_widget(s["rect"].left(), s["rect"].top()), s["rect"].size() * scale)
        v = value(s["control"])
        if s["analog"] == "trigger":
            if v > 0.02:
                fill = QRectF(box)
                fill.setHeight(box.height() * min(max(v, 0), 1))
                painter.fillRect(fill, lit)
        elif s["analog"] == "stick":
            centre = box.center()
            reach = s["radius"] * scale if s["radius"] > 0 else box.width() / 2
            dot = QPointF(centre.x() + v * reach, centre.y() + value(s["pair"]) * reach)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(lit if is_pressed(v) or is_pressed(value(s["pair"])) else QColor(255, 255, 255, 90))
            painter.drawEllipse(dot, 6, 6)
        elif is_pressed(v) or s["control"] == selected:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(lit if is_pressed(v) else sel)
            if s["circle"]:
                painter.drawEllipse(box)
            elif s["polygon"] is not None:
                painter.drawPolygon(QPolygonF([to_widget(q.x(), q.y()) for q in s["polygon"]]))
            else:
                painter.drawRoundedRect(box, 6, 6)
    metrics = QFontMetrics(qfont)
    line = metrics.height() + 2
    for left in (True, False):
        side = [s for s in spots if s["left"] == left and not s["quiet"]]
        side.sort(key=lambda s: s["anchor"].y())
        if not side:
            continue
        step = max(line, (height - 8.0) / len(side))
        y = 4 + step / 2
        for s in side:
            active = s["analog"] != "encoder" and is_pressed(value(s["control"])) or s["control"] == selected
            label = (QRectF(2, y - line / 2, CALLOUT - 10, line) if left
                     else QRectF(width - CALLOUT + 8, y - line / 2, CALLOUT - 10, line))
            anchor = to_widget(s["anchor"].x(), s["anchor"].y())
            end = QPointF(label.right() + 2 if left else label.left() - 2, y)
            pen = QColor(255, 140, 0) if active else QColor(*colors["mid"])
            painter.setPen(QPen(pen, 1.6 if active else 0.8))
            painter.drawLine(end, anchor)
            painter.setPen(QColor(*(colors["highlight"] if active else colors["text"])))
            painter.drawText(label, (Qt.AlignmentFlag.AlignRight if left else Qt.AlignmentFlag.AlignLeft)
                             | Qt.AlignmentFlag.AlignVCenter,
                             s["label"] + ("  " + captions[s["control"]] if captions.get(s["control"]) else ""))
            y += step
    painter.end()
    return qimage_to_pil(image).convert("RGB")


def all_held(hotspots):
    live = {}
    for c in hotspots["controls"]:
        kind = c.get("analog", {}).get("kind")
        if kind == "stick":
            live[c["control"]] = 0.7
            live[c["analog"]["pair"]] = -0.7
        else:
            live[c["control"]] = 1.0
    return live


def hotspot_map(picture, hotspots):
    image = picture.copy()
    draw = ImageDraw.Draw(image, "RGBA")
    k = image.width / hotspots["size"][0]
    small = font(max(11, int(image.width / 110)))
    for c in hotspots["controls"]:
        shape = c["shape"]
        colour = (90, 200, 255)
        if "circle" in shape:
            cx, cy, r = (v * k for v in shape["circle"])
            draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=colour + (255,), width=2, fill=colour + (40,))
        elif "polygon" in shape:
            draw.polygon([(x * k, y * k) for x, y in shape["polygon"]], outline=colour + (255,), width=2,
                         fill=colour + (40,))
        else:
            x, y, w, h = (v * k for v in shape["rect"])
            draw.rectangle((x, y, x + w, y + h), outline=colour + (255,), width=2, fill=colour + (40,))
        ax, ay = (v * k for v in c["anchor"])
        draw.text((ax, ay), c["control"], fill=(255, 255, 255, 255), font=small, anchor="mm",
                  stroke_width=2, stroke_fill=(0, 0, 0, 255))
    return image


def letterbox(image, size=(1100, 600), background=(18, 18, 20)):
    """@p image scaled into @p size, its aspect kept"""
    scale = min(size[0] / image.width, size[1] / image.height)
    small = image.convert("RGB").resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    page = Image.new("RGB", size, background)
    page.paste(small, ((size[0] - small.width) // 2, (size[1] - small.height) // 2))
    return page


def sheet(title, tiles, columns=2, background=(18, 18, 20)):
    width, height = tiles[0][1].size
    pad, caption = 24, 34
    rows = (len(tiles) + columns - 1) // columns
    page = Image.new("RGB", (columns * width + (columns + 1) * pad, 70 + rows * (height + caption + pad)), background)
    draw = ImageDraw.Draw(page)
    draw.text((pad, 20), title, fill=(230, 230, 230), font=font(30))
    for i, (name, image) in enumerate(tiles):
        x = pad + (i % columns) * (width + pad)
        y = 70 + (i // columns) * (height + caption + pad)
        draw.text((x, y + 4), name, fill=(160, 160, 160), font=font(20))
        page.paste(image.resize((width, height)), (x, y + caption))
    return page


def compare(qt, rsvg):
    a, b = qt.convert("RGB"), rsvg.convert("RGB").resize(qt.size)
    diff = ImageChops.difference(a, b)
    hist = diff.convert("L").histogram()
    total = a.width * a.height
    channels = [sum(i * n for i, n in enumerate(diff.getchannel(ch).histogram())) / total for ch in range(3)]
    return {"mean": round(sum(channels) / 3, 3), "max_channel_mean": round(max(channels), 3),
            "off32": round(sum(hist[33:]) / total * 100, 4)}, Image.eval(diff, lambda v: min(255, v * 8))


def main():
    names = sys.argv[1:] or list(generate.SETS)
    SCRATCH.mkdir(parents=True, exist_ok=True)
    check_path = SCRATCH / "check.json"
    results = json.loads(check_path.read_text()) if check_path.exists() else {}
    overview = []
    for name in names:
        folder = generate.OUT / name
        svg = folder / "front.svg"
        hotspots = json.loads((folder / "front.hotspots.json").read_text())
        out = SCRATCH / name
        out.mkdir(exist_ok=True)
        qt = render_qt(svg, 1600)
        qt.save(out / "qt.png")
        rsvg = render_rsvg(svg, 1600, out / "rsvg.png")
        dark_bg = THEMES["dark"]["window"]
        numbers, diff = compare(on(dark_bg, qt), on(dark_bg, rsvg))
        results[name] = numbers
        diff.save(out / "qt-vs-rsvg.png")
        held = all_held(hotspots)
        tiles = [("dialog, dark theme", dialog(svg, hotspots, "dark")),
                 ("dialog, light theme", dialog(svg, hotspots, "light")),
                 ("every control held, dark", dialog(svg, hotspots, "dark", held)),
                 ("every control held, light", dialog(svg, hotspots, "light", held)),
                 ("hotspot map (Qt)", letterbox(hotspot_map(on(dark_bg, qt), hotspots))),
                 (f"Qt vs rsvg x8: mean {numbers['mean']}, {numbers['off32']} % off > 32", letterbox(diff))]
        sheet(f"{name} · {len(hotspots['controls'])} controls", tiles).save(out / "sheet.png")
        singles = []
        for c in hotspots["controls"]:
            kind = c.get("analog", {}).get("kind")
            live = {c["control"]: 1.0} if kind != "stick" else {c["control"]: 0.8, c["analog"]["pair"]: -0.5}
            singles.append((c["control"], dialog(svg, hotspots, "dark", live, width=760, height=420)))
        sheet(f"{name} · one control held at a time", singles, columns=4).save(out / "controls.png")
        overview.append((name, tiles[0][1], tiles[1][1]))
        print(f"{name}: Qt vs rsvg mean {numbers['mean']} (max channel {numbers['max_channel_mean']}), "
              f"{numbers['off32']} % of pixels off by > 32")
    check_path.write_text(json.dumps(results, indent=2) + "\n")
    if len(names) > 1:
        tiles = []
        for name, dark, light in overview:
            tiles += [(f"{name} · dark", dark), (f"{name} · light", light)]
        sheet("POC 033 · controller pictures", tiles, columns=2).save(SCRATCH / "overview.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
