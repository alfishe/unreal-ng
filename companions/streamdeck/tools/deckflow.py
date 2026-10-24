#!/usr/bin/env python3
"""deckflow: the Stream Deck scenario pipeline. A compact spec and a Stream Deck Studio project are equal sources.

Guide: docs/companions/streamdeck/pipeline.md. Files (paths from the repository root):

  data/streamdeck/scenarios/src/<name>.spec.yaml          the spec of a scenario (hand-edited)
  data/streamdeck/scenarios/src/shared/<menu>.spec.yaml   a shared menu (several scenarios open it: a Studio include)
  data/streamdeck/scenarios/<name>.deck.yaml              the Studio project (scenario v2; open it in the studio)
  data/streamdeck/scenarios/shared/<menu>.deck.yaml       the project of a shared menu (the included file)
  data/streamdeck/scenarios/.sync/<name>.base.json        what both sides held at the last sync (3-way base)
  docs/inprogress/2026-10-19-streamdeck-integration/scenarios/<name>.md   the meta description (generated)

Commands:
  deckflow.py check NAME                  the spec (and the project) against the catalogue, glyphs, use cases,
                                          readouts, the grid; then `unreal-streamdeck scenario check`
  deckflow.py build NAME [--force]        spec -> project, meta description (refuses when the project holds
                                          edits that are not synced yet, unless --force: the spec wins)
  deckflow.py sync NAME [--prefer spec|project]   3-way: spec-only changes go to the project, project-only changes
                                          go back into the spec, a change of the same field on both sides is a
                                          conflict (nothing written; --prefer resolves all of them one way)
  deckflow.py run NAME [--deck SERIAL] [--api PORT] [--host URL] [--offscreen]
                                          sync, then play the project on a deck: by default an own deck of a Stream
                                          Deck Studio this tool starts with its own daemon (own ports); a real deck
                                          only with --deck
  deckflow.py watch NAME [run options]    run, then sync again on every change of a spec or a project; the daemon
                                          reloads the deck (menu stack kept)
  deckflow.py shots NAME [--pack P | --all-packs] [--open]
                                          every menu of the deck through the studio's automation: a contact sheet
                                          PNG in scratch/deckflow/<name>-shots.png (--pack: the deck's pack set to
                                          P for the shots; --all-packs: a row per pack, <name>-packs.png)
  deckflow.py stop                        end the daemon and studio this tool started
  deckflow.py list                        the specs and their state

Keys name glyphs (`face: {glyph: run.pause}`); the daemon draws them from the icon pack chosen at run time: the
deck's pack setting, else the scenario's settings.pack (the spec's `pack`), else magicwb.

NAME is a scenario name (src/<name>.spec.yaml) or a path to a spec. Exit codes: 0 ok, 1 issues / conflicts,
2 bad usage.
"""
import argparse
import copy
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deckyaml  # noqa: E402
import make_deck_scenario as gen  # noqa: E402

REPO = gen.REPO
KEY_PX = {"plusxl": 112, "xl": 96, "xl2": 96, "module32": 96, "plus": 120, "classic": 72, "mk2": 72,
          "original2": 72, "scissor": 72, "module15": 72, "original": 72, "mini": 80, "mini2": 80, "neo": 96}
GRIDS = dict(gen.GRIDS, xl2=(4, 8), module32=(4, 8), original=(3, 5), original2=(3, 5), scissor=(3, 5),
             module15=(3, 5), mini2=(2, 3))
DEFAULT_PACK = "magicwb"
ADD_GLYPH = ("add the glyph to tools/icons/streamdeck_glyphs.py, draw it in tools/icons/streamdeck_packs/magicwb.py, "
             "run tools/icons/regenerate_streamdeck_icons.py")
PACK_SOURCES = {"svg": ".svg", "png-pixel": ".png", "png-smooth": ".png"}
MENU_GLYPH = "nav.folder"
NAV_GLYPHS = {"back": "nav.back", "home": "nav.home"}

# Fields of a key entry, of a menu section, of a dial; the order fresh entries are written in
KEY_FIELDS = ["at", "uc", "open", "action", "value", "hold", "params", "steps", "nav", "do", "then", "title", "label",
              "glyph", "glyph_on", "state", "show", "face", "face_on", "tags", "activity", "extra"]
INCLUDE_FIELDS = ["at", "open", "uc", "title", "label", "glyph", "face", "tags", "extra"]
MENU_FIELDS = ["title", "uc", "glyph", "label", "context", "readonly", "strip", "back", "navigation", "face", "tags",
               "extra", "dials", "keys"]
DIAL_FIELDS = ["label", "uc", "turn", "press", "left", "right"]
TOP_FIELDS = ["scenario", "shared", "model", "pack", "settings"]
DERIVED = ("title", "glyph", "glyph_on", "state", "hold")
SHOW_FIELDS = ("readout", "format", "warn", "alert")  # a value key's `show` (the project's face.value)


class Paths:
    """Where the pipeline reads and writes (a test points it at a temporary folder)."""

    def __init__(self, root=REPO, out=None, docs=None, scratch=None):
        self.repo = root
        self.data = os.path.join(root, "data")
        self.scen = out or os.path.join(self.data, "streamdeck", "scenarios")
        self.src = os.path.join(self.scen, "src")
        self.sync = os.path.join(self.scen, ".sync")
        self.docs = docs or os.path.join(root, "docs", "inprogress", "2026-10-19-streamdeck-integration", "scenarios")
        self.catalogue = os.path.join(self.data, "streamdeck", "catalogue.json")
        self.glyphs = os.path.join(self.data, "streamdeck", "icons", "glyphs.json")
        self.packs = os.path.join(self.data, "streamdeck", "icons", "packs")
        self.use_cases = os.path.join(root, "docs", "inprogress", "2026-10-19-streamdeck-integration", "use-cases.md")
        self.readouts = os.path.join(root, "core", "src", "emulator", "actions", "readouts.h")
        self.scratch = scratch or os.path.join(root, "scratch", "deckflow")

    def spec(self, name, shared=False):
        return os.path.join(self.src, "shared", name + ".spec.yaml") if shared else os.path.join(self.src,
                                                                                                 name + ".spec.yaml")

    def project(self, name, shared=False):
        return os.path.join(self.scen, "shared", name + ".deck.yaml") if shared else os.path.join(self.scen,
                                                                                                  name + ".deck.yaml")

    def base(self, name, shared=False):
        return os.path.join(self.sync, "shared", name + ".base.json") if shared else os.path.join(self.sync,
                                                                                                  name + ".base.json")

    def rel(self, p):
        return show(p)

    def top_rel(self, p):
        """A path as the repository names it (the meta description's text: no machine paths)."""
        top = os.path.dirname(os.path.dirname(os.path.dirname(self.scen)))
        return os.path.relpath(p, top).replace(os.sep, "/")


class Issue:
    def __init__(self, where, message, error=True):
        self.where, self.message, self.error = where, message, error

    def __str__(self):
        return f"{self.where}: {'error' if self.error else 'warning'}: {self.message}"


class Failed(Exception):
    """Stops a command: the issues (errors, maybe warnings) say why."""

    def __init__(self, issues):
        super().__init__("\n".join(str(i) for i in issues))
        self.issues = issues


# ---------------------------------------------------------------- vocabularies

class Vocab:
    """The catalogue, the glyphs, the use cases and the readouts."""

    def __init__(self, paths):
        self.paths = paths
        self.catalogue = gen.load_json(paths.catalogue)
        self.actions = {a["id"]: a for a in self.catalogue["actions"]}
        g = gen.load_json(paths.glyphs)
        self.glyph_list = g["glyphs"]
        self.glyphs = {x["id"]: x for x in g["glyphs"]}
        self.glyph_actions = g.get("actions", {})
        self.use_cases = load_use_cases(paths.use_cases)
        self.readouts, self.readout_prefixes = load_readouts(paths.readouts)
        self.toggle_stems = load_toggle_stems(os.path.dirname(paths.readouts))
        self._params = {}
        self._packs = None

    def packs(self):
        """{name: pack.json} of every pack in data/streamdeck/icons/packs."""
        if self._packs is None:
            self._packs = {}
            d = self.paths.packs
            for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
                f = os.path.join(d, name, "pack.json")
                if os.path.exists(f):
                    self._packs[name] = gen.load_json(f)
        return self._packs

    def missing_from_packs(self, g):
        """The packs that do not draw glyph g in their own src/ (a fallback does not count: every pack draws every
        glyph of a scenario)."""
        out = []
        for name, meta in self.packs().items():
            ext = PACK_SOURCES.get(meta.get("source"), ".png")
            if not os.path.exists(os.path.join(self.paths.packs, name, "src", g + ext)):
                out.append(name)
        return out

    def params(self, aid):
        if aid not in self._params:
            self._params[aid] = parse_params(self.actions.get(aid, {}).get("params", ""))
        return self._params[aid]

    def default_hold(self, aid):
        a = self.actions.get(aid)
        return bool(a and a.get("held") and "hold?" not in a.get("params", ""))

    def glyph_for(self, aid, value):
        """(face, face_on, state) of an action as glyphs.json binds it."""
        a = self.glyph_actions.get(aid)
        if not a:
            return None, None, None
        face = a.get("face")
        pm = a.get("params") or {}
        vals = list(value.values()) if isinstance(value, dict) else [value]
        for v in vals:
            if isinstance(v, (str, int)) and not isinstance(v, bool) and str(v) in pm:
                face = pm[str(v)]
                break
        state = a.get("state")
        if state and "<" in state:
            name = value.get("name") if isinstance(value, dict) else None
            state = re.sub(r"<[^>]+>", str(name), state) if name else None
        return face, (a.get("face_on") if state else None), state

    def readout_ok(self, path):
        if not re.match(r"^[a-z][A-Za-z0-9_.-]*$", path) or ".." in path or path.endswith(".") or "." not in path:
            return False
        if path.startswith("toggle."):
            rest = path[len("toggle."):]
            return rest.startswith("feature.") or any(rest == a or rest.startswith(a + ".")
                                                      for a in list(self.actions) + sorted(self.toggle_stems))
        if any(path.startswith(p) and len(path) > len(p) for p in self.readout_prefixes):
            return True
        return any(path == r or path.startswith(r + ".") for r in self.readouts)


def load_use_cases(path):
    """{id: title} of docs/.../use-cases.md: the sections (C1) and their items (C1.1, C7.2a)."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^###\s+([CUD]\d+)\.\s+(.*)$", line) or re.match(r"^\s*-\s+([CUD]\d+\.\d+[a-z]?)\s+(.*)$",
                                                                          line)
            if m:
                out[m.group(1)] = m.group(2).strip()
    return out


def load_readouts(path):
    """The readout names of readouts.h's table (first column), and the prefixes (audio.source.<key>, toggle.<id>)."""
    names, prefixes = set(), {"toggle."}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("/// | `"):
                    cell = line.split("|")[1]
                    for t in re.findall(r"`([^`]+)`", cell):
                        if "<" in t:
                            prefixes.add(t.split("<")[0])
                        else:
                            names.add(t)
    names |= {"run.state.paused", "audio.mute.muted", "debug.mode.enabled", "media.tape.playing"}
    return names, prefixes


def load_toggle_stems(actions_dir):
    """The toggle readouts named other than their action (`rules.toggleStem = "profiler"`: toggle.profiler.<name> of
    prof.toggle), from the core's action sources."""
    stems = set()
    if os.path.isdir(actions_dir):
        for f in sorted(os.listdir(actions_dir)):
            if f.endswith(".cpp"):
                with open(os.path.join(actions_dir, f), encoding="utf-8", errors="replace") as fh:
                    stems |= set(re.findall(r'toggleStem\s*=\s*"([^"]+)"', fh.read()))
    return stems


_STOP = {"or", "with", "a", "an", "the", "else", "and", "to", "of", "in", "is", "one", "as", "by", "per", "all",
         "only", "its", "from", "for", "true", "false", "delta", "steps", "tenths", "list", "next", "prev"}


def parse_params(text):
    """Names, required names and enumerations of a catalogue params text ("op (save|load), slot? (1..9, 1), dir?")."""
    names, required, enums = set(), set(), {}
    depth = 0
    pieces, cur = [], ""
    for c in text:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if depth == 0 and c in ",;":
            pieces.append(cur)
            cur = ""
        else:
            cur += c
    pieces.append(cur)
    for piece in pieces:
        p = piece.strip()
        flat, d = "", 0
        for c in p:
            if c == "(":
                d += 1
            if d == 0:
                flat += c
            if c == ")":
                d -= 1
        first = True
        for m in re.finditer(r"[a-z_][a-z0-9_]*\??", flat):
            w = m.group(0)
            name = w.rstrip("?")
            # A piece that starts with its own description in brackets names a param even if the word reads as
            # an ordinary one (media.swap: "with (another drive: ...)")
            if name in _STOP and not (first and re.match(re.escape(w) + r"\s*\(", p)):
                first = False
                continue
            names.add(name)
            if first and not w.endswith("?") and " or " not in flat and "/" not in flat:
                required.add(name)
            first = False
        for m in re.finditer(r"([a-z_][a-z0-9_]*)\??\s*\(([^()]*)\)", p):
            inner = m.group(2).strip()
            if re.match(r"^[\w.-]+(\|[\w.-]+)+$", inner):
                enums[m.group(1)] = set(inner.split("|"))
            elif re.match(r"^[a-z][a-z0-9_]*(, [a-z][a-z0-9_]*)+$", inner):
                enums[m.group(1)] = set(inner.split(", "))
    return {"names": names, "required": required, "enums": enums}


# ---------------------------------------------------------------- the spec

