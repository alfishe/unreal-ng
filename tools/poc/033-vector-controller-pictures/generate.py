#!/usr/bin/env python3
"""POC 033: write the vector controller pictures into data/controllers/images/<set>/ (device-database.md §6-§8).

    generate.py            write every set: front.svg, front.hotspots.json (schema 1), provenance.json
    generate.py --check    exit 1 when a written file would differ, or a set breaks a rule below
    generate.py --only X   write only the set X (while drawing)

Rules checked: every hotspot inside the canvas; the control names of a set are unique; no element id twice; the
control names of the set this one replaces are all still there (the dialog and the profiles name them).
"""

import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from devices import db9, dualshock, eightbitdo, generic, playstation, switchpro, xbox  # noqa: E402
from model import H, W  # noqa: E402

ROOT = HERE.parents[2]
OUT = ROOT / "data" / "controllers" / "images"
GENERATOR = "tools/poc/033-vector-controller-pictures/generate.py"

SETS = {
    "xbox-series": xbox.series,
    "xbox-one": xbox.one,
    "xbox-elite-2": xbox.elite2,
    "dualsense": lambda: playstation.dualsense(),
    "dualshock-4": lambda: dualshock.dualshock4(),
    "8bitdo-ultimate": lambda: eightbitdo.ultimate(),
    "8bitdo-pro-2": lambda: eightbitdo.pro2(),
    "8bitdo-sn30-pro-plus": lambda: eightbitdo.sn30proplus(),
    "8bitdo-sn30-pro": lambda: eightbitdo.sn30pro("sn"),
    "8bitdo-sf30-pro": lambda: eightbitdo.sn30pro("sf"),
    "8bitdo-sn30": lambda: eightbitdo.sn30("sn"),
    "8bitdo-sf30": lambda: eightbitdo.sn30("sf"),
    "8bitdo-m30": lambda: eightbitdo.m30(),
    "8bitdo-zero-2": lambda: eightbitdo.zero2(),
    "switch-pro": lambda: switchpro.pro(),
    "generic-xbox-like": lambda: generic.pad("generic-xbox-like", "xbox"),
    "generic-playstation-like": lambda: dualshock.generic_like(),
    "generic-nintendo-like": lambda: generic.pad("generic-nintendo-like", "nintendo"),
    "generic-unknown": lambda: generic.pad("generic-unknown", "unknown"),
    "generic-db9-stick": lambda: db9.stick("generic-db9-stick", "dpad.", ["Fire"]),
    "kempston-joystick": lambda: db9.stick("kempston-joystick", "", ["Fire", "Fire 2"]),
    "sinclair-joystick": lambda: db9.stick("sinclair-joystick", "", ["Fire"]),
    "cursor-joystick": lambda: db9.stick("cursor-joystick", "", ["Fire"]),
}


def _more_sets():
    """The sets of the device modules added after the first round: a module under devices/ that defines SETS (set
    name -> function returning a Picture) registers them; the modules load in name order"""
    import importlib
    for path in sorted((HERE / "devices").glob("*.py")):
        if path.stem.startswith("_"):
            continue
        module = importlib.import_module(f"devices.{path.stem}")
        for name, make in getattr(module, "SETS", {}).items():
            if name in SETS:
                raise SystemExit(f"{name}: drawn by two modules")
            SETS[name] = make


_more_sets()


def committed(path):
    """The file as the last commit has it (the set this generator replaces), or None"""
    rel = path.relative_to(ROOT)
    result = subprocess.run(["git", "-C", str(ROOT), "show", f"HEAD:{rel}"], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def files(name):
    pic = SETS[name]()
    for i, spot in enumerate(pic.spots):
        spot["order"] = i
    hotspots = {"schema": 1, "family": name, "view": "front", "size": [W, H], "controls": pic.spots}
    provenance = {"family": name, "images": [{
        "view": "front", "title": pic.title, "source": GENERATOR, "author": "unreal-ng",
        "licence": "same as the repository",
        "terms": "vector drawing from measured dimensions; photos used as references only, nothing traced",
        "retouch": "none; platform logos left out (D5)", "references": pic.sources}]}
    return pic, {
        OUT / name / "front.svg": pic.svg(GENERATOR),
        OUT / name / "front.hotspots.json": json.dumps(hotspots, indent=2) + "\n",
        OUT / name / "provenance.json": json.dumps(provenance, indent=2, ensure_ascii=False) + "\n",
    }


def problems(name, pic, written):
    found = []
    names = [s["control"] for s in pic.spots]
    if len(names) != len(set(names)):
        found.append(f"{name}: a control twice: {sorted(n for n in set(names) if names.count(n) > 1)}")
    for spot in pic.spots:
        shape = spot["shape"]
        if "circle" in shape:
            cx, cy, r = shape["circle"]
            box = (cx - r, cy - r, cx + r, cy + r)
        else:
            x, y, w, h = shape["rect"]
            box = (x, y, x + w, y + h)
        if box[0] < 0 or box[1] < 0 or box[2] > W or box[3] > H:
            found.append(f"{name}: {spot['control']} outside the canvas")
    ids = re.findall(r'\bid="([^"]+)"', written[OUT / name / "front.svg"])
    if len(ids) != len(set(ids)):
        found.append(f"{name}: an element id twice: {sorted(i for i in set(ids) if ids.count(i) > 1)}")
    old = committed(OUT / name / "front.hotspots.json")
    if old:
        lost = {c["control"] for c in json.loads(old)["controls"]} - set(names)
        if lost:
            found.append(f"{name}: controls of the previous set missing: {sorted(lost)}")
    return found


def main():
    check = "--check" in sys.argv
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    stale, broken = [], []
    for name in SETS:
        if only and name != only:
            continue
        pic, written = files(name)
        broken += problems(name, pic, written)
        for path, text in written.items():
            if check:
                if not path.exists() or path.read_text() != text:
                    stale.append(str(path.relative_to(ROOT)))
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    for line in broken:
        print(line)
    if stale:
        print(f"stale (run {GENERATOR}):\n  " + "\n  ".join(stale))
    if not check and not broken:
        print(f"wrote {1 if only else len(SETS)} image sets to {OUT.relative_to(ROOT)}")
    return 1 if stale or broken else 0


if __name__ == "__main__":
    sys.exit(main())
