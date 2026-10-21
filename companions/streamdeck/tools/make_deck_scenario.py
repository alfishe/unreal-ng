#!/usr/bin/env python3
"""Writes a Stream Deck scenario (format v2) from a menu-hierarchy spec and the host's action catalogue.

Design: docs/inprogress/2026-10-19-streamdeck-integration/scenarios.md section 9. The spec says what the deck holds
(menus, keys, dials, each bound to a catalogue action id); the generator turns it into a scenario document: `action`
steps with their value / hold, faces from the icon set, keys placed row by row where the spec gives no slot, the back
key kept free. It stops on an action id the catalogue lacks, an icon the icon set lacks and a $data path that is not
there. The output is deterministic (no time stamps, no machine paths): the same spec and catalogue give the same bytes.

The shipped default scenario waits for the icon set and the use-case document that will be its spec; this tool and
its test fixture (tools/tests/fixture.spec.json) are the machinery.

Spec (JSON):
  {"name", "model", "settings"?, "icons"?: "$data/streamdeck/themes/unrealng/keys",
   "root": MENU}
  MENU   = {"id", "title", "menu": true (the root may omit it), "context"?, "icon"?, "label"?, "bg"?, "strip"?,
            "back"?: [row, col] | false (submenus: default [0, 0]), "dials"?: [DIAL], "items": [MENU | KEY], "at"?}
  KEY    = {"id", "title", "at"?, "icon"?, "label"?, "bg"?, "icon_on"?, "label_on"?, "bg_on"?, "state"?, "then"?,
            "tags"?, STEP | "steps": [STEP]}
  STEP   = {"action": id, "value"?: ..., "hold"?: true} or {"kind": ..., ...} (any other step kind as it is)
  DIAL   = {"index", "label", "turn"?: STEP, "press"?: STEP, "left"? / "right"?: STEP (a dir turn)}
  An icon is a picture of the icon set (icons/<name>.png, the label is in the picture) or else a built-in icon drawn
  with the label.

  make_deck_scenario.py SPEC --out FILE.deck.json       write the scenario
  make_deck_scenario.py SPEC --out FILE --check         exit 1 when FILE differs from what the generator writes
  make_deck_scenario.py --fetch-catalogue http://127.0.0.1:8090
                                                        refresh data/streamdeck/catalogue.json from a running unreal-qt
                                                        (GET /api/v1/controllers/actions; availability dropped)
"""
import argparse
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
DATA = os.path.join(REPO, "data")
CATALOGUE = os.path.join(DATA, "streamdeck", "catalogue.json")
DEFAULT_ICONS = "$data/streamdeck/themes/unrealng/keys"
BUILTIN_ICONS = {"record", "tape", "disk", "play", "pause", "stop", "reset", "speaker", "mute", "rewind", "forward",
                 "stepback", "flag", "bug", "gear", "check", "cross", "warn", "camera", "chip", "stepinto",
                 "stepover", "stepout", "eye", "memory", "regs", "disasm", "stack"}
GRIDS = {"plusxl": (4, 9), "xl": (4, 8), "plus": (2, 4), "classic": (3, 5), "mk2": (3, 5), "mini": (2, 3),
         "neo": (2, 4)}
BG, FG = "1A1A10", "FFD200"  # the UnrealNG theme's dark olive and yellow, for faces drawn from a built-in icon


class SpecError(Exception):
    pass


def data_file(path):
    """$data/x -> the repository's data/x"""
    return os.path.join(DATA, path[len("$data/"):].split("#")[0])