class Spec:
    """A parsed spec file: its text, the YAML value and locations, and the model."""

    def __init__(self, path, text=None):
        self.path = path
        if text is None:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        self.text = text
        self.issues = []
        self.where = {}  # (node id or "@doc", field) -> line
        self.entry_loc = {}  # node id -> Loc of its key entry (in the parent's keys block)
        self.section_loc = {}  # menu id -> Loc of its section
        self.auto_at = set()  # node ids placed by the tool (no `at` in the spec)
        try:
            self.value, self.loc = deckyaml.load(text, with_locs=True)
        except deckyaml.YamlError as e:
            raise Failed([Issue(f"{path}:{e.line}", e.message)]) from None
        if not isinstance(self.value, dict):
            raise Failed([Issue(f"{path}:1", "a spec is a mapping (scenario: NAME, model, pack, menu ID: ...)")])

    def at(self, line):
        return f"{show(self.path)}:{line}"

    def err(self, line, msg, error=True):
        self.issues.append(Issue(self.at(line), msg, error))

    @property
    def kind(self):
        return "shared" if "shared" in self.value else "scenario"

    @property
    def name(self):
        return str(self.value.get("shared") or self.value.get("scenario") or "")

    def menus(self):
        return [(k[5:].strip(), v, self.loc.keys[k]) for k, v in self.value.items()
                if isinstance(k, str) and k.startswith("menu ")]


def show(path):
    """A path for messages: relative to the working directory when inside it."""
    r = os.path.relpath(path)
    return path if r.startswith("..") else r


def norm_list(v):
    if v is None:
        return None
    if isinstance(v, list):
        return [str(x) for x in v] or None
    return [x for x in re.split(r"[\s,]+", str(v)) if x] or None


def clean(d):
    """A node without None / empty values (what the model compares)."""
    return {k: v for k, v in d.items() if v is not None and v != [] and v != {}}


def spec_model(spec, paths, shared_exists, vocab):
    """The model of a spec: {"doc": {...}, "nodes": {id: fields}, "order": [ids]}; issues into spec.issues."""
    v, L = spec.value, spec.loc
    for k in v:
        if not (isinstance(k, str) and (k in TOP_FIELDS or k.startswith("menu "))):
            spec.err(L.keys[k].line, f"unknown field '{k}' (scenario / shared, model, pack, settings, menu ID)")
    if "scenario" in v and "shared" in v:
        spec.err(L.keys["shared"].line, "a spec is a scenario or a shared menu, not both")
    if not spec.name:
        spec.err(1, "the spec needs 'scenario: NAME' (or 'shared: MENU' for a shared menu)")
    doc = clean({"kind": spec.kind, "name": spec.name, "model": v.get("model"), "pack": v.get("pack"),
                 "settings": v.get("settings")})
    for f in ("model", "pack", "settings"):
        if f in v:
            spec.where[("@doc", f)] = L.keys[f].line
    if isinstance(v.get("settings"), dict) and "pack" in v["settings"]:
        spec.err(L.keys["settings"].line, "the pack is the spec's top-level 'pack' (it becomes settings.pack)")
    nodes, order = {}, []
    menus = spec.menus()
    if not menus:
        spec.err(1, "no menu: a spec needs 'menu ID:' sections (the first is the root)")
        return {"doc": doc, "nodes": nodes, "order": order}
    if spec.kind == "shared" and menus[0][0] != spec.name:
        spec.err(menus[0][2].line, f"the first menu of a shared spec is the shared menu itself: 'menu {spec.name}:'")
    local = {m[0] for m in menus}
    opened = {}
    root_id = menus[0][0]
    for mid, body, mloc in menus:
        spec.section_loc[mid] = mloc
        if not re.match(r"^[A-Za-z0-9_.-]+$", mid):
            spec.err(mloc.line, f"menu id '{mid}': letters, digits, '.', '_', '-'")
        if body is None:
            body = {}
        if not isinstance(body, dict):
            spec.err(mloc.line, f"menu {mid}: a mapping of fields (title, keys, ...)")
            continue
        n = {"kind": "menu"}
        for f, fv in body.items():
            fl = mloc.keys[f].line
            if f not in MENU_FIELDS:
                spec.err(fl, f"menu {mid}: unknown field '{f}' ({', '.join(MENU_FIELDS)})")
                continue
            spec.where[(mid, f)] = fl
            if f == "keys":
                continue
            if f == "dials":
                n["dials"] = dials_model(spec, mid, fv, mloc.keys[f])
            elif f in ("uc", "tags"):
                n[f] = norm_list(fv)
            else:
                n[f] = fv
        if mid in nodes:
            spec.err(mloc.line, f"menu {mid}: the id is used twice")
        nodes[mid] = n
        order.append(mid)
    for mid, body, mloc in menus:
        if not isinstance(body, dict):
            continue
        keys = body.get("keys") or {}
        kloc = mloc.keys.get("keys")
        if not isinstance(keys, dict):
            spec.err(kloc.line if kloc else mloc.line, f"menu {mid}: keys is a mapping 'id: {{at: [row, col], ...}}'")
            continue
        for kid, entry in keys.items():
            eloc = kloc.keys[kid]
            kid = str(kid)
            spec.entry_loc[kid] = eloc
            if entry is None:
                entry = {}
            if not isinstance(entry, dict):
                spec.err(eloc.line, f"key {kid}: a mapping {{at: [row, col], action: ...}}")
                continue
            for f in entry:
                spec.where[(kid, f)] = eloc.keys[f].key_line if f in eloc.keys else eloc.line
            target = entry.get("open")
            if target is not None:
                target = str(target)
                if target in local:
                    if kid != target:
                        spec.err(eloc.line, f"key {kid} opens the local menu {target}: its id must be '{target}'")
                        continue
                    extra = [f for f in entry if f not in ("at", "open")]
                    if extra:
                        spec.err(eloc.line, f"key {kid}: {', '.join(extra)} of a local menu go in its section "
                                            f"'menu {target}:'")
                    if target in opened:
                        spec.err(eloc.line, f"menu {target} is opened twice (also line {opened[target]}); a menu "
                                            "several keys open is a shared menu (src/shared/)")
                        continue
                    if target == root_id:
                        spec.err(eloc.line, f"menu {target} is the root")
                        continue
                    opened[target] = eloc.line
                    n = nodes[target]
                    n["in"] = mid
                    if "at" in entry:
                        n["at"] = entry["at"]
                    continue
                if not shared_exists(target):
                    spec.err(eloc.line, f"key {kid}: 'open: {target}' is neither a menu of this spec nor "
                                        f"src/shared/{target}.spec.yaml")
                    continue
                n = {"kind": "include", "in": mid}
                for f, fv in entry.items():
                    if f not in INCLUDE_FIELDS:
                        spec.err(eloc.line, f"key {kid} (a shared menu): '{f}' is not taken here "
                                            f"({', '.join(INCLUDE_FIELDS)})")
                        continue
                    n[f] = norm_list(fv) if f in ("uc", "tags") else fv
                n["open"] = target
            else:
                n = {"kind": "key", "in": mid}
                for f, fv in entry.items():
                    if f not in KEY_FIELDS or f == "open":
                        spec.err(eloc.line, f"key {kid}: unknown field '{f}' ({', '.join(KEY_FIELDS)})")
                        continue
                    n[f] = norm_list(fv) if f in ("uc", "tags") else fv
            if kid in nodes:
                spec.err(eloc.line, f"key {kid}: the id is used twice")
                continue
            nodes[kid] = n
            order.append(kid)
    for mid in local:
        if mid != root_id and mid not in opened:
            spec.err(spec.section_loc[mid].line, f"menu {mid} is opened from no key ('{mid}: {{at: [r, c], open: "
                                                 f"{mid}}}' in a menu's keys)")
            nodes.pop(mid, None)
            order.remove(mid)
    model = {"doc": doc, "nodes": {k: clean(nodes[k]) for k in order if k in nodes}, "order": order}
    place_auto(model, spec)
    canonical(model, vocab)
    return model


def canonical(model, vocab):
    """A derived value written out although it is the default (a glyph its action has anyway) stands for the
    default: the project cannot tell them apart."""
    for nid, n in model["nodes"].items():
        d = defaults(n, vocab)
        if n.get("kind") in ("key", "menu") and n.get("title") is not None and n["title"] == (d.get("title") or nid):
            del n["title"]
        for f in ("glyph", "glyph_on", "state", "hold"):
            if f in n and d.get(f) is not None and same(n[f], d[f]):
                del n[f]
        if n.get("hold") is False and not d.get("hold"):
            del n["hold"]


def dials_model(spec, mid, v, loc):
    if v is None:
        return None
    if not isinstance(v, dict):
        spec.err(loc.line, f"menu {mid}: dials is a mapping 'index: {{label, turn, press}}'")
        return None
    out = {}
    for idx, d in v.items():
        dl = loc.keys[idx].line
        if not isinstance(idx, int) or idx < 0:
            spec.err(dl, f"menu {mid}: a dial index is a number (0 = the first dial), not '{idx}'")
            continue
        if not isinstance(d, dict):
            spec.err(dl, f"menu {mid}: dial {idx} is a mapping {{label, turn, press}}")
            continue
        bad = [f for f in d if f not in DIAL_FIELDS]
        if bad:
            spec.err(dl, f"menu {mid}: dial {idx}: unknown field(s) {', '.join(bad)} ({', '.join(DIAL_FIELDS)})")
        dd = {k: x for k, x in d.items() if k in DIAL_FIELDS}
        if "uc" in dd:
            dd["uc"] = norm_list(dd["uc"])
        out[str(idx)] = clean(dd)
        spec.where[(mid, f"dial {idx}")] = dl
    return out


def grid_of(model):
    return GRIDS.get(model["doc"].get("model", "plusxl"), (4, 9))


def back_slot(model, mid):
    n = model["nodes"][mid]
    root = n.get("in") is None
    if root and model["doc"].get("kind") != "shared":
        return None
    b = n.get("back", [0, 0])
    return tuple(b) if isinstance(b, list) and len(b) == 2 else None


def place_auto(model, spec=None):
    """Keys without `at`: the free slots row by row (the back key's kept free), in spec order."""
    rows, cols = grid_of(model)
    by_menu = {}
    for nid in model["order"]:
        n = model["nodes"].get(nid)
        if n and n.get("in") is not None:
            by_menu.setdefault(n["in"], []).append(nid)
    for mid, kids in by_menu.items():
        if mid not in model["nodes"]:
            continue
        used = {tuple(model["nodes"][k]["at"]) for k in kids
                if isinstance(model["nodes"][k].get("at"), list) and len(model["nodes"][k]["at"]) == 2}
        b = back_slot(model, mid)
        if b:
            used.add(b)
        free = [(r, c) for r in range(rows) for c in range(cols) if (r, c) not in used]
        for k in kids:
            n = model["nodes"][k]
            if "at" not in n:
                if not free:
                    if spec:
                        spec.err(spec.entry_loc[k].line, f"menu {mid}: no free key left for {k} "
                                                         f"({rows}x{cols} grid)")
                    continue
                n["at"] = list(free.pop(0))
                if spec:
                    spec.auto_at.add(k)


# ---------------------------------------------------------------- defaults (what the spec may leave out)

def first_action(n):
    if n.get("action"):
        return n["action"], n.get("value")
    for s in n.get("steps") or []:
        if isinstance(s, dict) and s.get("action"):
            return s["action"], s.get("value")
    return None, None


def defaults(n, vocab):
    k = n.get("kind")
    d = {}
    if k == "menu":
        d["glyph"] = MENU_GLYPH
        return d
    if k != "key":
        return d
    aid, value = first_action(n)
    if n.get("action"):
        face, face_on, state = vocab.glyph_for(aid, value)
        d.update(glyph=face, glyph_on=face_on, state=state, hold=vocab.default_hold(aid))
        d["title"] = vocab.actions.get(aid, {}).get("label")
    elif aid:
        d["glyph"] = vocab.glyph_for(aid, value)[0]
        d["title"] = vocab.actions.get(aid, {}).get("label")
    elif n.get("nav"):
        d["glyph"] = NAV_GLYPHS.get(n["nav"], MENU_GLYPH)
        d["title"] = {"back": "Back", "home": "Home"}.get(n["nav"])
    return d


def effective(n, vocab, nid):
    """The node with its defaults filled in."""
    e = dict(n)
    d = defaults(n, vocab)
    for f in DERIVED:
        if e.get(f) is None:
            e[f] = d.get(f)
    if e.get("glyph") == "none":
        e["glyph"] = None
    if e.get("title") is None and n.get("kind") != "include":
        e["title"] = nid
    return e


# ---------------------------------------------------------------- model -> Studio project (v2 document)

def spec_step(s):
    """A step of the spec ({action, value?, hold?, params?} | {nav: X} | {kind: ...}) for the generator."""
    if not isinstance(s, dict):
        return s
    if "nav" in s and "kind" not in s and "action" not in s:
        return nav_step(s["nav"])
    return dict(s)


def nav_step(to):
    return {"kind": "nav", "to": to} if to in ("back", "home") else {"kind": "nav", "to": "node", "node": to}


