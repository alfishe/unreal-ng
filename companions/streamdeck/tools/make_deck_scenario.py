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
  STEP   = {"action": id, "value"?: ..., "hold"?: true}, {"gui": method, "params"?: {...}, "target"?: ...} (a
            GUI control plane command, checked against data/streamdeck/gui-catalogue.json) or {"kind": ..., ...} (any
            other step kind as it is)
  INCLUDE = {"id", "include": FILE, "node"?, "at"?, "title"?, "tags"?, "face"?}   (a link to a subtree in FILE)
  Also (deckflow.py, the scenario pipeline): "face" / "face_on" given as objects are used as they are; "tags" on
  menus; "navigation" (more nav fields of a menu); "extra" (any other fields of the node, as they are); a spec with
  "shared": true is a menu of its own to be included: its root keeps a back key.
  DIAL   = {"index", "label", "turn"?: STEP, "press"?: STEP, "left"? / "right"?: STEP (a dir turn)}
  An icon is a picture of the icon set (icons/<name>.png, the label is in the picture) or else a built-in icon drawn
  with the label.

  make_deck_scenario.py SPEC --out FILE.deck.json       write the scenario
  make_deck_scenario.py SPEC --out FILE --check         exit 1 when FILE differs from what the generator writes
  make_deck_scenario.py --fetch-catalogue http://127.0.0.1:8090
                                                        refresh data/streamdeck/catalogue.json from a running unreal-qt
                                                        (GET /api/v1/controllers/actions; availability dropped). The
                                                        build writes it from the registry too (action-catalogue)
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


GUI_CATALOGUE = os.path.join(DATA, "streamdeck", "gui-catalogue.json")


def gui_commands(gui_catalogue):
    """{method: command} of a GUI catalogue (gui-catalogue.json); each command carries its interface's level."""
    out = {}
    for iface in (gui_catalogue or {}).get("interfaces", []):
        for c in iface.get("commands", []):
            out[c["method"]] = dict(c, level=iface.get("level", "view"), interface=iface["name"],
                                    state=list(iface.get("state", [])))
    return out


def gui_root_fields(commands):
    """The fields a GUI program's root state may hold: the host interfaces' names and state fields."""
    out = set()
    for c in commands.values():
        if c.get("level") == "host":
            out |= {c["interface"], *c.get("state", [])}
    return out


def gui_params_error(command, params):
    """None when params fit the command's declaration (unknown and missing ones are refused), else why not."""
    declared = {p["name"]: p for p in command.get("params", [])}
    takes = ", ".join(p["name"] + ("?" if p.get("optional") else "") for p in command.get("params", [])) or "nothing"
    unknown = [k for k in params if k not in declared]
    missing = [n for n, p in declared.items() if not p.get("optional") and n not in params]
    if not unknown and not missing:
        return None
    parts = [f"unknown parameter {', '.join(unknown)}"] if unknown else []
    parts += [f"missing {', '.join(missing)}"] if missing else []
    return f"{command['method']}: {'; '.join(parts)}; it takes {takes}"


class Generator:
    def __init__(self, catalogue, spec, gui=None):
        self.ids = {a["id"] for a in catalogue["actions"]}
        if gui is None and os.path.exists(GUI_CATALOGUE):
            gui = gui_commands(load_json(GUI_CATALOGUE))
        self.gui = gui or {}
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
            if isinstance(s.get("params"), dict):
                out["params"] = s["params"]
            self.paths(s.get("value"), where)
            return out
        if "gui" in s:
            # A command of the GUI control plane: the daemon sends it to the program that serves its WebAPI
            method = s["gui"]
            command = self.gui.get(method)
            if command is None:
                self.fail(where, f"gui '{method}' is not in the GUI catalogue (data/streamdeck/gui-catalogue.json)")
            params = s.get("params") if isinstance(s.get("params"), dict) else {}
            why = gui_params_error(command, params)
            if why:
                self.fail(where, why)
            out = {"kind": "gui", "method": method}
            if params:
                out["params"] = params
            if s.get("target"):
                out["target"] = s["target"]
            if s.get("hold"):
                out["hold"] = True  # repeated while the key is held (a command that repeats: cursor, page)
            return out
        if "kind" not in s:
            self.fail(where, "a step needs \"action\", \"gui\" or \"kind\"")
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
        if isinstance(n.get("face" + suffix), dict):
            return dict(n["face" + suffix])
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
        if isinstance(n.get("face_on"), dict):
            out["face_on"] = dict(n["face_on"])
        elif "icon_on" in n or "label_on" in n or "bg_on" in n:
            on = dict(n)
            for k in ("icon", "label", "bg"):
                if k + "_on" in n:
                    on[k] = n[k + "_on"]
            out["face_on"] = self.face(on)
        if n.get("state"):
            out["state"] = n["state"]
        if isinstance(n.get("do"), dict):
            out["do"] = self.step(n["do"], n["id"])
        elif "steps" in n:
            out["do"] = {"kind": "multi", "steps": [self.step(s, n["id"]) for s in n["steps"]]}
        elif "action" in n or "gui" in n or "kind" in n or "value" not in out["face"]:
            out["do"] = self.step(n, n["id"])
        # else: a display-only key, its face shows a value (face.value) and a press does nothing
        if n.get("then"):
            out["then"] = n["then"]
        out.update(n.get("extra", {}))
        return out

    def include(self, n):
        out = {"id": n["id"], "type": "include", "file": n["include"]}
        if n.get("node"):
            out["node"] = n["node"]
        if "title" in n:
            out["title"] = n["title"]
        if n.get("tags"):
            out["tags"] = list(n["tags"])
        if "at" in n:
            out["at"] = list(n["at"])
        if n.get("face"):
            out["face"] = dict(n["face"])
        out.update(n.get("extra", {}))
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
        if n.get("tags"):
            out["tags"] = list(n["tags"])
        if n.get("context"):
            out["context"] = n["context"]
        if "at" in n:
            out["at"] = list(n["at"])
        out["face"] = self.face(n)
        back = None if root and not self.spec.get("shared") else n.get("back", [0, 0])
        nav = {}
        if back:
            nav["back"] = list(back)
        elif back is False:
            nav["back"] = None
        if n.get("dials"):
            nav["dials"] = "own"
        nav.update(n.get("navigation", {}))
        if nav:
            out["nav"] = nav
        if n.get("strip"):
            out["strip"] = n["strip"]
        if n.get("dials"):
            out["dials"] = [self.dial(d, n["id"]) for d in n["dials"]]
        items = [self.menu(c) if c.get("menu") else self.include(c) if c.get("include") else self.key(c)
                 for c in n.get("items", [])]
        self.place(n["id"], items, [tuple(back)] if back else [])
        out["items"] = items
        out.update(n.get("extra", {}))
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
               "kind": a.get("kind", "press"), "held": bool(a.get("held", False))}
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
