#!/usr/bin/env python3
"""Write the family fallback images of the controllers dialog (device-database.md §6, §8, §10).

Each image is a line drawing in the 2048 x 1536 design space, written as data/controllers/images/<set>/front.svg,
with front.hotspots.json (schema 1) and provenance.json. They stand in until a retouched photo exists; the drawing is
ours, so the provenance says so. Run from anywhere: python3 tools/controllers/make_fallback_images.py [--check]
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


def stick(name, label, cx, cy, side):
    spot, svg = circle(f"{name}.x", label, cx, cy, 110, side,
                       analog={"kind": "stick", "pair": f"{name}.y", "radius": 70}, group=name)
    click, _ = circle(f"{name}.click", label, cx, cy, 60, side, group=name)
    return [spot, click], [svg, f'<circle cx="{cx}" cy="{cy}" r="60" fill="{BODY}" stroke="{EDGE}" stroke-width="4"/>']


def pad(face_labels, paddles=False, symmetric=False):
    """A pad outline: the left stick high (Xbox, Nintendo) or both low (PlayStation, symmetric=True)"""
    spots, svgs = [], []
    svgs.append(f'<path d="M560 520 Q1024 380 1488 520 Q1700 600 1720 900 Q1740 1220 1560 1260 Q1420 1280 1300 1060 '
                f'L748 1060 Q628 1280 488 1260 Q308 1220 328 900 Q348 600 560 520 Z" fill="{BODY}" '
                f'stroke="{EDGE}" stroke-width="8"/>')
    for spot, svg in (rect("lt", "LT", 520, 330, 220, 110, "left", analog={"kind": "trigger", "fill": "down"}),
                      rect("rt", "RT", 1308, 330, 220, 110, "right", analog={"kind": "trigger", "fill": "down"}),
                      rect("lb", "LB", 500, 460, 260, 60, "left"),
                      rect("rb", "RB", 1288, 460, 260, 60, "right")):
        spots.append(spot)
        svgs.append(svg)
    left_stick = (820, 920) if symmetric else (600, 720)
    dpad_at = (600, 720) if symmetric else (820, 920)
    s, v = stick("lstick", "LS", *left_stick, "left")
    spots += s
    svgs += v
    s, v = stick("rstick", "RS", 1228, 920, "right")
    spots += s
    svgs += v
    s, v = dpad(*dpad_at, 240, "left")
    spots += s
    svgs += v
    for control, dx, dy in (("north", 0, -1), ("south", 0, 1), ("west", -1, 0), ("east", 1, 0)):
        spot, svg = circle(control, face_labels[control], 1448 + dx * 105, 720 + dy * 105, 58, "right")
        spots.append(spot)
        svgs.append(svg)
    for control, label, cx in (("view", face_labels["view"], 900), ("guide", face_labels["guide"], 1024),
                               ("menu", face_labels["menu"], 1148)):
        spot, svg = circle(control, label, cx, 640, 34 if control != "guide" else 52, "left" if cx < 1024 else "right")
        if control == "guide":
            spot["callout"] = "right"
        spots.append(spot)
        svgs.append(svg)
    if paddles:
        for control, label, cx, cy, side in (("paddle.p3", "P3", 760, 1180, "left"), ("paddle.p4", "P4", 800, 1260, "left"),
                                             ("paddle.p1", "P1", 1288, 1180, "right"),
                                             ("paddle.p2", "P2", 1248, 1260, "right")):
            spot, svg = rect(control, label, cx - 60, cy - 30, 120, 60, side, rx=28)
            spots.append(spot)
            svgs.append(svg)
    return spots, svgs


def stick_joystick(prefix, fire_label, fire2=None):
    """A DB9 stick: base, shaft, knob; directions as four arrows around the shaft"""
    spots, svgs = [], []
    svgs.append(f'<rect x="624" y="760" width="800" height="520" rx="60" fill="{BODY}" stroke="{EDGE}" stroke-width="8"/>')
    svgs.append(f'<rect x="1000" y="420" width="48" height="420" fill="{EDGE}"/>')
    svgs.append(f'<circle cx="1024" cy="400" r="120" fill="#b0343a" stroke="{EDGE}" stroke-width="6"/>')
    for control, label, x, y, side in ((f"{prefix}up", "Up", 974, 180, "left"), (f"{prefix}down", "Down", 974, 860, "left"),
                                       (f"{prefix}left", "Left", 760, 520, "left"),
                                       (f"{prefix}right", "Right", 1188, 520, "right")):
        spot, svg = rect(control, label, x, y, 100, 100, side, rx=50)
        spot["group"] = "directions"
        spots.append(spot)
        svgs.append(svg.replace(f'fill="{KEY}"', 'fill="none"'))
    spot, svg = circle(f"{prefix}fire", fire_label, 760, 1000, 80, "left", fill="#b0343a")
    spots.append(spot)
    svgs.append(svg)
    if fire2:
        spot, svg = circle(f"{prefix}fire2", fire2, 1288, 1000, 80, "right", fill="#b0343a")
        spots.append(spot)
        svgs.append(svg)
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


XBOX = {"south": "A", "east": "B", "west": "X", "north": "Y", "view": "View", "menu": "Menu", "guide": "Guide"}
PLAYSTATION = {"south": "Cross", "east": "Circle", "west": "Square", "north": "Triangle", "view": "Create",
               "menu": "Options", "guide": "PS"}
NINTENDO = {"south": "B", "east": "A", "west": "Y", "north": "X", "view": "-", "menu": "+", "guide": "Home"}
UNKNOWN = {"south": "1", "east": "2", "west": "3", "north": "4", "view": "9", "menu": "10", "guide": "13"}

SETS = {
    "generic-xbox-like": lambda: pad(XBOX, paddles=True),
    "generic-playstation-like": lambda: pad(PLAYSTATION, symmetric=True),
    "generic-nintendo-like": lambda: pad(NINTENDO),
    "generic-unknown": lambda: pad(UNKNOWN),
    "generic-db9-stick": lambda: stick_joystick("dpad.", "Fire"),
    "kempston-joystick": lambda: stick_joystick("", "Fire", "Fire 2"),
    "sinclair-joystick": lambda: stick_joystick("", "Fire"),
    "cursor-joystick": lambda: stick_joystick("", "Fire"),
    "md6-pad": md6,
    "surface-dial": surface_dial,
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