class Project:
    """Builds the v2 documents of a model (the generator does the steps, slots and checks)."""

    def __init__(self, vocab, paths):
        self.vocab, self.paths = vocab, paths

    def face(self, e, suffix=""):
        g = e.get("glyph" + suffix)
        extra = e.get("face" + suffix) or {}
        if g:
            f = {"glyph": g}
            if e.get("label") and not suffix:
                f["label"] = e["label"]
        elif suffix:
            return dict(extra) if extra else None
        elif e.get("show"):
            # a value face: the label is the caption (the renderer falls back to the title): only a label written;
            # the face's own colours (white on the dark key): the warn / alert tints stand out from them
            f = {}
            if e.get("label"):
                f["label"] = e["label"]
        else:
            f = {"label": e.get("label") or str(e.get("title") or "").upper()[:10], "bg": gen.BG, "fg": gen.FG}
        if e.get("show") and not suffix:
            f["value"] = dict(e["show"])  # a value face: the readout as big text, the label as its caption
        f.update(extra)
        return f

    def tags(self, e, extra_meta=()):
        t = [f"uc:{u}" for u in e.get("uc") or []]
        t += list(extra_meta)
        t += list(e.get("tags") or [])
        return t

    def gen_spec(self, model, include_file):
        doc = model["doc"]
        nodes = model["nodes"]
        kids = {}
        for nid in model["order"]:
            if nid in nodes and nodes[nid].get("in") is not None:
                kids.setdefault(nodes[nid]["in"], []).append(nid)
        root = next(nid for nid in model["order"] if nid in nodes and nodes[nid].get("in") is None)

        def node(nid):
            n = nodes[nid]
            e = effective(n, self.vocab, nid)
            k = n["kind"]
            if k == "include":
                out = {"id": nid, "include": include_file(n["open"]), "at": n.get("at")}
                if n.get("title") is not None:
                    out["title"] = n["title"]
                out["tags"] = self.tags(e, [f"open:{n['open']}"])
                if n.get("glyph"):
                    out["face"] = dict({"glyph": n["glyph"]}, **(n.get("face") or {}))
                elif n.get("face"):
                    out["face"] = dict(n["face"])
                if n.get("label"):
                    out.setdefault("face", {})["label"] = n["label"]
                if n.get("extra"):
                    out["extra"] = n["extra"]
                return out
            if k == "menu":
                meta = []
                if n.get("in") is None:
                    if doc.get("kind") == "shared":
                        meta.append("shared")
                for idx, d in sorted((n.get("dials") or {}).items(), key=lambda x: int(x[0])):
                    meta += [f"dial.{idx}.uc:{u}" for u in d.get("uc") or []]
                out = {"id": nid, "menu": True, "title": e["title"], "face": self.face(e),
                       "tags": self.tags(e, meta)}
                if n.get("at") is not None:
                    out["at"] = n["at"]
                for f in ("context", "readonly", "strip"):
                    if n.get(f) is not None:
                        out[f] = n[f]
                if "back" in n:
                    out["back"] = n["back"]
                if n.get("navigation"):
                    out["navigation"] = n["navigation"]
                if n.get("dials"):
                    out["dials"] = []
                    for idx, d in sorted(n["dials"].items(), key=lambda x: int(x[0])):
                        g = {"index": int(idx), "label": d.get("label", "")}
                        for side in ("turn", "press", "left", "right"):
                            if side in d:
                                g[side] = self.step(d[side])
                        out["dials"].append(g)
                # by slot: the project's order does not depend on the spec's
                out["items"] = [node(c) for c in sorted(kids.get(nid, []), key=lambda c: (
                    nodes[c].get("at") or [99, 99], c))]
                if n.get("extra"):
                    out["extra"] = n["extra"]
                return out
            out = {"id": nid, "title": e["title"], "at": n.get("at"), "face": self.face(e),
                   "tags": self.tags(e)}
            fo = self.face(e, "_on")
            if fo:
                out["face_on"] = fo
            if e.get("state"):
                out["state"] = e["state"]
            if n.get("then"):
                out["then"] = n["then"]
            if n.get("action"):
                out["action"] = n["action"]
                for f in ("value", "params"):
                    if n.get(f) is not None:
                        out[f] = n[f]
                if e.get("hold"):
                    out["hold"] = True
            elif n.get("steps"):
                out["steps"] = [self.step(s) for s in n["steps"]]
            elif n.get("nav"):
                out["do"] = nav_step(n["nav"])
            elif n.get("do"):
                out["do"] = n["do"]
            extra = dict(n.get("extra") or {})
            if n.get("activity") is not None:
                extra["activity"] = n["activity"]
            if extra:
                out["extra"] = extra
            return out

        spec = {"name": doc.get("name", ""), "model": doc.get("model", "plusxl"), "root": node(root)}
        if doc.get("kind") == "shared":
            spec["shared"] = True
        settings = dict(doc.get("settings") or {})
        if doc.get("pack"):
            settings["pack"] = doc["pack"]  # the scenario's default; a deck's own pack setting wins
        if settings:
            spec["settings"] = settings
        return spec

    def step(self, s):
        s = spec_step(s)
        if isinstance(s, dict) and s.get("action") and "hold" not in s and self.vocab.default_hold(s["action"]):
            s["hold"] = True
        return s

    def document(self, model, include_file):
        spec = self.gen_spec(model, include_file)
        try:
            doc = gen.Generator(self.vocab.catalogue, spec).scenario()
        except gen.SpecError as e:
            raise Failed([Issue(model["doc"].get("name", "?"), str(e))]) from None
        if model["doc"].get("extra"):
            doc.update(model["doc"]["extra"])
        return doc


# ---------------------------------------------------------------- Studio project -> model

def parse_tags(tags):
    meta, rest = {}, []
    for t in tags or []:
        m = re.match(r"^(uc|glyph|glyph_on|open|pack|dial\.\d+\.uc):(.+)$", str(t))
        if m:
            meta.setdefault(m.group(1), []).append(m.group(2))
        elif t == "shared":
            meta["shared"] = True
        else:
            rest.append(str(t))
    return meta, rest


def glyph_of_image(path):
    """The glyph of a face rendered by an older deckflow ($data/streamdeck/scenarios/faces/<pack>-<px>/<id>.png)."""
    m = re.match(r"^\$data/streamdeck/scenarios/faces/[^/]+-\d+/(.+)\.png$", str(path or ""))
    return m.group(1) if m else None


def face_glyph(face):
    """The glyph a project's face shows (its `glyph` field; an older project's rendered face too); both are taken
    out of `face`, the rest stays."""
    g = face.pop("glyph", None)
    old = glyph_of_image(face.get("image"))
    if old:
        face.pop("image")
    return (str(g) if g else None) or old


def decompile_step(s, vocab):
    if not isinstance(s, dict):
        return s
    if s.get("kind") == "action" and set(s) <= {"kind", "id", "value", "params", "hold"}:
        out = {"action": s["id"]}
        if "value" in s:
            out["value"] = s["value"]
        if isinstance(s.get("params"), dict):
            out["params"] = s["params"]
        if bool(s.get("hold")) != vocab.default_hold(s["id"]):
            out["hold"] = bool(s.get("hold"))
        return out
    if s.get("kind") == "nav" and set(s) <= {"kind", "to", "node"}:
        if s.get("to") in ("back", "home"):
            return {"nav": s["to"]}
        if s.get("to") == "node" and s.get("node"):
            return {"nav": s["node"]}
    return dict(s)


def project_model(doc, vocab, base=None, where=""):
    """The model a Studio project holds (its tags carry the meta). base: the last sync's model, so that an
    unchanged derived value (a title, a glyph) keeps standing for "the default" when the action changed."""
    issues = []
    if not isinstance(doc, dict) or not isinstance(doc.get("root"), dict):
        raise Failed([Issue(where, "not a scenario document (no root)")])
    root = doc["root"]
    meta, _ = parse_tags(root.get("tags"))
    settings = dict(doc.get("settings") or {}) if isinstance(doc.get("settings"), dict) else doc.get("settings")
    pack = settings.pop("pack", None) if isinstance(settings, dict) else None
    d = {"kind": "shared" if meta.get("shared") else "scenario", "name": doc.get("name", ""),
         "model": doc.get("model"), "pack": pack or (meta.get("pack") or [None])[0], "settings": settings}
    extra = {k: v for k, v in doc.items() if k not in ("format", "version", "name", "model", "settings", "root")}
    if extra:
        d["extra"] = extra
    nodes, order = {}, []
    bnodes = (base or {}).get("nodes", {})
    # The derived values as the last sync saw them: a glyph bound to an action since then is not a studio edit
    bderived = (base or {}).get("derived", {})

    def walk(n, parent):
        nid = str(n.get("id", ""))
        if nid in nodes:
            issues.append(Issue(where, f"node id '{nid}' twice"))
            return
        t = n.get("type", "action")
        meta, rest = parse_tags(n.get("tags"))
        m = {"in": parent}
        if parent is not None and isinstance(n.get("at"), list):
            m["at"] = list(n["at"])
        if meta.get("uc"):
            m["uc"] = meta["uc"]
        if rest:
            m["tags"] = rest
        known = {"id", "type", "title", "tags", "at", "face", "face_on", "items", "do", "then", "state", "nav",
                 "dials", "strip", "context", "readonly", "file", "node", "activity", "included_from"}
        x = {k: v for k, v in n.items() if k not in known}
        face = dict(n.get("face") or {})
        b = bnodes.get(nid)
        if t == "include":
            m["kind"] = "include"
            op = (meta.get("open") or [None])[0]
            if not op:
                fm = re.match(r"^(?:.*/)?([^/]+)\.deck\.(?:yaml|yml|json)$", str(n.get("file", "")))
                op = fm.group(1) if fm else str(n.get("file", ""))
            m["open"] = op
            if n.get("title") is not None:
                m["title"] = n["title"]
            g = face_glyph(face) or (meta.get("glyph") or [None])[0]
            if g:
                m["glyph"] = g
            if face.get("label"):
                m["label"] = face.pop("label")
            if face:
                m["face"] = face
            if n.get("node"):
                x["node"] = n["node"]
            if x:
                m["extra"] = x
            nodes[nid] = clean(m)
            order.append(nid)
            return
        if t == "menu":
            m["kind"] = "menu"
            for f in ("context", "readonly", "strip"):
                if n.get(f) is not None:
                    m[f] = n[f]
            nav = dict(n.get("nav") or {})
            if parent is not None or d["kind"] == "shared":
                bk = nav.pop("back", [0, 0]) if "back" in nav else [0, 0]
                if bk is None or bk is False:
                    m["back"] = False
                elif bk != [0, 0]:
                    m["back"] = bk
            if n.get("dials") and nav.get("dials") == "own":
                nav.pop("dials")
            if nav:
                m["navigation"] = nav
            if n.get("dials"):
                dm = {}
                for dl in n["dials"]:
                    idx = str(dl.get("index", 0))
                    dd = {"label": dl.get("label", "")}
                    turn = dl.get("turn")
                    if isinstance(turn, dict) and turn.get("kind") == "dir" and set(turn) <= {"kind", "left",
                                                                                                "right"}:
                        for side in ("left", "right"):
                            if side in turn:
                                dd[side] = decompile_step(turn[side], vocab)
                    elif turn is not None:
                        dd["turn"] = decompile_step(turn, vocab)
                    if dl.get("press") is not None:
                        dd["press"] = decompile_step(dl["press"], vocab)
                    if meta.get(f"dial.{idx}.uc"):
                        dd["uc"] = meta[f"dial.{idx}.uc"]
                    dm[idx] = clean(dd) if dd.get("label") else clean(dict(dd, label=None))
                m["dials"] = dm
        else:
            m["kind"] = "key"
            if isinstance(face.get("value"), dict):
                v = face.pop("value")  # in the spec's order (the project's keys are sorted)
                m["show"] = {k: v[k] for k in SHOW_FIELDS if k in v}
                m["show"].update({k: x for k, x in v.items() if k not in SHOW_FIELDS})
            do = n.get("do") or {}
            if do.get("kind") == "multi" and set(do) <= {"kind", "steps"} and isinstance(do.get("steps"), list):
                m["steps"] = [decompile_step(s, vocab) for s in do["steps"]]
            else:
                ds = decompile_step(do, vocab)
                if "action" in ds:
                    m["action"] = ds["action"]
                    for f in ("value", "params"):
                        if f in ds:
                            m[f] = ds[f]
                    m["_hold"] = bool(do.get("hold"))
                elif "nav" in ds:
                    m["nav"] = ds["nav"]
                elif do:
                    m["do"] = ds
            if n.get("then"):
                m["then"] = n["then"]
            if "activity" in n:
                m["activity"] = n["activity"]
        # derived values: what the project shows, mapped back to "the default" where it is one
        eff = {"title": n["title"]} if "title" in n else {}  # none: the default (the studio shows the id)
        gi, gt = face_glyph(face), (meta.get("glyph") or [None])[0]
        beff = effective(b, vocab, nid) if b and b.get("kind") == m["kind"] else None
        if beff is not None and nid in bderived:
            beff.update(bderived[nid])
        if gi and gt and gi != gt:
            eff["glyph"] = gi if beff and beff.get("glyph") == gt else gt
        else:
            eff["glyph"] = gi or gt
        if m["kind"] == "key":
            fo = dict(n.get("face_on") or {})
            goi, got = face_glyph(fo), (meta.get("glyph_on") or [None])[0]
            eff["glyph_on"] = goi or got
            if fo:
                m["face_on"] = fo
            eff["state"] = n.get("state")
            if "_hold" in m:
                eff["hold"] = m.pop("_hold")
        if eff["glyph"] or m.get("show"):
            if face.get("label"):
                m["label"] = face.pop("label")
            if m.get("show") and not eff["glyph"] and face.get("bg") == gen.BG and face.get("fg") == gen.FG:
                face.pop("bg")
                face.pop("fg")
        else:
            lab = face.pop("label", None)
            if face.get("bg") == gen.BG and face.get("fg") == gen.FG:
                face.pop("bg")
                face.pop("fg")
            if lab and lab != str(n.get("title", nid) or "").upper()[:10]:
                m["label"] = lab
        if face:
            m["face"] = face
        if x:
            m["extra"] = x
        dflt = defaults(m, vocab)
        if m["kind"] != "key":
            eff.pop("glyph_on", None)
        for f, v in eff.items():
            if f == "title" and "title" not in n:
                continue
            if beff is not None and beff.get(f) == v:
                m[f] = b.get(f)
            elif v == (dflt.get(f) if f != "title" else (dflt.get("title") or nid)):
                m[f] = None
            elif f == "glyph" and v is None and dflt.get("glyph"):
                m[f] = "none" if b is not None else None  # a key new in the studio takes its action's glyph
            elif f == "hold" and v is False and not dflt.get("hold"):
                m[f] = None
            else:
                m[f] = v
        nodes[nid] = clean(m)
        order.append(nid)
        for c in n.get("items") or []:
            if isinstance(c, dict):
                walk(c, nid)

    walk(root, None)
    if issues:
        raise Failed(issues)
    return {"doc": clean(d), "nodes": nodes, "order": order}


# ---------------------------------------------------------------- 3-way merge

def same(a, b):
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)