class Generator:
    def __init__(self, catalogue, spec):
        self.ids = {a["id"] for a in catalogue["actions"]}
        self.spec = spec
        self.icons = spec.get("icons", DEFAULT_ICONS).rstrip("/")
        self.rows, self.cols = GRIDS.get(spec.get("model", "plusxl"), (4, 9))
        self.used = set()

    def fail(self, where, what):
        raise SpecError(f"{where}: {what}")

    def step(self, s, where):
        if "action" in s:
            aid = s["action"]
            if aid not in self.ids:
                self.fail(where, f"action '{aid}' is not in the catalogue")
            self.used.add(aid)
            out = {"kind": "action", "id": aid}
            if "value" in s:
                out["value"] = s["value"]
            if s.get("hold"):
                out["hold"] = True
            self.paths(s.get("value"), where)
            return out
        if "kind" not in s:
            self.fail(where, "a step needs \"action\" or \"kind\"")
        if s["kind"] in ("shell", "open", "keys", "webapi"):
            self.fail(where, f"kind '{s['kind']}' is not for a shipped scenario (ACC-5)")
        return dict(s)

    def paths(self, v, where):
        if isinstance(v, str) and v.startswith("$data/") and not os.path.exists(data_file(v)):
            self.fail(where, f"{v}: not in data/")
        elif isinstance(v, dict):
            for x in v.values():
                self.paths(x, where)
        elif isinstance(v, list):
            for x in v:
                self.paths(x, where)

    def face(self, n, suffix=""):
        icon, label = n.get("icon" + suffix), n.get("label" + suffix)
        bg = n.get("bg" + suffix)
        if icon:
            picture = f"{self.icons}/{icon}.png"
            if picture.startswith("$data/") and os.path.exists(data_file(picture)):
                f = {"image": picture}
                if bg:
                    f["bg"] = bg
                return f
            if icon not in BUILTIN_ICONS:
                self.fail(n["id"], f"icon '{icon}' is neither in {self.icons} nor built in")
        f = {"label": label if label is not None else n.get("title", n["id"]).upper()[:10]}
        if icon:
            f["icon"] = icon
        f["bg"], f["fg"] = bg or BG, FG
        return f

    def key(self, n):
        out = {"id": n["id"], "type": "action", "title": n.get("title", n["id"])}
        if n.get("tags"):
            out["tags"] = list(n["tags"])
        if "at" in n:
            out["at"] = list(n["at"])
        out["face"] = self.face(n)
        if "icon_on" in n or "label_on" in n or "bg_on" in n:
            on = dict(n)
            for k in ("icon", "label", "bg"):
                if k + "_on" in n:
                    on[k] = n[k + "_on"]
            out["face_on"] = self.face(on)
        if n.get("state"):
            out["state"] = n["state"]
        if "steps" in n:
            out["do"] = {"kind": "multi", "steps": [self.step(s, n["id"]) for s in n["steps"]]}
        else:
            out["do"] = self.step(n, n["id"])
        if n.get("then"):
            out["then"] = n["then"]
        return out

    def dial(self, d, where):
        out = {"index": d["index"], "label": d.get("label", "")}
        if "turn" in d:
            out["turn"] = self.step(d["turn"], where)
        elif "left" in d or "right" in d:
            out["turn"] = {"kind": "dir"}
            for side in ("left", "right"):
                if side in d:
                    out["turn"][side] = self.step(d[side], where)
        if "press" in d:
            out["press"] = self.step(d["press"], where)
        return out

    def menu(self, n, root=False):
        out = {"id": n["id"], "type": "menu", "title": n.get("title", n["id"])}
        if n.get("context"):
            out["context"] = n["context"]
        if "at" in n:
            out["at"] = list(n["at"])
        out["face"] = self.face(n)
        back = None if root else n.get("back", [0, 0])
        nav = {}
        if back:
            nav["back"] = list(back)
        elif back is False:
            nav["back"] = None
        if n.get("dials"):
            nav["dials"] = "own"
        if nav:
            out["nav"] = nav
        if n.get("strip"):
            out["strip"] = n["strip"]
        if n.get("dials"):
            out["dials"] = [self.dial(d, n["id"]) for d in n["dials"]]
        items = [self.menu(c) if c.get("menu") else self.key(c) for c in n.get("items", [])]
        self.place(n["id"], items, [tuple(back)] if back else [])
        out["items"] = items
        return out

    def place(self, menu_id, items, reserve):
        """Row-major into the free slots; the slot stays right after the title."""
        used = set(reserve) | {tuple(i["at"]) for i in items if "at" in i}
        for i in items:
            if "at" in i and tuple(i["at"]) in reserve:
                self.fail(i["id"], f"slot {i['at']} is the back key of {menu_id}")
        free = [(r, c) for r in range(self.rows) for c in range(self.cols) if (r, c) not in used]
        for i in items:
            if "at" not in i:
                if not free:
                    self.fail(menu_id, f"more keys than the {self.rows}x{self.cols} grid holds")
                at = list(free.pop(0))
                keys = list(i.items())
                i.clear()
                for k, v in keys:
                    i[k] = v
                    if k == "title":
                        i["at"] = at

    def scenario(self):
        doc = {"format": "streamdeck-scenario", "version": 2, "name": self.spec.get("name", ""),
               "model": self.spec.get("model", "plusxl")}
        if self.spec.get("settings"):
            doc["settings"] = self.spec["settings"]
        doc["root"] = self.menu(self.spec["root"], root=True)
        ids = [n["id"] for n in walk(doc["root"])]
        dup = sorted({i for i in ids if ids.count(i) > 1})
        if dup:
            raise SpecError("duplicate ids: " + ", ".join(dup))
        return doc


