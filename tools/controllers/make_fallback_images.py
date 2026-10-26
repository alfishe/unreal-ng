#!/usr/bin/env python3
"""Write the family fallback images of the controllers dialog (device-database.md §6, §8, §10).

Each image is a line drawing in the 2048 x 1536 design space, written as data/controllers/images/<set>/front.svg,
with front.hotspots.json (schema 1) and provenance.json. The pads and the DB9 sticks are drawn by POC 033
(tools/poc/033-vector-controller-pictures/generate.py); this script keeps the sets that have no vector picture yet
(md6-pad, surface-dial, generic-keyboard). The drawing is ours, so the provenance says so.
Run from anywhere: python3 tools/controllers/make_fallback_images.py [--check]
--check exits 1 when a written file would differ (the files are generated, not edited by hand).
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "data" / "controllers" / "images"
W, H = 2048, 1536
BODY = "#3a3d42"
EDGE = "#8a8f98"
KEY = "#5b6068"


def circle(control, label, cx, cy, r, side, fill=KEY, analog=None, group=None):
    spot = {"control": control, "label": label, "shape": {"circle": [cx, cy, r]}, "anchor": [cx, cy], "callout": side}
    if analog:
        spot["analog"] = analog
    if group:
        spot["group"] = group
    svg = f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" stroke="{EDGE}" stroke-width="6"/>'
    return spot, svg


def rect(control, label, x, y, w, h, side, rx=18, analog=None):
    spot = {"control": control, "label": label, "shape": {"rect": [x, y, w, h]},
            "anchor": [x + w // 2, y + h // 2], "callout": side}
    if analog:
        spot["analog"] = analog
    svg = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{KEY}" stroke="{EDGE}" stroke-width="6"/>'
    return spot, svg


def dpad(cx, cy, size, side, prefix="dpad", label="D-pad"):
    spots, svgs = [], []
    arm = size // 3
    for control, dx, dy in ((f"{prefix}.up", 0, -1), (f"{prefix}.down", 0, 1), (f"{prefix}.left", -1, 0),
                            (f"{prefix}.right", 1, 0)):
        x = cx + dx * arm - arm // 2
        y = cy + dy * arm - arm // 2
        spot, svg = rect(control, label, x, y, arm, arm, side, rx=8)
        spot["group"] = prefix
        spots.append(spot)
        svgs.append(svg)
    svgs.append(f'<rect x="{cx - arm // 2}" y="{cy - arm // 2}" width="{arm}" height="{arm}" fill="{KEY}"/>')
    return spots, svgs


def md6():
    spots, svgs = [], []
    svgs.append(f'<path d="M480 600 Q1024 480 1568 600 Q1760 700 1700 1000 Q1640 1200 1400 1120 L648 1120 '
                f'Q408 1200 348 1000 Q288 700 480 600 Z" fill="{BODY}" stroke="{EDGE}" stroke-width="8"/>')
    s, v = dpad(640, 860, 300, "left", prefix="", label="D-pad")
    for spot in s:
        spot["control"] = spot["control"].lstrip(".")
        spot["group"] = "directions"
    spots += s
    svgs += v
    for control, cx, cy in (("a", 1240, 960), ("b", 1380, 920), ("c", 1520, 880), ("x", 1220, 800), ("y", 1360, 760),
                            ("z", 1500, 720)):
        spot, svg = circle(control, control.upper(), cx, cy, 56, "right")
        spots.append(spot)
        svgs.append(svg)
    for control, label, cx in (("start", "Start", 1024), ("mode", "Mode", 1500)):
        spot, svg = rect(control, label, cx - 70, 640 if control == "start" else 560, 140, 44, "right", rx=22)
        spots.append(spot)
        svgs.append(svg)
    return spots, svgs


def surface_dial():
    """A Surface Dial from above: the body turns (knob, a dot where it has turned to), the top presses (push); the
    hand on it (touch) has no place of its own, so it is a tag under the dial"""
    spots, svgs = [], []
    spot, svg = circle("push", "Press", 1024, 700, 170, "right", fill=BODY)
    spots.append(spot)
    knob, ring = circle("knob", "Turn", 1024, 700, 520, "left", fill=KEY,
                        analog={"kind": "encoder", "steps": 36, "radius": 440})
    spots.append(knob)
    touch, tag = rect("touch", "Touch", 874, 1300, 300, 90, "right", rx=45)
    spots.append(touch)
    svgs += [ring, f'<circle cx="1024" cy="700" r="440" fill="none" stroke="{EDGE}" stroke-width="4"/>', svg, tag]
    return spots, svgs


def steam_deck():
    """The Steam Deck from the front: the screen in the middle; on the left the D-pad, the left stick, the left
    trackpad and the Steam button, mirrored on the right by A B X Y, the right stick, the right trackpad and the quick
    access button. The triggers and shoulders stand above the body; the back paddles (L4 L5 R4 R5) hang below the
    grips as dashed tabs. A trackpad shows the finger as a stick's dot; its pressure is the bar under it, the thumb on
    a stick a tag under the stick"""
    spots, svgs = [], []
    svgs.append(f'<path d="M260 430 L1788 430 Q1960 430 1975 600 L1990 1000 Q1995 1230 1800 1250 Q1640 1260 1560 1160 '
                f'L488 1160 Q408 1260 248 1250 Q53 1230 58 1000 L73 600 Q88 430 260 430 Z" fill="{BODY}" '
                f'stroke="{EDGE}" stroke-width="8"/>')
    svgs.append(f'<rect x="674" y="480" width="700" height="480" rx="34" fill="#1d1f22" stroke="{EDGE}" '
                f'stroke-width="4"/>')
    svgs.append('<rect x="709" y="520" width="630" height="400" rx="6" fill="#232a33"/>')
    svgs.append('<path d="M709 520 L1009 520 L829 920 L709 920 Z" fill="#ffffff" fill-opacity="0.04"/>')
    for side, sign in (("left", 1), ("right", -1)):
        mirror = (lambda x: x) if sign > 0 else (lambda x: W - x)
        for row in range(3):
            for col in range(4):
                cx = mirror(150 + col * 26 + (row % 2) * 13)
                svgs.append(f'<circle cx="{cx}" cy="{1080 + row * 24}" r="6" fill="#2c2f33"/>')
        trigger, shoulder = ("lt", "lb") if sign > 0 else ("rt", "rb")
        tl, sl = ("L2", "L1") if sign > 0 else ("R2", "R1")
        x = 150 if sign > 0 else W - 150 - 260
        for spot, svg in (rect(trigger, tl, x, 290, 260, 90, side, analog={"kind": "trigger", "fill": "down"}),
                          rect(shoulder, sl, x - 20, 392, 300, 44, side, rx=20)):
            spots.append(spot)
            svgs.append(svg)
    s, v = dpad(250, 620, 210, "left")
    spots += s
    svgs += v
    for control, dx, dy in (("north", 0, -1), ("south", 0, 1), ("west", -1, 0), ("east", 1, 0)):
        spot, svg = circle(control, {"north": "Y", "south": "A", "west": "X", "east": "B"}[control],
                           1798 + dx * 78, 620 + dy * 78, 42, "right")
        spots.append(spot)
        svgs.append(svg)
    for name, label, cx, side in (("lstick", "LS", 500, "left"), ("rstick", "RS", W - 500, "right")):
        spot, svg = circle(f"{name}.x", label, cx, 600, 95, side,
                           analog={"kind": "stick", "pair": f"{name}.y", "radius": 60}, group=name)
        click, _ = circle(f"{name}.click", f"{label} press", cx, 600, 52, side, group=name)
        touch, tag = rect(f"{name}.touch", f"{label} touch", cx - 50, 712, 100, 28, side, rx=14)
        touch["quiet"] = True
        touch["group"] = name
        spots += [spot, click, touch]
        svgs += [svg, f'<circle cx="{cx}" cy="600" r="52" fill="{BODY}" stroke="{EDGE}" stroke-width="4"/>', tag]
    for name, label, cx, side in (("ltouchpad", "Left trackpad", 440, "left"),
                                  ("rtouchpad", "Right trackpad", W - 440, "right")):
        x, y, size = cx - 115, 765, 230
        pad_spot, pad_svg = rect(f"{name}.x", label, x, y, size, size, side, rx=40,
                                 analog={"kind": "stick", "pair": f"{name}.y", "radius": 100})
        pad_spot["group"] = name
        click, _ = rect(f"{name}.click", f"{label} press", x + 20, y + 20, size - 40, size - 40, side, rx=30)
        click["group"] = name
        touch, _ = rect(f"{name}.touch", f"{label} touch", x + 20, y + 20, size - 40, size - 40, side, rx=30)
        touch["quiet"] = True
        touch["group"] = name
        pressure, bar = rect(f"{name}.pressure", f"{label} pressure", x + 15, y + size + 10, size - 30, 14, side, rx=7,
                             analog={"kind": "trigger", "fill": "down"})
        pressure["quiet"] = True
        pressure["group"] = name
        spots += [pad_spot, click, touch, pressure]
        svgs += [pad_svg, f'<rect x="{x + 20}" y="{y + 20}" width="{size - 40}" height="{size - 40}" rx="30" '
                          f'fill="none" stroke="{EDGE}" stroke-width="2" stroke-dasharray="6 10"/>', bar]
    for control, label, x, side in (("view", "View", 560, "left"), ("menu", "Menu", W - 560, "right")):
        spot, svg = circle(control, label, x, 478, 24, side)
        spots.append(spot)
        svgs.append(svg)
    for control, label, x, side in (("guide", "Steam", 250, "left"), ("misc.1", "···", W - 250, "right")):
        spot, svg = rect(control, label, x - 55, 1000, 110, 46, side, rx=20)
        spots.append(spot)
        svgs.append(svg)
    svgs.append(f'<text x="{W - 250}" y="1032" font-family="sans-serif" font-size="34" fill="{EDGE}" '
                f'text-anchor="middle">···</text>')
    for control, label, x, y, side in (("paddle.p3", "L4", 150, 1275, "left"), ("paddle.p4", "L5", 150, 1345, "left"),
                                       ("paddle.p1", "R4", W - 150 - 170, 1275, "right"),
                                       ("paddle.p2", "R5", W - 150 - 170, 1345, "right")):
        spot, svg = rect(control, label, x, y, 170, 52, side, rx=24)
        spots.append(spot)
        svgs.append(svg.replace('stroke-width="6"', 'stroke-width="5" stroke-dasharray="14 8"'))
        svgs.append(f'<text x="{x + 85}" y="{y + 36}" font-family="sans-serif" font-size="30" fill="{EDGE}" '
                    f'text-anchor="middle">{label}</text>')
    return spots, svgs


# A 104-key ANSI keyboard: (PcKey automation name, label, width in key units), rows top to bottom; None is a gap
KEYBOARD_MAIN = [
    [("esc", "Esc", 1), None, ("f1", "F1", 1), ("f2", "F2", 1), ("f3", "F3", 1), ("f4", "F4", 1), (None, 0.5),
     ("f5", "F5", 1), ("f6", "F6", 1), ("f7", "F7", 1), ("f8", "F8", 1), (None, 0.5), ("f9", "F9", 1),
     ("f10", "F10", 1), ("f11", "F11", 1), ("f12", "F12", 1)],
    [("backquote", "`", 1), ("1", "1", 1), ("2", "2", 1), ("3", "3", 1), ("4", "4", 1), ("5", "5", 1),
     ("6", "6", 1), ("7", "7", 1), ("8", "8", 1), ("9", "9", 1), ("0", "0", 1), ("minus", "-", 1),
     ("equal", "=", 1), ("backspace", "Backspace", 2)],
    [("tab", "Tab", 1.5), ("q", "Q", 1), ("w", "W", 1), ("e", "E", 1), ("r", "R", 1), ("t", "T", 1),
     ("y", "Y", 1), ("u", "U", 1), ("i", "I", 1), ("o", "O", 1), ("p", "P", 1), ("lbracket", "[", 1),
     ("rbracket", "]", 1), ("backslash", "\\", 1.5)],
    [("capslock", "Caps", 1.75), ("a", "A", 1), ("s", "S", 1), ("d", "D", 1), ("f", "F", 1), ("g", "G", 1),
     ("h", "H", 1), ("j", "J", 1), ("k", "K", 1), ("l", "L", 1), ("semicolon", ";", 1), ("quote", "'", 1),
     ("enter", "Enter", 2.25)],
    [("lshift", "Shift", 2.25), ("z", "Z", 1), ("x", "X", 1), ("c", "C", 1), ("v", "V", 1), ("b", "B", 1),
     ("n", "N", 1), ("m", "M", 1), ("comma", ",", 1), ("period", ".", 1), ("slash", "/", 1),
     ("rshift", "Shift", 2.75)],
    [("lctrl", "Ctrl", 1.25), ("lgui", "Win", 1.25), ("lalt", "Alt", 1.25), ("space", "", 6.25),
     ("ralt", "Alt", 1.25), ("rgui", "Win", 1.25), ("menu", "Menu", 1.25), ("rctrl", "Ctrl", 1.25)],
]
# The navigation block (x from its left, y in rows) and the keypad (x, y, w, h in key units)
KEYBOARD_NAV = [("printscreen", "PrtSc", 0, 0), ("scrolllock", "ScrLk", 1, 0), ("pause", "Pause", 2, 0),
                ("insert", "Ins", 0, 1), ("home", "Home", 1, 1), ("pageup", "PgUp", 2, 1),
                ("delete", "Del", 0, 2), ("end", "End", 1, 2), ("pagedown", "PgDn", 2, 2),
                ("up", "\u2191", 1, 4), ("left", "\u2190", 0, 5), ("down", "\u2193", 1, 5), ("right", "\u2192", 2, 5)]
KEYBOARD_PAD = [("numlock", "Num", 0, 1, 1, 1), ("kp_divide", "/", 1, 1, 1, 1), ("kp_multiply", "*", 2, 1, 1, 1),
                ("kp_minus", "-", 3, 1, 1, 1), ("kp_7", "7", 0, 2, 1, 1), ("kp_8", "8", 1, 2, 1, 1),
                ("kp_9", "9", 2, 2, 1, 1), ("kp_plus", "+", 3, 2, 1, 2), ("kp_4", "4", 0, 3, 1, 1),
                ("kp_5", "5", 1, 3, 1, 1), ("kp_6", "6", 2, 3, 1, 1), ("kp_1", "1", 0, 4, 1, 1),
                ("kp_2", "2", 1, 4, 1, 1), ("kp_3", "3", 2, 4, 1, 1), ("kp_enter", "Enter", 3, 4, 1, 2),
                ("kp_0", "0", 0, 5, 2, 1), ("kp_decimal", ".", 2, 5, 1, 1)]


def keyboard():
    """A generic 104-key PC keyboard from above. Every key is a quiet hotspot `key.<PcKey name>`: it lights while
    held and takes a callout only when something is bound to it (a hundred lines would hide the picture)"""
    unit = 84
    gap = 8
    left = (W - 22.5 * unit) / 2
    top = (H - 6.5 * unit) / 2
    spots, svgs = [], []
    svgs.append(f'<rect x="{left - 30:.0f}" y="{top - 30:.0f}" width="{22.5 * unit + 60:.0f}" '
                f'height="{6.5 * unit + 60:.0f}" rx="28" fill="{BODY}" stroke="{EDGE}" stroke-width="6"/>')

    def key(name, label, x, y, w, h):
        px, py = round(left + x * unit + gap / 2), round(top + y * unit + gap / 2)
        pw, ph = round(w * unit - gap), round(h * unit - gap)
        spot, svg = rect(f"key.{name}", label or "Space", px, py, pw, ph, "left" if px + pw / 2 < W / 2 else "right",
                         rx=10)
        spot["quiet"] = True
        spots.append(spot)
        svgs.append(svg)
        if label:
            size = 30 if len(label) <= 2 else (24 if len(label) <= 5 else 20)
            svgs.append(f'<text x="{px + pw / 2:.0f}" y="{py + ph / 2 + size * 0.35:.0f}" font-family="sans-serif" '
                        f'font-size="{size}" fill="#e6e8eb" text-anchor="middle">{escape(label)}</text>')

    for row, keys in enumerate(KEYBOARD_MAIN):
        x = 0.0
        y = row + (0 if row == 0 else 0.5)  # the function row stands apart
        for entry in keys:
            if entry is None:
                x += 1
                continue
            if entry[0] is None:
                x += entry[1]
                continue
            name, label, w = entry
            key(name, label, x, y, w, 1)
            x += w
    for name, label, x, y in KEYBOARD_NAV:
        key(name, label, 15.25 + x, y + (0 if y == 0 else 0.5), 1, 1)
    for name, label, x, y, w, h in KEYBOARD_PAD:
        key(name, label, 18.5 + x, y + 0.5, w, h)
    return spots, svgs


def escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


SETS = {
    "md6-pad": md6,
    "surface-dial": surface_dial,
    "steam-deck": steam_deck,
    "generic-keyboard": keyboard,
}


def files(name):
    spots, svgs = SETS[name]()
    for i, spot in enumerate(spots):
        spot["order"] = i
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
           f'<!-- generated by tools/controllers/make_fallback_images.py: do not edit -->\n  '
           + "\n  ".join(svgs) + "\n</svg>\n")
    hotspots = {"schema": 1, "family": name, "view": "front", "size": [W, H], "controls": spots}
    provenance = {"family": name, "images": [{
        "view": "front", "source": "tools/controllers/make_fallback_images.py", "author": "unreal-ng",
        "licence": "same as the repository", "terms": "generated line drawing, no photo",
        "retouch": "none"}]}
    return {
        OUT / name / "front.svg": svg,
        OUT / name / "front.hotspots.json": json.dumps(hotspots, indent=2) + "\n",
        OUT / name / "provenance.json": json.dumps(provenance, indent=2) + "\n",
    }


def main():
    check = "--check" in sys.argv
    stale = []
    for name in SETS:
        for path, text in files(name).items():
            if check:
                if not path.exists() or path.read_text() != text:
                    stale.append(str(path.relative_to(ROOT)))
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    if stale:
        print("stale (run tools/controllers/make_fallback_images.py):\n  " + "\n  ".join(stale))
        return 1
    if not check:
        print(f"wrote {len(SETS)} image sets to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