def merge(S, P, B, prefer=None):
    """(merged model, conflicts); a conflict: (node, field, spec value, project value, base value)."""
    conflicts = []

    def fields(s, p, b, nid):
        out = {}
        for f in sorted(set(s) | set(p) | set(b)):
            sv, pv, bv = s.get(f), p.get(f), b.get(f)
            if same(sv, pv):
                v = sv
            elif same(sv, bv):
                v = pv
            elif same(pv, bv):
                v = sv
            elif prefer == "spec":
                v = sv
            elif prefer == "project":
                v = pv
            else:
                conflicts.append((nid, f, sv, pv, bv))
                v = sv
            if v is not None:
                out[f] = v
        return out

    bdoc = B["doc"] if B else {}
    M = {"doc": fields(S["doc"], P["doc"], bdoc, "@doc"), "nodes": {}, "order": []}
    ids = list(S["order"]) + [i for i in P["order"] if i not in S["nodes"]]
    bn = B["nodes"] if B else {}
    for nid in ids:
        s, p, b = S["nodes"].get(nid), P["nodes"].get(nid), bn.get(nid)
        if b is None:
            if s is not None and p is not None:
                M["nodes"][nid] = fields(s, p, {}, nid)
            else:
                M["nodes"][nid] = copy.deepcopy(s if s is not None else p)
        elif s is None and p is None:
            continue
        elif s is None or p is None:
            other = p if s is None else s
            if same(other, b) or prefer == ("spec" if s is None else "project"):
                continue  # deleted on one side, untouched on the other
            if prefer:
                M["nodes"][nid] = copy.deepcopy(other)
            else:
                conflicts.append((nid, "(node)", "deleted" if s is None else "changed",
                                  "deleted" if p is None else "changed", "present"))
                continue
        else:
            M["nodes"][nid] = fields(s, p, b, nid)
        M["order"].append(nid)
    # the structure after the merge: parents that are still menus, one node per slot
    slots = {}
    for nid in M["order"]:
        n = M["nodes"][nid]
        par = n.get("in")
        if par is None:
            continue
        if par not in M["nodes"] or M["nodes"][par].get("kind") != "menu":
            conflicts.append((nid, "in", S["nodes"].get(nid, {}).get("in"), P["nodes"].get(nid, {}).get("in"),
                              f"menu {par} is gone"))
            continue
        at = tuple(n.get("at") or ())
        if at in slots.get(par, {}):
            other = slots[par][at]
            conflicts.append((nid, "at", f"{list(at)} (also {other})", "", "the same key on both sides"))
        slots.setdefault(par, {})[at] = nid
    return M, conflicts


# ---------------------------------------------------------------- model -> spec text (fresh, and patching)

def entry_fields(n, kind):
    names = INCLUDE_FIELDS if kind == "include" else (["at", "open"] if kind == "menu" else KEY_FIELDS)
    return names


def entry_dict(nid, n, auto=False, old=None):
    """The key entry of a node in its parent's keys block, in the old entry's field order where there is one."""
    k = n["kind"]
    if k == "menu":
        vals = {"at": n.get("at"), "open": nid}
    else:
        vals = {f: n.get(f) for f in entry_fields(n, k) if n.get(f) is not None}
        if k == "include":
            vals["open"] = n["open"]
    if auto:
        vals.pop("at", None)
    for f in ("uc", "tags"):
        if isinstance(vals.get(f), list) and len(vals[f]) == 1 and f == "uc":
            vals[f] = vals[f][0]
    order = list(old.keys()) if old else []
    canon = entry_fields(n, k)
    keys = [f for f in order if f in vals] + [f for f in canon if f in vals and f not in order]
    return {f: vals[f] for f in keys}


def section_fields(n):
    out = {}
    for f in MENU_FIELDS:
        if f in ("keys", "dials"):
            continue
        v = n.get(f)
        if v is None:
            continue
        if f == "uc" and isinstance(v, list) and len(v) == 1:
            v = v[0]
        out[f] = v
    return out


def dial_dict(d):
    out = {}
    for f in DIAL_FIELDS:
        if d.get(f) is not None:
            v = d[f]
            out[f] = v[0] if f == "uc" and isinstance(v, list) and len(v) == 1 else v
    return out


def entry_line(indent, key, fields, pad_to=0, comment=""):
    head = f"{deckyaml.flow_key(key)}:"
    if pad_to and len(head) + 1 < pad_to:
        head = head.ljust(pad_to - 1)
    return " " * indent + head + " " + deckyaml.flow(fields) + comment


def write_spec(model, header=None):
    """A spec text from a model (a new spec, or a new section's text)."""
    doc = model["doc"]
    lines = []
    if header:
        lines += [f"# {h}" if h else "#" for h in header]
    lines.append(f"{'shared' if doc.get('kind') == 'shared' else 'scenario'}: {deckyaml.flow_scalar(doc['name'])}")
    for f in ("model", "pack", "settings"):
        if doc.get(f) is not None:
            lines.append(f"{f}: {deckyaml.flow(doc[f])}")
    for mid in [i for i in model["order"] if model["nodes"].get(i, {}).get("kind") == "menu"]:
        lines.append("")
        lines += section_text(model, mid)
    return "\n".join(lines) + "\n"


def children(model, mid):
    """A menu's nodes by slot."""
    kids = [i for i in model["order"] if i in model["nodes"] and model["nodes"][i].get("in") == mid]
    return sorted(kids, key=lambda i: (model["nodes"][i].get("at") or [99, 99], i))


def menu_tree(model):
    """The menus depth first from the root, by slot."""
    root = next((i for i in model["order"] if i in model["nodes"] and model["nodes"][i].get("in") is None), None)
    out = []

    def walk(mid):
        out.append(mid)
        for k in children(model, mid):
            if model["nodes"][k].get("kind") == "menu":
                walk(k)

    if root:
        walk(root)
    return out


def section_text(model, mid, auto=()):
    n = model["nodes"][mid]
    out = [f"menu {mid}:"]
    for f, v in section_fields(n).items():
        out.append(f"  {f}: {deckyaml.flow(v)}")
    if n.get("dials"):
        out.append("  dials:")
        for idx, d in sorted(n["dials"].items(), key=lambda x: int(x[0])):
            out.append(entry_line(4, int(idx), dial_dict(d)))
    kids = children(model, mid)
    if kids:
        out.append("  keys:")
        pad = max(len(deckyaml.flow_key(k)) for k in kids) + 2
        for k in kids:
            out.append(entry_line(4, k, entry_dict(k, model["nodes"][k], k in auto), pad))
    return out


class Patcher:
    """Edits a spec text line by line so that it holds the merged model; comments and layout stay where they can."""

    def __init__(self, spec, S, M):
        self.spec, self.S, self.M = spec, S, M
        self.lines = spec.text.split("\n")
        self.ops = []  # (start index, end index exclusive, new lines)
        self.appended = []  # new sections at the end
        self.old_entries = {}  # node id -> (its entry as the spec had it, its comment): a moved key keeps both

    def replace(self, loc, new):
        self.ops.append((loc.line - 1, loc.end, new))

    def insert(self, before_line_index, new):
        self.ops.append((before_line_index, before_line_index, new))

    def comment(self, loc):
        return deckyaml.comment_of(self.lines[loc.end - 1]) if loc.line == loc.end else ""

    def indent_of(self, loc):
        t = self.lines[loc.line - 1]
        return len(t) - len(t.lstrip())

    def pad_of(self, loc):
        t = self.lines[loc.line - 1].lstrip()
        m = re.match(r"^(\S+:\s*)\{", t)
        return len(m.group(1)) if m else 0

    def block_end(self, sec_loc, field):
        """Index (0-based) after the last line of a field's block in a section (keys, dials)."""
        return sec_loc.keys[field].end

    def run(self):
        S, M, spec = self.S, self.M, self.spec
        sv = spec.value
        # the document's fields
        for f in ("model", "pack", "settings"):
            a, b = S["doc"].get(f), M["doc"].get(f)
            if same(a, b):
                continue
            if f in spec.loc.keys:
                loc = spec.loc.keys[f]
                self.replace(loc, [] if b is None else [f"{f}: {deckyaml.flow(b)}" + self.comment(loc)])
            elif b is not None:
                first = min(spec.loc.keys[k].line for k in sv)  # after the name line
                name_key = "shared" if "shared" in sv else "scenario"
                at = spec.loc.keys[name_key].end if name_key in spec.loc.keys else first
                self.insert(at, [f"{f}: {deckyaml.flow(b)}"])
        if S["doc"].get("name") != M["doc"].get("name"):
            k = "shared" if "shared" in sv else "scenario"
            loc = spec.loc.keys[k]
            self.replace(loc, [f"{k}: {deckyaml.flow_scalar(M['doc']['name'])}" + self.comment(loc)])
        gone_sections = set()
        # menus removed: the whole section (their keys go with it)
        for mid, n in S["nodes"].items():
            if n["kind"] == "menu" and (mid not in M["nodes"] or M["nodes"][mid]["kind"] != "menu"):
                loc = spec.section_loc[mid]
                end = loc.end
                # blank lines after a removed section go with it
                while end < len(self.lines) and self.lines[end].strip() == "":
                    end += 1
                self.ops.append((loc.line - 1, end, []))
                gone_sections.add(mid)
        new_sections = [mid for mid in M["order"] if M["nodes"][mid]["kind"] == "menu"
                        and (mid not in S["nodes"] or S["nodes"][mid]["kind"] != "menu")]
        additions = {}  # menu id -> [lines]
        for nid in M["order"]:
            n = M["nodes"][nid]
            s = S["nodes"].get(nid)
            if s is not None and s["kind"] != n["kind"]:
                s = None
                if nid in spec.entry_loc and S["nodes"][nid].get("in") not in gone_sections:
                    self.replace(spec.entry_loc[nid], [])
            if n.get("in") is None and n["kind"] == "menu":
                if s is not None:
                    self.section(nid, s, n)
                continue
            if n.get("in") in new_sections:
                continue  # written with its new section
            if s is None:
                additions.setdefault(n["in"], []).append(nid)
                if n["kind"] == "menu":
                    continue
                continue
            if n["kind"] == "menu" and nid not in new_sections:
                self.section(nid, s, n)
            moved = s.get("in") != n.get("in")
            old_entry = None
            if nid in spec.entry_loc:
                par = s.get("in")
                old_entry = (sv.get(f"menu {par}") or {}).get("keys", {}).get(nid) if par else None
                self.old_entries[nid] = (old_entry, self.comment(spec.entry_loc[nid]))
            auto = nid in spec.auto_at and same(s.get("at"), n.get("at"))
            new_entry = entry_dict(nid, n, auto, old_entry)
            if moved:
                if nid in spec.entry_loc and s.get("in") not in gone_sections:
                    self.replace(spec.entry_loc[nid], [])
                additions.setdefault(n["in"], []).append(nid)
            elif not same(old_entry, new_entry) and nid in spec.entry_loc and not same(
                    entry_dict(nid, s, nid in spec.auto_at, old_entry), new_entry):
                eloc = spec.entry_loc[nid]
                self.replace(eloc, [entry_line(self.indent_of(eloc), nid, new_entry, self.pad_of(eloc),
                                               self.comment(eloc))])
        # keys removed
        for nid, s in S["nodes"].items():
            if nid in M["nodes"] or s.get("in") is None:
                continue
            if s.get("in") in gone_sections or (s["kind"] == "menu" and nid in gone_sections
                                                  and False):
                continue
            if nid in spec.entry_loc:
                self.replace(spec.entry_loc[nid], [])
        # keys added (or moved here) to menus that exist in the spec
        for mid, kids in additions.items():
            if mid in new_sections:
                continue
            sec = spec.section_loc[mid]
            body = sv.get(f"menu {mid}") or {}
            if "keys" in sec.keys and isinstance(body.get("keys"), dict) and body["keys"]:
                kl = sec.keys["keys"]
                first = next(iter(kl.keys.values()))
                ind, pad = self.indent_of(first), self.pad_of(first)
                at = kl.end
                lines = [self.moved_line(ind, k, pad) for k in kids]
            else:
                ind = self.indent_of(sec) + 4
                at = sec.end
                lines = ([] if "keys" in sec.keys else [" " * (ind - 2) + "keys:"]) + [
                    self.moved_line(ind, k, 0) for k in kids]
                if "keys" in sec.keys:
                    at = sec.keys["keys"].end
            self.insert(at, lines)
        for mid in new_sections:
            self.appended += [""] + section_text(M, mid)
        return self.apply()

    def moved_line(self, ind, k, pad):
        old, comment = self.old_entries.get(k, (None, ""))
        return entry_line(ind, k, entry_dict(k, self.M["nodes"][k], False, old), pad, comment)

    def section(self, mid, s, n):
        """A menu's own fields (not its keys)."""
        sec = self.spec.section_loc[mid]
        body = self.spec.value.get(f"menu {mid}") or {}
        a, b = section_fields(s), section_fields(n)
        ind = self.indent_of(sec) + 2
        last_field_end = sec.line
        for f in body:
            if f not in ("keys",):
                last_field_end = max(last_field_end, sec.keys[f].end)
        for f in MENU_FIELDS:
            if f in ("keys", "dials"):
                continue
            if same(a.get(f), b.get(f)):
                continue
            if f in sec.keys:
                loc = sec.keys[f]
                self.replace(loc, [] if f not in b else [" " * ind + f"{f}: {deckyaml.flow(b[f])}" + self.comment(loc)])
            else:
                self.insert(last_field_end, [" " * ind + f"{f}: {deckyaml.flow(b[f])}"])
        da, db = s.get("dials") or {}, n.get("dials") or {}
        if not same(da, db):
            if "dials" not in sec.keys:
                self.insert(last_field_end, [" " * ind + "dials:"] + [
                    entry_line(ind + 2, int(i), dial_dict(d)) for i, d in sorted(db.items(), key=lambda x: int(x[0]))])
                return
            dl = sec.keys["dials"]
            old = body.get("dials") or {}
            if not db:
                self.replace(dl, [])
                return
            if not isinstance(old, dict) or not old:
                self.replace(dl, [" " * ind + "dials:"] + [
                    entry_line(ind + 2, int(i), dial_dict(d)) for i, d in sorted(db.items(), key=lambda x: int(x[0]))])
                return
            for idx in sorted(set(da) | set(db), key=int):
                eloc = dl.keys.get(int(idx))
                if idx not in db:
                    if eloc:
                        self.replace(eloc, [])
                elif idx not in da or eloc is None:
                    self.insert(dl.end, [entry_line(ind + 2, int(idx), dial_dict(db[idx]))])
                elif not same(da[idx], db[idx]):
                    oe = old.get(int(idx)) or {}
                    nd = dial_dict(db[idx])
                    nd = {f: nd[f] for f in [x for x in oe if x in nd] + [x for x in nd if x not in oe]}
                    self.replace(eloc, [entry_line(self.indent_of(eloc), int(idx), nd, self.pad_of(eloc),
                                                   self.comment(eloc))])

    def apply(self):
        lines = list(self.lines)
        trailing = lines and lines[-1] == ""
        if trailing:
            lines.pop()
        ops = sorted(self.ops, key=lambda o: (o[0], o[1]), reverse=True)
        last_start = None
        for start, end, new in ops:
            if last_start is not None and end > last_start and start != end:
                continue  # inside a range already replaced (a removed section)
            lines[start:end] = new
            if start != end:
                last_start = start
        if self.appended:
            while lines and lines[-1].strip() == "":
                lines.pop()
            lines += self.appended
        return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- checks