def walk(n):
    yield n
    for c in n.get("items", []):
        yield from walk(c)


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def shipped_catalogue(reply):
    """GET /api/v1/controllers/actions -> the shipped form (no availability of the moment, no instance)."""
    rows = []
    for a in reply["actions"]:
        row = {"id": a["id"], "label": a.get("label", a["id"]), "group": a.get("group", ""),
               "kind": a.get("kind", "press"), "held": bool(a.get("held", False)), "gui": bool(a.get("gui", False))}
        if a.get("params"):
            row["params"] = a["params"]
        rows.append(row)
    return {"format": "unreal-ng-action-catalogue", "version": 1, "actions": rows}


def dumps(doc):
    return json.dumps(doc, indent=1, ensure_ascii=False) + "\n"


def generate(catalogue, spec):
    return Generator(catalogue, spec).scenario()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("spec", nargs="?", help="the menu-hierarchy spec (JSON)")
    p.add_argument("--out", help="the scenario to write (.deck.json)")
    p.add_argument("--catalogue", default=CATALOGUE, help="the action catalogue (default data/streamdeck/catalogue.json)")
    p.add_argument("--fetch-catalogue", metavar="WEBAPI", help="refresh the catalogue file from a running unreal-qt")
    p.add_argument("--check", action="store_true", help="compare with --out instead of writing it")
    args = p.parse_args(argv)
    if args.fetch_catalogue:
        with urllib.request.urlopen(args.fetch_catalogue.rstrip("/") + "/api/v1/controllers/actions", timeout=5) as r:
            reply = json.load(r)
        with open(args.catalogue, "w", encoding="utf-8", newline="\n") as f:
            f.write(dumps(shipped_catalogue(reply)))
        print(f"wrote {args.catalogue}: {len(reply['actions'])} actions")
    if not args.spec:
        return 0 if args.fetch_catalogue else p.error("a spec is needed")
    if not args.out:
        p.error("--out is needed")
    try:
        text = dumps(generate(load_json(args.catalogue), load_json(args.spec)))
    except SpecError as e:
        print(f"{args.spec}: {e}", file=sys.stderr)
        return 2
    if args.check:
        try:
            with open(args.out, encoding="utf-8") as f:
                same = f.read() == text
        except OSError:
            same = False
        if not same:
            print(f"{args.out} differs from the generator's output for {args.spec}", file=sys.stderr)
            return 1
        print(f"{args.out}: up to date")
        return 0
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