def check_model(model, vocab, spec=None, where_default=""):
    """Issues of a model against the vocabularies; spec gives the lines."""
    issues = []

    def at(nid, field=None):
        if spec is not None:
            line = spec.where.get((nid, field)) if field else None
            if line is None and nid in spec.entry_loc:
                line = spec.entry_loc[nid].line
            if line is None and nid in spec.section_loc:
                line = spec.section_loc[nid].line
            if line is not None:
                return spec.at(line)
        return f"{where_default} node {nid}" if nid != "@doc" else where_default

    doc = model["doc"]
    m = doc.get("model", "plusxl")
    if m not in GRIDS:
        issues.append(Issue(at("@doc", "model"), f"model '{m}': one of {', '.join(sorted(GRIDS))}"))
    pack = doc.get("pack", DEFAULT_PACK)
    if str(pack) not in vocab.packs():
        packs = list(vocab.packs())
        issues.append(Issue(at("@doc", "pack"), f"pack '{pack}' is not in data/streamdeck/icons/packs "
                                                f"({', '.join(packs)})"))
    rows, cols = grid_of(model)
    slots = {}
    nodes = model["nodes"]

    def step(s, nid, field):
        if not isinstance(s, dict):
            issues.append(Issue(at(nid, field), f"a step is a mapping {{action: ID, value?}}, not {s!r}"))
            return
        if "action" in s:
            aid = s["action"]
            bad = [f for f in s if f not in ("action", "value", "hold", "params")]
            if bad:
                issues.append(Issue(at(nid, field), f"an action step takes action, value, hold, params; not "
                                                    f"{', '.join(bad)}"))
            if aid not in vocab.actions:
                near = [a for a in vocab.actions if a.split(".")[0] == str(aid).split(".")[0]][:6]
                issues.append(Issue(at(nid, field), f"action '{aid}' is not in the catalogue"
                                                    + (f" (in its group: {', '.join(near)})" if near else "")))
                return
            check_params(aid, s.get("value"), s.get("params"), nid, field)
        elif "nav" in s and "kind" not in s:
            t = s["nav"]
            if t not in ("back", "home") and t not in nodes:
                issues.append(Issue(at(nid, field), f"nav: '{t}' is back, home or a node of this document"))
        elif "kind" in s:
            if s["kind"] in ("shell", "open", "keys", "webapi"):
                issues.append(Issue(at(nid, field), f"kind '{s['kind']}' is not for a shipped scenario (ACC-5)"))
        else:
            issues.append(Issue(at(nid, field), "a step needs action, nav or kind"))

    def check_params(aid, value, params, nid, field):
        p = vocab.params(aid)
        a = vocab.actions[aid]
        for obj in (value, params):
            if not isinstance(obj, dict):
                continue
            for k, v in obj.items():
                if a.get("kind") == "delta" and k in ("d", "rate"):
                    continue
                if p["names"] and k not in p["names"]:
                    issues.append(Issue(at(nid, field), f"{aid}: param '{k}' is not in its params "
                                                        f"({a.get('params', 'none')})"))
                elif not p["names"] and a.get("kind") not in ("delta", "text"):
                    issues.append(Issue(at(nid, field), f"{aid} takes no params (got '{k}')", False))
                if k in p["enums"] and isinstance(v, str) and not v.startswith("$") and v not in p["enums"][k]:
                    issues.append(Issue(at(nid, field), f"{aid}: {k} '{v}' is not one of "
                                                        f"{'|'.join(sorted(p['enums'][k]))}"))
        if isinstance(value, dict) or value is None:
            have = set(value or {}) | set(params or {})
            if "preset" in have:  # dbg.bp_add, port.out: a port preset stands for the address / the port
                have |= {"address", "port"}
            for r in sorted(p["required"] - have):
                if not (r == "value" and value is not None):
                    issues.append(Issue(at(nid, field), f"{aid}: '{r}' is needed ({a.get('params')})", False))
        if a.get("kind") == "press" and value is not None and not isinstance(value, (dict, bool)):
            # a press takes its parameters as an object: a bare value never reaches the action (set.speed: the
            # number became a press, the host ran its default, the next speed)
            name = next(iter(sorted(p["names"])), "value") if len(p["names"]) == 1 else "<param>"
            issues.append(Issue(at(nid, field), f"{aid} is a press: write 'value: {{{name}: {value}}}' "
                                                f"({a.get('params') or 'no params'})"))
        if a.get("kind") == "text" and value is None:
            issues.append(Issue(at(nid, field), f"{aid} is a text action: it needs a value", False))
        if a.get("kind") == "delta" and field not in ("turn", "left", "right") and not str(field).startswith("dial"):
            issues.append(Issue(at(nid, field), f"{aid} is a dial (delta) action, not for a key", False))

    def glyph(g, nid, field):
        if not g or g == "none":
            return
        if g not in vocab.glyphs:
            near = [x for x in vocab.glyphs if x.split(".")[0] == str(g).split(".")[0]][:8]
            issues.append(Issue(at(nid, field), f"glyph '{g}' is not in glyphs.json"
                                                + (f" (its group: {', '.join(near)})" if near else "")))
            return
        missing = vocab.missing_from_packs(g)
        if missing:
            issues.append(Issue(at(nid, field), f"glyph '{g}' is missing from pack(s) {', '.join(missing)}: "
                                                f"{ADD_GLYPH}"))

    def show(v, nid):
        if not isinstance(v, dict) or not isinstance(v.get("readout"), str):
            issues.append(Issue(at(nid, "show"), "show: {readout: <path>, format: <template>, warn: n, alert: n}"))
            return
        bad = [k for k in v if k not in ("readout", "format", "warn", "alert")]
        if bad:
            issues.append(Issue(at(nid, "show"), f"show: unknown field(s) {', '.join(bad)} (readout, format, warn, "
                                                 "alert)"))
        if not vocab.readout_ok(v["readout"]):
            issues.append(Issue(at(nid, "show"), f"show: readout '{v['readout']}' is not a readout of readouts.h "
                                                 "(host.stats.cpu_percent, emulator.stats, debug.breakpoints.total, "
                                                 "...)"))
        if v.get("format") is not None:
            why = value_format_error(v["format"]) if isinstance(v["format"], str) else "a template is a string"
            if why:
                issues.append(Issue(at(nid, "show"), f"show: format: {why}"))
        for t in ("warn", "alert"):
            if v.get(t) is not None and (not isinstance(v[t], (int, float)) or isinstance(v[t], bool)):
                issues.append(Issue(at(nid, "show"), f"show: {t} must be a number"))

    def uc(lst, nid, field):
        for u in lst or []:
            if vocab.use_cases and u not in vocab.use_cases:
                issues.append(Issue(at(nid, field), f"use case '{u}' is not in use-cases.md"))

    for nid in model["order"]:
        n = nodes.get(nid)
        if n is None:
            continue
        if not re.match(r"^[A-Za-z0-9_.-]+$", nid):
            issues.append(Issue(at(nid), f"id '{nid}': letters, digits, '.', '_', '-'"))
        e = effective(n, vocab, nid)
        uc(n.get("uc"), nid, "uc")
        for f in ("glyph", "glyph_on"):
            # the glyphs the key shows (its own or its action's): every pack must draw them
            glyph(n.get(f) if n.get(f) not in (None, "none") or n["kind"] == "include" else e.get(f), nid, f)
        if n["kind"] == "key":
            acts = [n] if n.get("action") else []
            if n.get("action"):
                step({k: n[k] for k in ("action", "value", "hold", "params") if k in n}, nid, "action")
            for s in n.get("steps") or []:
                step(s, nid, "steps")
            if n.get("nav") is not None:
                step({"nav": n["nav"]}, nid, "nav")
            if n.get("do") is not None:
                step(n["do"], nid, "do")
            if n.get("show") is not None:
                show(n["show"], nid)
            if not (acts or n.get("steps") or n.get("nav") or n.get("do") or n.get("show")):
                issues.append(Issue(at(nid), f"key {nid} does nothing: give it action, steps, nav, do, open or show"))
            if sum(1 for f in ("action", "steps", "nav", "do") if n.get(f) is not None) > 1:
                issues.append(Issue(at(nid), f"key {nid}: one of action, steps, nav, do"))
            if n.get("then") not in (None, "stay", "back", "home"):
                issues.append(Issue(at(nid, "then"), f"then: stay, back or home (not '{n['then']}')"))
            if e.get("state") and not vocab.readout_ok(e["state"]):
                issues.append(Issue(at(nid, "state"), f"state '{e['state']}' is not a readout path "
                                                      "(readouts.h: run.state.paused, toggle.<action id>, ...)"))
            if e.get("state") and not e.get("glyph_on") and not n.get("face_on"):
                issues.append(Issue(at(nid, "state"), "state without glyph_on: the face never changes", False))
            if n.get("action") and not e.get("glyph") and not n.get("show"):
                issues.append(Issue(at(nid), f"{n['action']} has no glyph (glyphs.json binds none): the key shows "
                                             "its title", False))
            if n.get("hold") and n.get("action") and not vocab.actions.get(n["action"], {}).get("held"):
                issues.append(Issue(at(nid, "hold"), f"{n['action']} is not a held action", False))
        if n["kind"] == "menu":
            for idx, d in (n.get("dials") or {}).items():
                if int(idx) >= {"plusxl": 6, "plus": 4}.get(m, 0):
                    issues.append(Issue(at(nid, f"dial {idx}"), f"dial {idx}: the {m} has "
                                                               f"{ {'plusxl': 6, 'plus': 4}.get(m, 0)} dials"))
                uc(d.get("uc"), nid, f"dial {idx}")
                for side in ("turn", "press", "left", "right"):
                    if side in d:
                        step(d[side], nid, f"dial {idx}" if spec and (nid, f"dial {idx}") in spec.where else side)
            ctx = n.get("context")
            if ctx is not None and not re.match(r"^[a-z0-9._-]+$", str(ctx)):
                issues.append(Issue(at(nid, "context"), "context: lower case, digits, '.', '_', '-'"))
            b = n.get("back")
            if b is not None and b is not False and not (isinstance(b, list) and len(b) == 2):
                issues.append(Issue(at(nid, "back"), "back: [row, col] or false"))
        par = n.get("in")
        if par is not None:
            a = n.get("at")
            if not (isinstance(a, list) and len(a) == 2 and all(isinstance(x, int) for x in a)):
                issues.append(Issue(at(nid, "at"), f"at: [row, col], not {a!r}"))
                continue
            if not (0 <= a[0] < rows and 0 <= a[1] < cols):
                issues.append(Issue(at(nid, "at"), f"slot {a} is outside the {m} grid ({rows} rows x {cols} keys)"))
            if back_slot(model, par) == tuple(a):
                issues.append(Issue(at(nid, "at"), f"slot {a} is the back key of {par}"))
            other = slots.setdefault(par, {}).get(tuple(a))
            if other:
                issues.append(Issue(at(nid, "at"), f"slot {a} of {par} is taken by {other}"))
            slots[par][tuple(a)] = nid
    return issues


VALUE_TYPES = "fdxX%bk#s"


def value_format_error(fmt):
    """None when fmt is a value template (sdb/valueface.h: {field:spec}, {field?yes|no}, {{ }}), else why not."""
    i = 0
    while i < len(fmt):
        c = fmt[i]
        if c in "{}" and fmt[i + 1:i + 2] == c:
            i += 2
            continue
        if c == "}":
            return "unbalanced '}' (\"}}\" is a brace)"
        if c == "{":
            end = fmt.find("}", i + 1)
            if end < 0:
                return "unbalanced '{' (\"{{\" is a brace)"
            body = fmt[i + 1:end]
            if "{" in body:
                return f"'{{' inside {{{body}}}"
            q, colon = body.find("?"), body.find(":")
            if q >= 0 and (colon < 0 or q < colon):
                field = body[:q]
                if "|" not in body[q + 1:]:
                    return f"{{{body}}}: a condition is ?yes|no"
            else:
                field = body if colon < 0 else body[:colon]
                if colon >= 0 and not re.match(r"^\+?0?\d*(\.\d+)?[" + re.escape(VALUE_TYPES) + r"]?$",
                                                body[colon + 1:]):
                    return f"unknown spec ':{body[colon + 1:]}' in {{{body}}} (types f d x X % b k # s)"
            if not re.match(r"^[A-Za-z0-9_.-]*$", field):
                return f"{{{body}}}: a field is a dotted name (fps, frame_time_us.avg)"
            i = end + 1
            continue
        i += 1
    return None


def find_cli(paths):
    env = os.environ.get("UNREAL_STREAMDECK_BIN")
    cands = [env] if env else []
    for d in sorted(os.listdir(paths.repo)) if os.path.isdir(paths.repo) else []:
        if d.startswith("cmake-build"):
            cands.append(os.path.join(paths.repo, d, "bin", "unreal-streamdeck"))
    cands.append(shutil.which("unreal-streamdeck"))
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def cli_check(paths, project_file):
    """`unreal-streamdeck scenario check` on a project: [] when clean or no CLI found (a note instead)."""
    cli = find_cli(paths)
    if not cli:
        return [Issue(paths.rel(project_file), "unreal-streamdeck not found (build it or set UNREAL_STREAMDECK_BIN): "
                                               "the companion's own check skipped", False)]
    r = subprocess.run([cli, "scenario", "check", project_file, "--catalogue", paths.catalogue],
                       capture_output=True, text=True, timeout=30, env=dict(os.environ, UNREAL_STREAMDECK_DATA=paths.data))
    out = []
    for line in (r.stdout + r.stderr).splitlines():
        line = line.strip()
        if not line or line.startswith("ok:"):
            continue
        err = r.returncode != 0 and ("error" in line.lower() or not line.lower().startswith("warning"))
        out.append(Issue(paths.rel(project_file), "scenario check: " + line, err))
    if r.returncode != 0 and not any(i.error for i in out):
        out.append(Issue(paths.rel(project_file), f"scenario check failed (exit {r.returncode})"))
    return out


# ---------------------------------------------------------------- one scenario: its units

class Unit:
    """One spec and its project: the scenario itself or a shared menu it opens."""

    def __init__(self, paths, name, shared):
        self.paths, self.name, self.shared = paths, name, shared
        self.spec_path = paths.spec(name, shared)
        self.project_path = paths.project(name, shared)
        self.base_path = paths.base(name, shared)

    def include_file(self, target):
        return f"{target}.deck.yaml" if self.shared else f"shared/{target}.deck.yaml"

    def label(self):
        return ("shared/" if self.shared else "") + self.name


def shared_exists_fn(paths):
    return lambda t: os.path.exists(paths.spec(t, True)) or os.path.exists(paths.project(t, True))


def read_text(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write_if_changed(p, text_or_bytes):
    data = text_or_bytes.encode("utf-8") if isinstance(text_or_bytes, str) else text_or_bytes
    if os.path.exists(p):
        with open(p, "rb") as f:
            if f.read() == data:
                return False
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = p + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, p)
    return True


def load_project(path):
    text = read_text(path)
    try:
        if path.endswith(".json"):
            return json.loads(text)
        return deckyaml.load(text)
    except (deckyaml.YamlError, ValueError) as e:
        raise Failed([Issue(path, f"cannot read the project: {e}")]) from None


def strip_order(m):
    return {"doc": m["doc"], "nodes": m["nodes"]}


def base_of(m, vocab):
    """The sync base: the model and, per node, the derived values (glyph, state, ...) the project was built with."""
    b = strip_order(m)
    b["derived"] = {nid: {f: v for f, v in effective(n, vocab, nid).items() if f in DERIVED}
                    for nid, n in m["nodes"].items()}
    return b


class Pipeline:
    def __init__(self, paths=None, vocab=None, log=print):
        self.paths = paths or Paths()
        self.vocab = vocab or Vocab(self.paths)
        self.log = log

    def resolve_name(self, name):
        if name.endswith(".spec.yaml"):
            base = os.path.basename(name)[:-len(".spec.yaml")]
            shared = os.path.basename(os.path.dirname(os.path.abspath(name))) == "shared"
            return base, shared
        if name.startswith("shared/"):
            return name[len("shared/"):], True
        return name, False

    def units(self, name):
        """The scenario's unit and every shared menu it opens (transitively)."""
        base, shared = self.resolve_name(name)
        out, todo, seen = [], [(base, shared)], set()
        while todo:
            n, sh = todo.pop(0)
            if (n, sh) in seen:
                continue
            seen.add((n, sh))
            u = Unit(self.paths, n, sh)
            out.append(u)
            for t in self.opens(u):
                todo.append((t, True))
        return out

    def opens(self, u):
        """Shared menus a unit opens (from its spec, else its project)."""
        try:
            if os.path.exists(u.spec_path):
                sp = Spec(u.spec_path)
                local = {m[0] for m in sp.menus()}
                return sorted({str(e.get("open")) for _, body, _ in sp.menus() if isinstance(body, dict)
                               for e in (body.get("keys") or {}).values()
                               if isinstance(e, dict) and e.get("open") is not None and str(e["open"]) not in local})
            if os.path.exists(u.project_path):
                pm = project_model(load_project(u.project_path), self.vocab)
                return sorted({n["open"] for n in pm["nodes"].values() if n["kind"] == "include"})
        except Failed:
            return []
        return []

    # -- the three models of a unit
    def spec_side(self, u):
        if not os.path.exists(u.spec_path):
            return None, None
        sp = Spec(u.spec_path)
        model = spec_model(sp, self.paths, shared_exists_fn(self.paths), self.vocab)
        if (model["doc"].get("kind") == "shared") != u.shared:
            sp.err(1, "a shared menu's spec lives in src/shared/ and says 'shared: ID'; a scenario's in src/ and "
                      "says 'scenario: NAME'")
        if model["doc"].get("name") != u.name:
            sp.err(1, f"the spec's name '{model['doc'].get('name')}' differs from its file name '{u.name}'")
        errs = [i for i in sp.issues if i.error]
        if errs:
            raise Failed(sp.issues)
        return sp, model

    def base_side(self, u):
        if not os.path.exists(u.base_path):
            return None
        with open(u.base_path, encoding="utf-8") as f:
            return json.load(f)

    def project_side(self, u, base):
        if not os.path.exists(u.project_path):
            return None
        return project_model(load_project(u.project_path), self.vocab, base, self.paths.rel(u.project_path))

    def built(self, u, model):
        """(project text, document) of a model."""
        pr = Project(self.vocab, self.paths)
        doc = pr.document(model, u.include_file)
        return deckyaml.dump_fk(doc), doc

    # -- sync
    def sync_unit(self, u, prefer=None, force_spec=False, dry=False):
        """Returns (changes, issues); raises Failed on conflicts or errors."""
        sp, S = self.spec_side(u)
        B = self.base_side(u)
        P = self.project_side(u, B)
        changes = []
        if S is None and P is None:
            raise Failed([Issue(self.paths.rel(u.spec_path), "no spec and no project")])
        if S is None:
            # a project made in the studio first: the spec is written from it
            M = P
            text = write_spec(M, [f"Spec of {u.label()}: written by deckflow from {self.paths.rel(u.project_path)}"])
            if not dry:
                write_if_changed(u.spec_path, text)
            changes.append(f"wrote {self.paths.rel(u.spec_path)} from the project")
            sp, S = self.spec_side(u)
        elif P is None or force_spec:
            M = S
        else:
            M, conflicts = merge(S, P, B, prefer)
            if conflicts:
                raise Failed([self.conflict_issue(u, sp, c) for c in conflicts])
        issues = check_model(M, self.vocab, sp if same(strip_order(M), strip_order(S)) else None,
                             self.paths.rel(u.project_path))
        if any(i.error for i in issues):
            raise Failed(issues)
        if not same(strip_order(M), strip_order(S)):
            text = Patcher(sp, S, M).run()
            check = Spec(u.spec_path, text)
            again = spec_model(check, self.paths, shared_exists_fn(self.paths), self.vocab)
            if check.issues or not same(strip_order(again), strip_order(M)):
                diff = [f"{k}: {json.dumps(again['nodes'].get(k))} != {json.dumps(M['nodes'].get(k))}"
                        for k in sorted(set(again["nodes"]) | set(M["nodes"]))
                        if not same(again["nodes"].get(k), M["nodes"].get(k))]
                raise Failed([Issue(self.paths.rel(u.spec_path), "internal: the patched spec does not read back as "
                                                                 "the merged model; nothing written. "
                                    + "; ".join([str(i) for i in check.issues] + diff[:5]))])
            if not dry:
                write_if_changed(u.spec_path, text)
            changes.append(f"{self.paths.rel(u.spec_path)}: project changes taken over")
        text, doc = self.built(u, M)
        if not dry and write_if_changed(u.project_path, text):
            changes.append(f"wrote {self.paths.rel(u.project_path)}")
        if not dry:
            write_if_changed(u.base_path, json.dumps(base_of(M, self.vocab), indent=1, sort_keys=True) + "\n")
        return changes, issues, M, doc

    def conflict_issue(self, u, sp, c):
        nid, field, sv, pv, bv = c
        line = sp.where.get((nid, field)) if sp else None
        if line is None and sp and nid in sp.entry_loc:
            line = sp.entry_loc[nid].line
        if line is None and sp and nid in sp.section_loc:
            line = sp.section_loc[nid].line
        where = f"{self.paths.rel(u.spec_path)}:{line}" if line else self.paths.rel(u.spec_path)
        f = lambda v: json.dumps(v, ensure_ascii=False) if not isinstance(v, str) else v  # noqa: E731
        return Issue(where, f"conflict: {nid} {field}: spec {f(sv)}, project ({self.paths.rel(u.project_path)}) "
                            f"{f(pv)}, last sync {f(bv)}")

    def unsynced_project_edits(self, u):
        B = self.base_side(u)
        P = self.project_side(u, B)
        if P is None or B is None:
            return P is not None and B is None and os.path.exists(u.spec_path) and not same(
                strip_order(P), strip_order(self.spec_side(u)[1]))
        return not same(strip_order(P), strip_order(B))

    def build(self, name, force=False, prefer=None, sync=True, cli=True, docs=True):
        """sync (or build) every unit, write the meta description; returns (changes, issues)."""
        units = self.units(name)
        all_changes, all_issues, models = [], [], {}
        if not sync and not force:
            dirty = [u for u in units if self.unsynced_project_edits(u)]
            if dirty:
                raise Failed([Issue(self.paths.rel(u.project_path), "the project holds edits the spec does not "
                                                                    "have yet: run `deckflow.py sync` (or build "
                                                                    "--force: the spec wins)") for u in dirty])
        errors = []
        for u in units:
            try:
                ch, iss, M, doc = self.sync_unit(u, prefer=prefer, force_spec=force)
            except Failed as e:
                errors += e.issues
                continue
            all_changes += ch
            all_issues += iss
            models[u.label()] = (u, M, doc)
        if errors:
            raise Failed(errors + all_issues)
        if cli:
            main = units[0]
            all_issues += cli_check(self.paths, main.project_path)
        if docs and not units[0].shared:
            all_changes += self.write_docs(units[0], models)
        return all_changes, all_issues

    def all_specs(self):
        out = []
        if os.path.isdir(self.paths.src):
            out += [(f[:-len(".spec.yaml")], False) for f in sorted(os.listdir(self.paths.src))
                    if f.endswith(".spec.yaml")]
        sd = os.path.join(self.paths.src, "shared")
        if os.path.isdir(sd):
            out += [(f[:-len(".spec.yaml")], True) for f in sorted(os.listdir(sd)) if f.endswith(".spec.yaml")]
        return out

    # -- the meta description
    def write_docs(self, main, models):
        text = MetaDoc(self, main, models).text()
        out = os.path.join(self.paths.docs, main.name + ".md")
        return [f"wrote {self.paths.rel(out)}"] if write_if_changed(out, text) else []

    def check(self, name):
        """Issues of the spec(s) and the project(s), without writing."""
        issues = []
        for u in self.units(name):
            try:
                sp, S = self.spec_side(u)
                if S is None:
                    issues.append(Issue(self.paths.rel(u.spec_path), "no spec (sync writes it from the project)",
                                        False))
                    continue
                issues += sp.issues
                issues += check_model(S, self.vocab, sp)
                B = self.base_side(u)
                P = self.project_side(u, B)
                if P is not None:
                    M, conflicts = merge(S, P, B)
                    issues += [self.conflict_issue(u, sp, c) for c in conflicts]
                    if not conflicts and not same(strip_order(P), strip_order(B or {"doc": {}, "nodes": {}})):
                        issues.append(Issue(self.paths.rel(u.project_path), "the project holds edits not synced "
                                                                            "into the spec yet (deckflow sync)", False))
                try:
                    self.built(u, S)
                except Failed as e:
                    issues += e.issues
            except Failed as e:
                issues += e.issues
        main = self.units(name)[0]
        if os.path.exists(main.project_path):
            issues += cli_check(self.paths, main.project_path)
        return issues


# ---------------------------------------------------------------- the meta description (markdown)

class MetaDoc:
    def __init__(self, pipe, main, models):
        self.p, self.main, self.models = pipe, main, models
        self.vocab = pipe.vocab

    def img(self, g, w=40):
        """The glyph as the scenario's pack draws it (its source picture; the deck scales it to the key)."""
        meta = self.vocab.packs().get(self.pack, {})
        ext = PACK_SOURCES.get(meta.get("source"), ".png")
        # as the repository names it (the pack is the repository's, the description may be written elsewhere)
        top = os.path.dirname(os.path.dirname(os.path.dirname(self.p.paths.scen)))
        f = os.path.relpath(os.path.join(self.p.paths.packs, self.pack, "src", g + ext), self.p.paths.repo)
        rel = self.relrepo(os.path.join(top, f))
        return f'<img src="{rel}" width="{w}" alt="{g}">'

    def text(self):
        u, M, _ = self.models[self.main.label()]
        doc = M["doc"]
        model = doc.get("model", "plusxl")
        pack, px = doc.get("pack", DEFAULT_PACK), KEY_PX.get(model, 112)
        self.pack = pack
        out = [f"# Stream Deck scenario: {doc['name']}", "",
               f"Generated by `companions/streamdeck/tools/deckflow.py` from "
               f"[`{self.p.paths.top_rel(u.spec_path)}`]({self.relrepo(u.spec_path)}); do not edit (edit the spec or "
               f"the Studio project [`{self.p.paths.top_rel(u.project_path)}`]({self.relrepo(u.project_path)}), then "
               f"`deckflow.py sync {doc['name']}`).", "",
               "| | |", "|:--|:--|", f"| Model | `{model}` ({'x'.join(map(str, grid_of(M)))} keys, {px} px) |",
               f"| Icon pack | `{pack}` (the scenario's `settings.pack`; a deck's own pack setting wins; the pictures "
               f"below are this pack's) |"]
        shared = [lbl for lbl in self.models if lbl != self.main.label()]
        if shared:
            out.append(f"| Shared menus | {', '.join('`' + s + '`' for s in shared)} |")
        out += ["", "## Contents", ""]
        menus = []
        for lbl, (uu, mm, _) in self.models.items():
            for nid in menu_tree(mm):
                menus.append((lbl, uu, mm, nid))
        for lbl, uu, mm, nid in menus:
            out.append(f"- [{self.menu_title(mm, nid)}](#{self.anchor(lbl, nid)})")
        out.append("- [Coverage](#coverage)")
        for lbl, uu, mm, nid in menus:
            out += [""] + self.menu(lbl, uu, mm, nid)
        out += [""] + self.coverage()
        return "\n".join(out) + "\n"

    def relrepo(self, p):
        return os.path.relpath(p, self.p.paths.docs).replace(os.sep, "/")

    def anchor(self, lbl, nid):
        t = f"menu {nid}" + (f" ({lbl})" if lbl != self.main.label() else "")
        return re.sub(r"[^a-z0-9 _-]", "", t.lower()).replace(" ", "-")

    def menu_title(self, mm, nid):
        return f"menu {nid}"

    def menu(self, lbl, u, mm, mid):
        n = mm["nodes"][mid]
        e = effective(n, self.vocab, mid)
        doc = mm["doc"]
        rows, cols = grid_of(mm)
        head = f"## menu {mid}" + (f" ({lbl})" if lbl != self.main.label() else "")
        out = [head, ""]
        facts = [f"Title: {e['title']}"]
        if n.get("uc"):
            facts.append("use cases " + ", ".join(n["uc"]))
        if n.get("context"):
            facts.append(f"context `{n['context']}`")
        if n.get("readonly"):
            facts.append("read-only (its keys only show)")
        if n.get("in"):
            facts.append(f"opened from `{n['in']}` at {n.get('at')}")
        elif doc.get("kind") != "shared":
            facts.append("the root")
        if n.get("strip"):
            facts.append("strip " + ", ".join(f"{k} `{deckyaml.flow(v)}`" for k, v in n["strip"].items()))
        out += ["; ".join(facts) + ".", ""]
        grid = [["" for _ in range(cols)] for _ in range(rows)]
        b = back_slot(mm, mid)
        if b and 0 <= b[0] < rows and 0 <= b[1] < cols:
            grid[b[0]][b[1]] = "*back*"
        kids = children(mm, mid)
        for k in kids:
            kn = mm["nodes"][k]
            ke = effective(kn, self.vocab, k)
            a = kn.get("at") or [0, 0]
            if not (0 <= a[0] < rows and 0 <= a[1] < cols):
                continue
            g, title = self.face_of(kn, ke)
            cell = (self.img(g) + "<br>") if g else ""
            cell += (f"**{kn['label']}** " if kn.get("label") else "") + f"{title}"
            if isinstance(kn.get("show"), dict):
                cell += f"<br>`{kn['show'].get('format') or '{}'}`"
            if kn.get("uc"):
                cell += "<br>" + " ".join(kn["uc"])
            grid[a[0]][a[1]] = cell.replace("|", "\\|")
        out.append("| " + " | ".join(f"{c}" for c in range(cols)) + " |")
        out.append("|" + ":-:|" * cols)
        for r in range(rows):
            out.append("| " + " | ".join(grid[r]) + " |")
        out += ["", "| Key | Slot | Glyph | Title | Does | Use case |", "|:--|:--|:--|:--|:--|:--|"]
        for k in sorted(kids, key=lambda i: tuple(mm["nodes"][i].get("at") or [0, 0])):
            kn = mm["nodes"][k]
            ke = effective(kn, self.vocab, k)
            g, title = self.face_of(kn, ke)
            gtxt = f"{self.img(g, 28)} `{g}`" if g else "(none)"
            if ke.get("glyph_on"):
                gtxt += f" / {self.img(ke['glyph_on'], 28)} `{ke['glyph_on']}` when `{ke.get('state')}`"
            if kn.get("label"):
                gtxt += f" + label {kn['label']}"
            out.append(f"| `{k}` | {kn.get('at')} | {gtxt} | {title.replace('|', '/')} | "
                       f"{self.does(kn, ke).replace('|', '/')} | {' '.join(kn.get('uc') or [])} |")
        if n.get("dials"):
            out += ["", "| Dial | Label | Turn | Press | Use case |", "|:--|:--|:--|:--|:--|"]
            for idx, d in sorted(n["dials"].items(), key=lambda x: int(x[0])):
                turn = self.step_text(d.get("turn")) if "turn" in d else " / ".join(
                    f"{s}: {self.step_text(d[s])}" for s in ("left", "right") if s in d)
                out.append(f"| {idx} | {d.get('label', '')} | {turn or ''} | "
                           f"{self.step_text(d.get('press')) if 'press' in d else ''} | {' '.join(d.get('uc') or [])} |")
        return out

    def face_of(self, kn, ke):
        """(glyph, title) a key shows; a shared menu's from its own spec unless the key overrides them."""
        if kn["kind"] != "include":
            return ke.get("glyph"), str(ke.get("title") or "")
        g, t = kn.get("glyph"), kn.get("title")
        sub = self.models.get("shared/" + kn["open"])
        if sub:
            se = effective(sub[1]["nodes"].get(kn["open"], {"kind": "menu"}), self.vocab, kn["open"])
            g, t = g or se.get("glyph"), t or se.get("title")
        return g, str(t or kn["open"])

    def step_text(self, s):
        if not isinstance(s, dict):
            return ""
        if "action" in s:
            v = s.get("value")
            t = f"`{s['action']}`"
            if v is not None:
                t += " " + deckyaml.flow(v)
            if s.get("hold"):
                t += " (held)"
            return t
        if "nav" in s:
            return f"go to {s['nav']}"
        return deckyaml.flow(s)

    def does(self, kn, ke):
        k = kn["kind"]
        if k == "include":
            return f"opens the shared menu `{kn['open']}`"
        if k == "menu":
            return f"opens menu `{kn.get('id', '')}`".replace("``", "") if False else "opens the menu"
        if kn.get("action"):
            t = self.step_text({"action": kn["action"], "value": kn.get("value"), "hold": ke.get("hold")})
        elif kn.get("steps"):
            t = "; ".join(self.step_text(s) for s in kn["steps"])
        elif kn.get("nav"):
            t = f"go to {kn['nav']}"
        elif kn.get("do") is not None:
            t = deckyaml.flow(kn.get("do"))
        else:
            t = "nothing (shows a value)"
        if isinstance(kn.get("show"), dict):
            sh = kn["show"]
            t = f"shows `{sh.get('format') or '{}'}` of `{sh.get('readout')}`" + (
                "" if t == "nothing (shows a value)" else f"; {t}")
            th = [f"{x} {sh[x]}" for x in ("warn", "alert") if sh.get(x) is not None]
            if th:
                t += f" ({', '.join(th)})"
        if kn.get("then"):
            t += f", then {kn['then']}"
        return t

    def coverage(self):
        v = self.vocab
        covered, used_actions, used_glyphs, no_glyph = {}, set(), set(), set()
        for lbl, (u, mm, _) in self.models.items():
            for nid, n in mm["nodes"].items():
                e = effective(n, v, nid)
                for uc in n.get("uc") or []:
                    covered.setdefault(uc, []).append(nid)
                for d in (n.get("dials") or {}).values():
                    for uc in d.get("uc") or []:
                        covered.setdefault(uc, []).append(f"{nid} dial")
                for f in ("glyph", "glyph_on"):
                    if e.get(f):
                        used_glyphs.add(e[f])
                acts = [n.get("action")] + [s.get("action") for s in n.get("steps") or [] if isinstance(s, dict)]
                for d in (n.get("dials") or {}).values():
                    acts += [d[s].get("action") for s in ("turn", "press", "left", "right") if
                             isinstance(d.get(s), dict)]
                for a in acts:
                    if a:
                        used_actions.add(a)
                        if n.get("action") == a and not e.get("glyph"):
                            no_glyph.add(a)
        covered = {k: sorted(set(v)) for k, v in covered.items()}
        out = ["## Coverage", "", "Use cases with a key or a dial (an item counts when it or its section is named):",
               "", "| Use case | Covered by |", "|:--|:--|"]
        ids = list(v.use_cases)
        for uc in ids:
            if uc in covered:
                out.append(f"| {uc} {v.use_cases[uc].split(':')[0][:60]} | {', '.join(f'`{x}`' for x in covered[uc])} |")
        sections = {}
        for uc in ids:
            if "." in uc and uc not in covered and uc.split(".")[0] not in covered:
                sections.setdefault(uc.split(".")[0], []).append(uc)
        out += ["", "Use cases without a key:", ""]
        for sec in [i for i in ids if "." not in i]:
            if sec in covered:
                continue
            items = sections.get(sec, [])
            if items or sec not in covered:
                out.append(f"- {sec} {v.use_cases[sec]}: {', '.join(items) if items else '(the section)'}")
        unknown = sorted(u for u in covered if u not in v.use_cases)
        if unknown:
            out += ["", "Named but not in use-cases.md: " + ", ".join(unknown)]
        out += ["", "Actions bound without a glyph (the key shows its title): "
                + (", ".join(f"`{a}`" for a in sorted(no_glyph)) or "none") + "."]
        unused = [g["id"] for g in v.glyph_list if g["id"] not in used_glyphs]
        out += ["", f"Glyphs this scenario does not use ({len(unused)} of {len(v.glyph_list)}): "
                + ", ".join(f"`{g}`" for g in unused) + "."]
        return out


# ---------------------------------------------------------------- the deck: daemon, studio, run, watch, shots

DEF_VIEWERS, DEF_API, DEF_STUDIO = 37150, 37153, 37152


def http(method, url, body=None, timeout=5, raw=False):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        b = r.read()
        return b if raw else (json.loads(b) if b else {})


def alive(url):
    try:
        http("GET", url, timeout=1)
        return True
    except (urllib.error.URLError, OSError, ValueError):
        return False


def find_studio(paths):
    env = os.environ.get("UNREAL_STREAMDECK_STUDIO_BIN")
    if env:
        return env
    for d in sorted(os.listdir(paths.repo)):
        if d.startswith("cmake-build"):
            for c in (os.path.join(paths.repo, d, "bin", "unreal-streamdeck-studio.app", "Contents", "MacOS",
                                   "unreal-streamdeck-studio"),
                      os.path.join(paths.repo, d, "bin", "unreal-streamdeck-studio")):
                if os.path.isfile(c):
                    return c
    return shutil.which("unreal-streamdeck-studio")


class Env:
    """The daemon and the studio this tool runs (own ports, never the user's), and the deck it plays on."""

    def __init__(self, paths, args):
        self.paths = paths
        self.api = args.api or DEF_API
        self.viewers = getattr(args, "viewers", None) or DEF_VIEWERS
        self.studio_port = getattr(args, "studio", None) or DEF_STUDIO
        self.deck = args.deck
        self.host = args.host
        self.offscreen = args.offscreen
        self.own_daemon = args.api is None
        self.state_file = os.path.join(paths.scratch, f"env-{self.api}.json")

    def daemon_url(self, p=""):
        return f"http://127.0.0.1:{self.api}/api/v1{p}"

    def studio_url(self, p=""):
        return f"http://127.0.0.1:{self.studio_port}/api/v1{p}"

    def start(self, model, log):
        os.makedirs(self.paths.scratch, exist_ok=True)
        state = {}
        if os.path.exists(self.state_file):
            with open(self.state_file) as f:
                state = json.load(f)
        if self.own_daemon and not alive(self.daemon_url("/health")):
            cli = find_cli(self.paths)
            if not cli:
                raise Failed([Issue("run", "unreal-streamdeck not found: tools/build/build.sh unreal-streamdeck")])
            logf = open(os.path.join(self.paths.scratch, f"daemon-{self.api}.log"), "ab")
            cmd = [cli, "run", "--wait", "--viewers", str(self.viewers), "--api", str(self.api), "--no-lock",
                   "--device", "deckflow-no-usb-deck", "--companion",
                   os.path.join(self.paths.scratch, "companion.json")]
            cmd += ["--host", self.host] if self.host else ["--dry-run", "--no-surface", "--host",
                                                            "http://127.0.0.1:9"]
            p = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, start_new_session=True,
                                 env=dict(os.environ, UNREAL_STREAMDECK_DATA=self.paths.data))
            state["daemon"] = p.pid
            log(f"daemon: pid {p.pid}, automation {self.daemon_url()}, viewers 127.0.0.1:{self.viewers}"
                + ("" if self.host else " (dry run: actions are logged, not sent)"))
            self.wait(lambda: alive(self.daemon_url("/health")), "the daemon")
        if not self.deck and not alive(self.studio_url("/connection")):
            st = find_studio(self.paths)
            if not st:
                raise Failed([Issue("run", "unreal-streamdeck-studio not found: tools/build/build.sh "
                                           "unreal-streamdeck-studio")])
            logf = open(os.path.join(self.paths.scratch, f"studio-{self.studio_port}.log"), "ab")
            env = dict(os.environ, UNREAL_STREAMDECK_DATA=self.paths.data)
            if self.offscreen:
                env["QT_QPA_PLATFORM"] = "offscreen"
            p = subprocess.Popen([st, "--daemon", f"127.0.0.1:{self.viewers}", "--daemon-api", str(self.api),
                                  "--api-port", str(self.studio_port), "--own", model, "--no-daemon-start"],
                                 stdout=logf, stderr=subprocess.STDOUT, start_new_session=True, env=env)
            state["studio"] = p.pid
            log(f"studio: pid {p.pid}, automation {self.studio_url()}, own {model} deck"
                + (" (offscreen)" if self.offscreen else ""))
            self.wait(lambda: alive(self.studio_url("/connection")), "the studio")
        with open(self.state_file, "w") as f:
            json.dump(state, f)
        if not self.deck:
            self.wait(lambda: http("GET", self.studio_url("/connection")).get("state") == "joined", "the own deck")
            c = http("GET", self.studio_url("/connection"))
            if c.get("real"):
                raise Failed([Issue("run", "the studio mirrors a real deck; deckflow plays only on an own deck "
                                           "unless --deck names one")])
            self.deck = c.get("serial") or c.get("deck")
        return self.deck

    def wait(self, cond, what, secs=15):
        t = time.time() + secs
        while time.time() < t:
            try:
                if cond():
                    return
            except (urllib.error.URLError, OSError, ValueError):
                pass
            time.sleep(0.2)
        raise Failed([Issue("run", f"{what} did not come up in {secs} s (logs in {self.paths.rel(self.paths.scratch)})")])

    def play(self, project_file):
        r = http("PUT", self.daemon_url(f"/decks/{self.deck}/scenario"), {"file": project_file, "watch": True})
        return r

    def stop(self, log):
        if not os.path.exists(self.state_file):
            log("nothing started by deckflow on these ports")
            return
        with open(self.state_file) as f:
            state = json.load(f)
        for what in ("studio", "daemon"):
            pid = state.get(what)
            if pid:
                try:
                    os.kill(pid, signal.SIGTERM)
                    log(f"{what}: pid {pid} ended")
                except ProcessLookupError:
                    log(f"{what}: pid {pid} was gone")
        os.remove(self.state_file)


def menus_of(doc, folder):
    """(id, title, depth) of every menu as the deck has it, depth first: an included menu takes the include's id,
    its nodes get '<include id>.' before theirs."""
    out = []

    def walk(n, depth, prefix, folder, as_id=None):
        if n.get("type") == "include":
            f = os.path.join(folder, n.get("file", ""))
            try:
                sub = load_project(f)["root"]
            except (Failed, OSError, KeyError):
                return
            walk(dict(sub, title=n.get("title", sub.get("title"))), depth, prefix + n["id"] + ".",
                 os.path.dirname(f), prefix + n["id"])
            return
        if n.get("type") == "menu":
            nid = as_id or prefix + n["id"]
            out.append((nid, n.get("title", n["id"]), depth))
            for c in sorted(n.get("items") or [], key=lambda c: c.get("at") or [99, 99]):
                walk(c, depth + 1, prefix, folder)

    walk(doc["root"], 0, "", folder)
    return out


def deck_pack(env, deck):
    """The deck's pack setting on the daemon ("" = the scenario's), or None when the daemon has no pack route."""
    try:
        return http("GET", env.daemon_url(f"/decks/{deck}/pack")).get("setting", "")
    except (urllib.error.URLError, OSError, ValueError):
        return None


def set_deck_pack(env, deck, pack):
    """The deck's pack ("scenario" = the scenario's settings.pack); raises Failed when the daemon refuses."""
    try:
        return http("PUT", env.daemon_url(f"/decks/{deck}/pack"), {"pack": pack or "scenario"})
    except urllib.error.HTTPError as e:
        raise Failed([Issue("shots", f"pack '{pack}': the daemon says {e.code} {e.read().decode(errors='replace')}")])
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise Failed([Issue("shots", f"pack '{pack}': the daemon has no pack route ({e})")])


def screenshot(env):
    return http("GET", env.studio_url("/screenshot?area=deck"), raw=True)


def capture_menus(pipe, env, deck, u):
    """(id, title, depth, picture) of every menu the deck plays, then home again."""
    from PIL import Image
    pics = []
    # the daemon holds the resolved document: included menus have their prefixed ids
    for mid, title, depth in menus_of(load_project(u.project_path), os.path.dirname(u.project_path)):
        http("POST", env.studio_url(f"/runtime/navigate?deck={deck}"), {"to": "node", "node": mid})
        t = time.time() + 3
        while time.time() < t:
            st = http("GET", env.studio_url(f"/runtime/state?deck={deck}"))
            if (st.get("page") or {}).get("menu") == mid:
                break
            time.sleep(0.1)
        time.sleep(0.35)  # the viewer draws the page
        pics.append((mid, title, depth, Image.open(io.BytesIO(screenshot(env))).convert("RGB")))
    http("POST", env.studio_url(f"/runtime/navigate?deck={deck}"), {"to": "home"})
    if not pics:
        raise Failed([Issue("shots", "no menu")])
    return pics


def switch_pack(env, deck, pack):
    """Sets the deck's pack and waits until the viewer shows it (the picture changes, at most ~1.5 s)."""
    http("POST", env.studio_url(f"/runtime/navigate?deck={deck}"), {"to": "home"})
    time.sleep(0.35)
    before = screenshot(env)
    set_deck_pack(env, deck, pack)
    t = time.time() + 1.5
    while time.time() < t:
        time.sleep(0.15)
        if screenshot(env) != before:
            break
    time.sleep(0.2)


def sheet_fonts(pipe):
    try:
        sys.path.insert(0, os.path.join(pipe.paths.repo, "tools", "icons"))
        import render_streamdeck_icons as rsi
        return rsi.font(26), rsi.font(18)
    except ImportError:
        return None, None


def shots(pipe, env, name, log, open_it=False, pack=None, all_packs=False):
    """The contact sheet of every menu: the deck's pack set to `pack` (default: the scenario's) for the shots, or
    with all_packs a row per pack; the deck's own pack setting is given back after."""
    u = pipe.units(name)[0]
    deck = env.deck
    page = http("GET", env.daemon_url(f"/decks/{deck}/scenario?document=true"))
    doc = page.get("document")
    if not doc or not page.get("file", "").endswith(os.path.basename(u.project_path)):
        raise Failed([Issue("shots", f"deck {deck} does not play {pipe.paths.rel(u.project_path)}: deckflow run "
                                     f"{name} first")])
    before = deck_pack(env, deck)
    if (pack or all_packs) and before is None:
        raise Failed([Issue("shots", "the daemon has no pack route (GET /api/v1/decks/{serial}/pack): rebuild "
                                     "unreal-streamdeck")])
    big, small = sheet_fonts(pipe)
    try:
        if all_packs:
            try:
                packs = [p["name"] for p in http("GET", env.daemon_url("/packs")).get("packs", [])]
            except (urllib.error.URLError, OSError, ValueError):
                packs = []
            packs = packs or list(pipe.vocab.packs())
            rows = []
            for pk in packs:
                switch_pack(env, deck, pk)
                rows.append((pk, capture_menus(pipe, env, deck, u)))
            sheet = packs_sheet(name, rows, big, small)
            out = os.path.join(pipe.paths.scratch, f"{name}-packs.png")
            what = f"{len(rows)} packs x {len(rows[0][1]) if rows else 0} menus"
        else:
            if before is not None and (pack or before):
                switch_pack(env, deck, pack or "scenario")
            pics = capture_menus(pipe, env, deck, u)
            sheet = menus_sheet(name, pics, pack, big, small)
            out = os.path.join(pipe.paths.scratch, f"{name}-shots.png")
            what = f"{len(pics)} menus" + (f", pack {pack}" if pack else "")
    finally:
        if before is not None and (pack or all_packs or before):
            try:
                set_deck_pack(env, deck, before or "scenario")
            except Failed as e:
                print_issues(e.issues)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out, optimize=True)
    log(f"wrote {pipe.paths.rel(out)} ({what})")
    if open_it and sys.platform == "darwin":
        subprocess.run(["open", "-a", "Preview", out], check=False)
    return out


def menus_sheet(name, pics, pack, big, small):
    from PIL import Image, ImageDraw
    w = max(p[3].width for p in pics)
    lab = 34
    cols = 2 if len(pics) > 2 else 1
    cell_w = w + 24
    cell_h = max(p[3].height for p in pics) + lab + 24
    rows = (len(pics) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w + 24, rows * cell_h + 70), (40, 41, 46))
    d = ImageDraw.Draw(sheet)
    d.text((24, 18), f"{name}: {len(pics)} menus" + (f", pack {pack}" if pack else "") + " (deckflow shots)",
           fill=(235, 235, 235), font=big)
    for i, (mid, title, depth, im) in enumerate(pics):
        x, y = 24 + (i % cols) * cell_w, 70 + (i // cols) * cell_h
        d.text((x, y), f"{'  ' * depth}{mid}: {title}", fill=(210, 210, 210), font=small)
        sheet.paste(im, (x, y + lab))
    return sheet


def packs_sheet(name, rows, big, small, max_w=760):
    """A row per pack (its name at the left), a column per menu; the deck pictures scaled to max_w."""
    from PIL import Image, ImageDraw
    menus = rows[0][1]
    w0 = max(p[3].width for _, pics in rows for p in pics)
    k = min(1.0, max_w / w0)
    cw = round(w0 * k)
    ch = round(max(p[3].height for _, pics in rows for p in pics) * k)
    lab_w, lab_h, pad = 170, 30, 16
    sheet = Image.new("RGB", (lab_w + len(menus) * (cw + pad) + pad, 70 + lab_h + len(rows) * (ch + pad)),
                      (40, 41, 46))
    d = ImageDraw.Draw(sheet)
    d.text((24, 18), f"{name}: {len(menus)} menus in {len(rows)} packs (deckflow shots --all-packs)",
           fill=(235, 235, 235), font=big)
    for c, (mid, title, depth, _) in enumerate(menus):
        d.text((lab_w + c * (cw + pad), 70), f"{mid}: {title}", fill=(210, 210, 210), font=small)
    for r, (pk, pics) in enumerate(rows):
        y = 70 + lab_h + r * (ch + pad)
        d.text((24, y + ch // 2 - 10), pk, fill=(235, 235, 235), font=small)
        for c, (_, _, _, im) in enumerate(pics):
            sheet.paste(im.resize((cw, round(im.height * k)), Image.LANCZOS), (lab_w + c * (cw + pad), y))
    return sheet


def watch_files(pipe, name):
    files = []
    for u in pipe.units(name):
        files += [u.spec_path, u.project_path]
    return files


def mtimes(files):
    return {f: (os.stat(f).st_mtime_ns if os.path.exists(f) else 0) for f in files}


def reload_studio_editor(env, files, log):
    """The studio's editor shows one of the rewritten projects and has no unsaved edits: open it again."""
    try:
        st = http("GET", env.studio_url("/editor"), timeout=1)
    except (urllib.error.URLError, OSError, ValueError):
        return
    path = st.get("path") or ""
    if path and any(os.path.realpath(path) == os.path.realpath(f) for f in files):
        if st.get("dirty"):
            log(f"studio: {path} has unsaved edits; save them (the next sync takes them) or revert")
            return
        http("POST", env.studio_url("/editor/open"), {"path": path})
        log(f"studio: reopened {os.path.basename(path)}")


# ---------------------------------------------------------------- command line

def print_issues(issues, out=sys.stderr):
    for i in issues:
        print(str(i), file=out)


def main(argv=None, paths=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for c in ("check", "build", "sync", "run", "watch", "shots"):
        s = sub.add_parser(c)
        s.add_argument("name")
        if c in ("build", "sync", "run", "watch"):
            s.add_argument("--prefer", choices=("spec", "project"), help="resolve every conflict one way")
            s.add_argument("--no-cli", action="store_true", help="skip `unreal-streamdeck scenario check`")
        if c == "build":
            s.add_argument("--force", action="store_true", help="the spec wins over unsynced project edits")
        if c in ("run", "watch", "shots"):
            s.add_argument("--deck", help="a deck serial (a real deck only when named here)")
            s.add_argument("--api", type=int, help="the automation port of a daemon that runs already "
                                                   f"(default: start one on {DEF_API})")
            s.add_argument("--studio", type=int, help=f"the studio's automation port (default {DEF_STUDIO})")
            s.add_argument("--viewers", type=int, help=f"the device wire port of the daemon started (default "
                                                       f"{DEF_VIEWERS})")
            s.add_argument("--host", help="the unreal-ng WebAPI the actions go to (default: a dry run)")
            s.add_argument("--offscreen", action="store_true", help="the studio without a window")
        if c == "shots":
            s.add_argument("--open", action="store_true", help="open the sheet in Preview")
            g = s.add_mutually_exclusive_group()
            g.add_argument("--pack", help="the deck's pack for the shots (default: the scenario's settings.pack)")
            g.add_argument("--all-packs", action="store_true", help="a row per pack: <name>-packs.png")
    s = sub.add_parser("stop")
    s.add_argument("--api", type=int)
    sub.add_parser("list")
    a = p.parse_args(argv)
    pipe = Pipeline(paths)
    try:
        if a.cmd == "list":
            for name, shared in pipe.all_specs():
                u = Unit(pipe.paths, name, shared)
                st = "no project" if not os.path.exists(u.project_path) else (
                    "project edits to sync" if pipe.unsynced_project_edits(u) else "in sync"
                    if os.path.exists(u.base_path) else "never synced")
                print(f"{u.label():24} {st}")
            return 0
        if a.cmd == "stop":
            Env(pipe.paths, argparse.Namespace(api=a.api, deck=None, host=None, offscreen=False)).stop(print)
            return 0
        if a.cmd == "check":
            issues = pipe.check(a.name)
            print_issues(issues, sys.stdout)
            errs = sum(1 for i in issues if i.error)
            print(f"{a.name}: {errs} error(s), {len(issues) - errs} warning(s)")
            return 1 if errs else 0
        if a.cmd in ("build", "sync", "run", "watch"):
            ch, issues = pipe.build(a.name, force=getattr(a, "force", False), prefer=a.prefer,
                                    sync=a.cmd != "build", cli=not a.no_cli)
            for c in ch:
                print(c)
            print_issues(issues, sys.stdout)
            if any(i.error for i in issues):
                return 1
            print(f"{a.name}: {'up to date' if not ch else 'done'}")
        if a.cmd in ("run", "watch", "shots"):
            env = Env(pipe.paths, a)
            u = pipe.units(a.name)[0]
            model = Spec(u.spec_path).value.get("model", "plusxl")
            deck = env.start(model, print)
            if a.cmd in ("run", "watch"):
                env.play(u.project_path)
                print(f"deck {deck} plays {pipe.paths.rel(u.project_path)} (watched)")
            if a.cmd == "shots":
                shots(pipe, env, a.name, print, a.open, pack=a.pack, all_packs=a.all_packs)
            if a.cmd == "watch":
                files = watch_files(pipe, a.name)
                seen = mtimes(files)
                print("watching " + ", ".join(pipe.paths.rel(f) for f in files) + " (Ctrl+C ends)")
                while True:
                    time.sleep(0.25)
                    now = mtimes(files)
                    if now == seen:
                        continue
                    time.sleep(0.15)  # let an editor finish writing
                    try:
                        ch, issues = pipe.build(a.name, prefer=a.prefer, cli=False)
                        for c in ch:
                            print(time.strftime("%H:%M:%S ") + c)
                        print_issues(issues, sys.stdout)
                        rewritten = [f for f in files if f.endswith(".deck.yaml") and f"wrote {pipe.paths.rel(f)}" in ch]
                        if rewritten:
                            reload_studio_editor(env, rewritten, print)
                    except Failed as e:
                        print_issues(e.issues, sys.stdout)
                        print(time.strftime("%H:%M:%S ") + "not synced: fix the above (the deck keeps the last "
                                                            "good project)")
                    files = watch_files(pipe, a.name)
                    seen = mtimes(files)
        return 0
    except Failed as e:
        print_issues(e.issues)
        return 1
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
