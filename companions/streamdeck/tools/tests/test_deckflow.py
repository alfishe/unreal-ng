#!/usr/bin/env python3
"""Tests of the scenario pipeline (tools/deckflow.py, tools/deckyaml.py) on its fixture specs (tests/deckflow/src):
the YAML subset, the spec's mistakes with their lines, deterministic builds, glyph faces and the pack (settings.pack,
every glyph in every pack), the checked-in sample, and the 3-way sync
(a spec-only change, a project-only change made the way the studio writes it, both sides = a conflict, the round
trip spec -> project -> spec).

  python3 companions/streamdeck/tools/tests/test_deckflow.py
"""
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import deckflow as df  # noqa: E402
import deckyaml as y  # noqa: E402

FIXTURE = os.path.join(HERE, "deckflow")
REPO = df.REPO
DOCS_REL = os.path.join("docs", "inprogress", "2026-10-19-streamdeck-integration", "scenarios")


def temp_paths(tmp, src=os.path.join(FIXTURE, "src")):
    """Paths whose outputs live in tmp (the same layout as the repository's), the vocabularies the repository's."""
    p = df.Paths()
    p.scen = os.path.join(tmp, "data", "streamdeck", "scenarios")
    p.src = os.path.join(p.scen, "src")
    p.sync = os.path.join(p.scen, ".sync")
    p.docs = os.path.join(tmp, DOCS_REL)
    p.scratch = os.path.join(tmp, "scratch")
    if src:
        shutil.copytree(src, p.src)
    return p


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def write(p, text):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def find(node, nid):
    if node.get("id") == nid:
        return node
    for c in node.get("items") or []:
        r = find(c, nid)
        if r:
            return r
    return None


VOCAB = None


class Base(unittest.TestCase):
    def setUp(self):
        global VOCAB
        self.tmp = tempfile.mkdtemp(prefix="deckflow-test-")
        self.paths = temp_paths(self.tmp)
        if VOCAB is None:
            VOCAB = df.Vocab(df.Paths())
        self.vocab = VOCAB
        self.vocab.paths = df.Paths()
        self.pipe = df.Pipeline(self.paths, self.vocab, log=lambda *_: None)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def build(self, name="fx", **kw):
        kw.setdefault("cli", False)
        return self.pipe.build(name, **kw)

    def studio_edit(self, fn, name="fx", shared=False):
        """Edit the project as the studio does: the document as JSON, saved as fkYAML writes it."""
        p = self.paths.project(name, shared)
        doc = y.load(read(p))
        fn(doc)
        write(p, y.dump_fk(doc))

    def spec_text(self, name="fx", shared=False):
        return read(self.paths.spec(name, shared))

    def spec_model(self, name="fx", shared=False):
        sp = df.Spec(self.paths.spec(name, shared))
        return df.strip_order(df.spec_model(sp, self.paths, df.shared_exists_fn(self.paths), self.vocab))

    def project_model(self, name="fx", shared=False):
        return df.strip_order(df.project_model(df.load_project(self.paths.project(name, shared)), self.vocab))


class YamlTest(unittest.TestCase):
    def test_fk_style(self):
        doc = {"format": "streamdeck-scenario", "name": "t: x", "root": {
            "id": "home", "tags": ["uc:C1.1", "a b", "1", "true", "x: y", "#c", "-d", "null", ""],
            "items": [{"at": [0, 1], "title": 'It\'s "q"', "face": {"image": "$data/x.png"}, "e": {}, "l": [],
                       "f": 1.5}]}}
        expected = (
            'format: streamdeck-scenario\nname: "t: x"\nroot:\n  id: home\n  items:\n    -\n      at:\n'
            '        - 0\n        - 1\n      e: {}\n      f: 1.5\n      face:\n        image: $data/x.png\n'
            '      l: []\n      title: "It\'s \\"q\\""\n  tags:\n    - uc:C1.1\n    - "a b"\n    - "1"\n'
            '    - "true"\n    - "x: y"\n    - "#c"\n    - -d\n    - "null"\n    - ""\n')
        self.assertEqual(y.dump_fk(doc), expected)  # what `unreal-streamdeck scenario convert` writes
        self.assertEqual(y.load(expected), doc)

    def test_spec_style_and_lines(self):
        text = ("a: 1  # c\nmenu x:\n  keys:\n    k: {at: [0, 1], v: {p: $data/a b.z80},\n        w: 'it''s'}\n"
                "    m: {t: Save state, u: uc:C1}\n  list:\n    - 1\n    - q: 2\n      r: 3\n")
        v, loc = y.load(text, with_locs=True)
        self.assertEqual(v["menu x"]["keys"]["k"], {"at": [0, 1], "v": {"p": "$data/a b.z80"}, "w": "it's"})
        self.assertEqual(v["menu x"]["list"], [1, {"q": 2, "r": 3}])
        k = loc.keys["menu x"].keys["keys"].keys["k"]
        self.assertEqual((k.line, k.end), (4, 5))
        self.assertEqual(y.comment_of("a: 1  # c"), "  # c")
        for value in ({"title": "Save state", "x": "x: y", "n": "1", "t": "uc:C1.1", "p": "$data/a.png",
                       "c": "a, b", "e": "", "q": "#x", "f": 2.5, "b": False, "z": None},):
            self.assertEqual(y.load("k: " + y.flow(value)), {"k": value})

    def test_errors_name_the_line(self):
        for text, line, what in (("a: {b: 1\n", 1, "not closed"), ("a: 1\n  b: 2\n", 2, "indentation"),
                                 ("a: 1\na: 2\n", 2, "duplicate"), ("a: &x 1\n", 1, "not supported"),
                                 ("a:\n  - 1\n  - [2,\n", 3, "")):
            with self.assertRaises(y.YamlError) as e:
                y.load(text)
            self.assertEqual(e.exception.line, line, text)
            self.assertIn(what, e.exception.message)


class SpecTest(Base):
    def issues_of(self, old, new):
        p = self.paths.spec("fx")
        text = self.spec_text()
        self.assertIn(old, text)
        write(p, text.replace(old, new, 1))
        return [str(i) for i in self.pipe.check("fx")]

    def expect(self, old, new, what, line=None):
        issues = self.issues_of(old, new)
        hits = [i for i in issues if what in i]
        self.assertTrue(hits, f"{what!r} not in {issues}")
        if line:
            self.assertIn(f"fx.spec.yaml:{line}:", hits[0])
        write(self.paths.spec("fx"), read(os.path.join(FIXTURE, "src", "fx.spec.yaml")))

    def test_clean(self):
        self.assertEqual([str(i) for i in self.pipe.check("fx") if i.error], [])

    def test_mistakes(self):
        self.expect("action: mach.nmi}", "action: mach.nmy}", "action 'mach.nmy' is not in the catalogue", 15)
        self.expect("glyph: mach.start,", "glyph: mach.stort,", "glyph 'mach.stort'", 17)
        self.expect("uc: C1.2,", "uc: C1.99,", "use case 'C1.99'", 14)
        self.expect("{at: [0, 1], uc: C1.2", "{at: [0, 1], uc: C1.2, state: run.nothing", "not a readout path", 14)
        self.expect("{at: [0, 1], uc: C1.2", "{at: [4, 1], uc: C1.2", "outside the plusxl grid", 14)
        self.expect("{at: [0, 1], uc: C1.2", "{at: [0, 0], uc: C1.2", "is taken by", 14)
        self.expect("qs:     {at: [0, 1]", "qs:     {at: [0, 0]", "back key of tools", 26)
        self.expect("open: fxspeed}", "open: nowhere}", "neither a menu of this spec", 19)
        self.expect("tools:  {at: [2, 0], open: tools}", "tool:   {at: [2, 0], open: tools}", "its id must be 'tools'",
                    20)
        self.expect("tools:  {at: [2, 0], open: tools}", "", "is opened from no key", 22)
        self.expect("value: {op: save, slot: 1}", "value: {op: keep, slot: 1}", "op 'keep' is not one of load|save",
                    26)
        self.expect("value: {op: save, slot: 1}", "value: {op: save, slots: 1}", "param 'slots'", 26)
        self.expect("{at: [0, 1], uc: C1.2, action: mach.reset}", "{at: [0, 1], uc: C1.2, action: mach.reset, colour: x}",
                    "unknown field 'colour'", 14)
        self.expect("    0: {label: TTD", "    9: {label: TTD", "the plusxl has 6 dials", 11)
        self.expect("  context: play", "  context: Play!", "context: lower case", 9)
        self.expect("menu tools:\n  title: Tools", "menu tools:\n  title: Tools\n  keys: [a, b]", "duplicate key", 26)
        self.expect("steps: [{action: mach.start", "steps: [{kind: shell, command: ls}, {action: mach.start",
                    "ACC-5", 18)
        self.expect("pack: magicwb", "pack: magicwc", "pack 'magicwc'", 4)
        self.expect("{at: [1, 0], uc: C2.1", "{at: [1, 0], uc: C2.1, then: later", "then: stay, back or home", 16)
        self.expect("settings: {brightness: 50}", "settings: {brightness: 50", "not closed", 5)
        # value keys: the readout against readouts.h, the template, the thresholds
        self.expect("readout: host.stats.cpu_percent", "readout: host.stat.cpu", "readout 'host.stat.cpu' is not", 29)
        self.expect('format: "{:.0f}%"', 'format: "{:q}%"', "show: format: unknown spec ':q'", 29)
        self.expect('format: "{:.0f}%"', 'format: "{:.0f%"', "show: format: unbalanced", 29)
        self.expect("warn: 80", "warn: high", "show: warn must be a number", 29)
        self.expect("alert: 95}}", "alert: 95, colour: red}}", "show: unknown field(s) colour", 29)
        self.expect(', show: {readout: host.stats.cpu_percent, format: "{:.0f}%", warn: 80, alert: 95}}', "}",
                    "key cpu does nothing", 29)

    def test_auto_slot_and_defaults(self):
        m = self.spec_model()["nodes"]
        self.assertEqual(m["nmi"]["at"], [0, 2])  # the first free key
        e = df.effective(m["pause"], self.vocab, "pause")
        self.assertEqual((e["glyph"], e["glyph_on"], e["state"]), ("run.pause", "run.play", "run.state.paused"))
        self.assertTrue(df.effective(m["reset"], self.vocab, "reset")["hold"])  # a held action
        self.assertEqual(df.effective(m["qs"], self.vocab, "qs")["glyph"], "snapshot.quick_save")  # per param
        self.assertEqual(m["speed"]["kind"], "include")


class BuildTest(Base):
    def test_project(self):
        self.build()
        doc = y.load(read(self.paths.project("fx")))
        root = doc["root"]
        self.assertEqual((doc["format"], doc["version"], doc["name"]), ("streamdeck-scenario", 2, "fx"))
        self.assertEqual(doc["settings"], {"brightness": 50, "pack": "magicwb"})  # the spec's pack: the default
        self.assertFalse([t for t in root["tags"] if t.startswith(("pack:", "glyph:"))])
        pause = find(root, "pause")
        self.assertEqual(pause["do"], {"kind": "action", "id": "run.pause_toggle"})
        # glyph faces: the daemon draws them from the pack chosen at run time
        self.assertEqual(pause["face"], {"glyph": "run.pause"})
        self.assertEqual(pause["face_on"], {"glyph": "run.play"})
        self.assertEqual(pause["tags"], ["uc:C1.1"])
        self.assertEqual(find(root, "reset")["do"], {"kind": "action", "id": "mach.reset", "hold": True})
        self.assertEqual(find(root, "x4")["face"], {"glyph": "set.speed", "label": "X4"})
        inc = find(root, "speed")
        self.assertEqual((inc["type"], inc["file"], inc["at"]), ("include", "shared/fxspeed.deck.yaml", [1, 3]))
        self.assertIn("open:fxspeed", inc["tags"])
        tools = find(root, "tools")
        self.assertEqual((tools["nav"], tools["tags"], tools["face"]), ({"back": [0, 0]}, ["uc:C6"],
                                                                       {"glyph": "nav.folder"}))
        self.assertEqual(find(root, "back")["do"], {"kind": "nav", "to": "back"})
        self.assertIn("dial.0.uc:U2.1", root["tags"])
        # value faces: the spec's `show` is the face's value; a key with nothing else to do has no "do"
        cpu, fps = find(root, "cpu"), find(root, "fps")
        self.assertEqual(cpu["face"]["value"], {"readout": "host.stats.cpu_percent", "format": "{:.0f}%", "warn": 80,
                                                "alert": 95})
        self.assertEqual(cpu["face"]["label"], "CPU")
        self.assertNotIn("do", cpu)
        self.assertEqual(fps["face"], {"glyph": "run.pause", "value": {"readout": "emulator.stats",
                                                                        "format": "{fps:.0f} FPS"}})
        self.assertEqual(fps["do"], {"kind": "action", "id": "run.pause_toggle"})
        shared = y.load(read(self.paths.project("fxspeed", True)))
        self.assertEqual(shared["root"]["nav"], {"back": [0, 0]})  # a shared menu keeps its back key
        self.assertIn("shared", shared["root"]["tags"])
        self.assertNotIn("settings", shared)  # no pack: a shared menu follows the scenario's (the deck's) pack
        md = read(os.path.join(self.paths.docs, "fx.md"))
        for text in ("## menu home", "## menu fxspeed (shared/fxspeed)", "`run.pause_toggle`", "| C1.1 ",
                     "shows `{:.0f}%` of `host.stats.cpu_percent` (warn 80, alert 95)",
                     "Use cases without a key", "Glyphs this scenario does not use",
                     '<img src="../../../../data/streamdeck/icons/packs/magicwb/src/run.pause.png"'):
            self.assertIn(text, md)

    def test_deterministic(self):
        self.build()
        first = self.snapshot()
        other = tempfile.mkdtemp(prefix="deckflow-test-")
        try:
            pipe = df.Pipeline(temp_paths(other), self.vocab, log=lambda *_: None)
            pipe.build("fx", cli=False)
            self.assertEqual(self.snapshot(other), first)
        finally:
            shutil.rmtree(other, ignore_errors=True)
        for text in first.values():
            for local in ("/Users/", "/Volumes/", "C:\\", "/home/", "/private/", "/var/folders"):
                self.assertNotIn(local, text.decode("utf-8", "replace"))
        self.assertEqual(self.build()[0], [])  # again: nothing to write

    def snapshot(self, root=None):
        root = root or self.tmp
        out = {}
        for d, _, files in os.walk(root):
            for f in files:
                p = os.path.join(d, f)
                with open(p, "rb") as fh:
                    out[os.path.relpath(p, root)] = fh.read()
        return out

    def test_screen_maps(self):
        """build writes a map of every page above its `menu ID:` line (a cell per key, `>` opens a page, `<BACK`),
        keeps one map per page however often it runs, and check says when a map no longer matches the keys"""
        self.build()
        text = read(self.paths.spec("fx"))
        self.assertIn("# +-- screen home: ", text)
        self.assertEqual(text.count("# +-- screen home: "), 1)
        head = text.index("# +-- screen home: ")
        self.assertLess(head, text.index("\nmenu home:"))
        self.assertIn("NMI", text[head:text.index("\nmenu home:")])  # no label: the key's id
        self.assertEqual(self.build()[0], [])  # the maps are up to date: nothing to write
        with open(self.paths.spec("fx"), "w", encoding="utf-8") as f:
            f.write(text.replace("action: mach.nmi}", "action: mach.nmi, label: MAGIC}", 1))
        pipe = df.Pipeline(self.paths, self.vocab, log=lambda *_: None)
        self.assertTrue([i for i in pipe.check("fx") if "screen maps" in str(i)])
        self.build()
        text = read(self.paths.spec("fx"))
        self.assertEqual(text.count("# +-- screen home: "), 1)
        self.assertIn("MAGIC", text[text.index("# +-- screen home: "):text.index("\nmenu home:")])

    def test_no_faces_rendered(self):
        self.build()
        for d, dirs, files in os.walk(self.tmp):
            self.assertNotIn("faces", dirs, d)
            self.assertFalse([f for f in files if f.endswith(".png")], d)

    def test_glyph_missing_from_a_pack(self):
        """Every pack draws every glyph a scenario uses: a pack without one is an error that says how to add it."""
        packs = os.path.join(self.tmp, "packs")
        repo_packs = df.Paths().packs
        shutil.copytree(os.path.join(repo_packs, "magicwb"), os.path.join(packs, "magicwb"))
        shutil.copytree(os.path.join(repo_packs, "os4"), os.path.join(packs, "os4"))
        os.remove(os.path.join(packs, "os4", "src", "mach.nmi.png"))  # the action's default glyph
        os.remove(os.path.join(packs, "os4", "src", "mach.start.png"))  # a glyph the spec names
        vp = df.Paths()
        vp.packs = packs
        vocab = df.Vocab(vp)
        pipe = df.Pipeline(self.paths, vocab, log=lambda *_: None)
        issues = [str(i) for i in pipe.check("fx") if i.error]
        nmi = [i for i in issues if "glyph 'mach.nmi'" in i]
        self.assertTrue(nmi, issues)
        self.assertIn("fx.spec.yaml:15:", nmi[0])
        self.assertIn("is missing from pack(s) os4: add the glyph to tools/icons/streamdeck_glyphs.py, draw it in "
                      "tools/icons/streamdeck_packs/magicwb.py, run tools/icons/regenerate_streamdeck_icons.py",
                      nmi[0])
        self.assertTrue([i for i in issues if "glyph 'mach.start'" in i and "fx.spec.yaml:17:" in i], issues)
        self.assertEqual(len([i for i in issues if "missing from pack" in i]), 2, issues)
        with self.assertRaises(df.Failed):
            pipe.build("fx", cli=False)

    def test_old_project_faces_still_read(self):
        """A project an older deckflow wrote (rendered faces, glyph / pack tags) reads as the same model."""
        self.build()
        model = self.project_model()

        def old(d):
            def walk(n):
                for f, tag in (("face", "glyph"), ("face_on", "glyph_on")):
                    fc = n.get(f) or {}
                    if fc.get("glyph"):
                        g = fc.pop("glyph")
                        fc["image"] = f"$data/streamdeck/scenarios/faces/magicwb-112/{g}.png"
                        if n.get("type") != "include":
                            n.setdefault("tags", []).append(f"{tag}:{g}")
                for c in n.get("items") or []:
                    walk(c)
            walk(d["root"])
            d["root"]["tags"].append("pack:" + d["settings"].pop("pack"))
        self.studio_edit(old)
        self.assertEqual(self.project_model(), model)

    def test_checked_in_sample_is_the_pipelines(self):
        """data/streamdeck/scenarios/sample.deck.yaml (+ shared/speed) and its meta description are the pipeline's
        output of src/sample.spec.yaml: run `deckflow.py sync sample` after editing either."""
        tmp = tempfile.mkdtemp(prefix="deckflow-test-")
        try:
            paths = temp_paths(tmp, os.path.join(REPO, "data", "streamdeck", "scenarios", "src"))
            pipe = df.Pipeline(paths, self.vocab, log=lambda *_: None)
            pipe.build("sample", cli=False)
            repo = df.Paths()
            for mine, shipped in ((paths.project("sample"), repo.project("sample")),
                                  (paths.project("speed", True), repo.project("speed", True)),
                                  (os.path.join(paths.docs, "sample.md"), os.path.join(repo.docs, "sample.md"))):
                self.assertEqual(read(mine), read(shipped), f"{shipped}: run deckflow.py sync sample")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_build_refuses_unsynced_project_edits(self):
        self.build()
        self.studio_edit(lambda d: find(d["root"], "nmi").update(title="Magic"))
        with self.assertRaises(df.Failed) as e:
            self.build(sync=False)
        self.assertIn("deckflow.py sync", str(e.exception))
        self.build(sync=False, force=True)  # the spec wins
        self.assertEqual(find(y.load(read(self.paths.project("fx")))["root"], "nmi")["title"], "NMI")

    @unittest.skipUnless(df.find_cli(df.Paths()), "unreal-streamdeck not built")
    def test_companion_check(self):
        self.build()
        issues = df.cli_check(self.paths, self.paths.project("fx"))
        self.assertEqual([str(i) for i in issues if i.error], [])


class SyncTest(Base):
    def setUp(self):
        super().setUp()
        self.build()
        self.spec0 = self.spec_text()

    def test_round_trip_is_identity(self):
        self.assertEqual(self.project_model(), self.spec_model())  # spec -> project -> model
        self.assertEqual(self.build()[0], [])  # nothing changed: nothing written
        self.assertEqual(self.spec_text(), self.spec0)
        before = self.spec_model()
        os.remove(self.paths.spec("fx"))  # the spec from the project alone (a scenario made in the studio)
        os.remove(self.paths.base("fx"))
        self.build()
        self.assertEqual(self.spec_model(), before)
        self.assertEqual(self.project_model(), before)

    def test_a_glyph_bound_later_is_not_a_studio_edit(self):
        """A project built while an action had no glyph, then glyphs.json binds one: the sync takes the new glyph
        into the project and leaves the spec alone (no `glyph: none` read back from the old project)."""
        bound = self.vocab.glyph_actions.pop("mach.nmi")
        try:
            self.build(force=True)
            self.assertNotIn("glyph", find(y.load(read(self.paths.project("fx")))["root"], "nmi").get("face", {}))
        finally:
            self.vocab.glyph_actions["mach.nmi"] = bound
        self.build()
        self.assertEqual(self.spec_text(), self.spec0)
        self.assertEqual(find(y.load(read(self.paths.project("fx")))["root"], "nmi")["face"]["glyph"],
                         bound["face"])

    def test_spec_only_change(self):
        write(self.paths.spec("fx"), self.spec0.replace("action: mach.nmi}", "action: mach.nmi, title: Magic}")
              .replace("value: {value: 4}, label: X4", "value: {value: 8}, label: X8"))
        changes, _ = self.build()
        self.assertTrue(any("wrote" in c and "fx.deck.yaml" in c for c in changes), changes)
        root = y.load(read(self.paths.project("fx")))["root"]
        self.assertEqual(find(root, "nmi")["title"], "Magic")
        self.assertEqual((find(root, "x4")["do"]["value"], find(root, "x4")["face"]["label"]), ({"value": 8}, "X8"))
        self.assertNotIn("project changes taken over", " ".join(changes))

    def test_project_only_changes(self):
        def edits(d):
            r = d["root"]
            a, b = find(r, "pause"), find(r, "reset")  # a move onto an occupied key swaps (MoveNode, swap: true)
            a["at"], b["at"] = b["at"], a["at"]
            find(r, "nmi")["title"] = "Magic NMI"  # retitle
            x4 = find(r, "x4")
            x4["do"]["value"] = {"value": 16}  # rebind
            x4["face"]["label"] = "X16"
            r["items"].append({"id": "mute", "type": "action", "at": [3, 0], "title": "Mute",
                               "do": {"kind": "action", "id": "audio.mute_toggle"},
                               "face": {"icon": "mute", "label": "MUTE"}})  # a new key from the palette
            r["items"] = [c for c in r["items"] if c["id"] != "bench"]  # delete
            ql = find(r, "ql")  # into another menu
            find(r, "tools")["items"].remove(ql)
            ql["at"] = [2, 5]
            r["items"].append(ql)
            find(r, "pause")["face"]["glyph"] = "run.play"  # another glyph (the inspector's glyph field)
            find(r, "cpu")["face"]["value"]["format"] = "{:.1f} %"  # the inspector's value fields
            find(r, "cpu")["face"]["value"].pop("warn")
            r["items"].append({"id": "more", "type": "menu", "at": [3, 4], "title": "More", "items": [
                {"id": "more.nmi", "type": "action", "at": [0, 1], "do": {"kind": "action", "id": "mach.nmi"}}],
                "nav": {"back": [0, 0]}})  # a new menu with a key
        self.studio_edit(edits)
        changes, _ = self.build()
        self.assertIn("project changes taken over", " ".join(changes))
        text = self.spec_text()
        self.assertIn("    pause:  {at: [0, 1], uc: C1.1, action: run.pause_toggle, glyph: run.play}    # the "
                      "state-bound face", text)
        self.assertIn("    reset:  {at: [0, 0], uc: C1.2, action: mach.reset}", text)
        self.assertIn("    nmi:    {uc: C1.3, action: mach.nmi, title: Magic NMI}                         # placed "
                      "by the tool: [0, 2]", text)  # still placed by the tool, the comment kept
        self.assertIn("    x4:     {at: [1, 0], uc: C2.1, action: set.speed, value: {value: 16}, label: X16}", text)
        self.assertIn("    mute:   {at: [3, 0], action: audio.mute_toggle, face: {icon: mute}}", text)
        self.assertNotIn("bench", text)
        self.assertIn('    cpu:    {at: [1, 1], uc: C10.2, label: CPU, show: {readout: host.stats.cpu_percent, '
                      'format: "{:.1f} %", alert: 95}}', text)
        self.assertIn("    ql:     {at: [2, 5], uc: C6.1, action: snapshot.quick, value: {op: load, slot: 1}, "
                      "label: L1}", text.split("menu tools:")[0])
        self.assertIn("menu more:\n  title: More\n  keys:\n    more.nmi: {at: [0, 1], action: mach.nmi}", text)
        self.assertIn("    more:   {at: [3, 4], open: more}", text)
        self.assertIn("# Test fixture of deckflow", text)
        self.assertEqual(self.spec_model(), self.project_model())
        self.assertEqual(self.build()[0], [])  # in sync now
        # the default glyph follows a rebind made in the studio
        self.studio_edit(lambda d: find(d["root"], "reset")["do"].update(id="mach.cold_reset", hold=False))
        self.build()
        root = y.load(read(self.paths.project("fx")))["root"]
        self.assertEqual(find(root, "reset")["face"], {"glyph": "mach.cold_reset"})
        self.assertIn("reset:  {at: [0, 0], uc: C1.2, action: mach.cold_reset}", self.spec_text())

    def test_conflict(self):
        write(self.paths.spec("fx"), self.spec0.replace("{at: [0, 1], uc: C1.2", "{at: [0, 3], uc: C1.2"))
        self.studio_edit(lambda d: find(d["root"], "reset").update(at=[0, 4]))
        spec, proj, base = (read(self.paths.spec("fx")), read(self.paths.project("fx")),
                            read(self.paths.base("fx")))
        with self.assertRaises(df.Failed) as e:
            self.build()
        msg = str(e.exception)
        line = spec.split("\n").index(next(l for l in spec.split("\n") if l.lstrip().startswith("reset:"))) + 1
        self.assertIn(f"fx.spec.yaml:{line}: error: conflict: reset at: spec [0, 3], project", msg)
        self.assertIn("[0, 4], last sync [0, 1]", msg)
        self.assertEqual((read(self.paths.spec("fx")), read(self.paths.project("fx")), read(self.paths.base("fx"))),
                         (spec, proj, base))  # nothing written
        self.assertIn("conflict", " ".join(str(i) for i in self.pipe.check("fx")))
        self.build(prefer="project")
        self.assertIn("reset:  {at: [0, 4]", self.spec_text())

    def test_both_sides_other_fields_merge(self):
        write(self.paths.spec("fx"), self.spec0.replace("action: mach.reset}", "action: mach.reset, title: Reset!}"))
        self.studio_edit(lambda d: find(d["root"], "x4").update(at=[1, 5]))
        self.build()
        self.assertIn("title: Reset!", self.spec_text())
        self.assertIn("x4:     {at: [1, 5]", self.spec_text())
        root = y.load(read(self.paths.project("fx")))["root"]
        self.assertEqual((find(root, "reset")["title"], find(root, "x4")["at"]), ("Reset!", [1, 5]))

    def test_delete_against_edit_is_a_conflict(self):
        write(self.paths.spec("fx"), self.spec0.replace("    nmi:    {uc: C1.3, action: mach.nmi}"
                                                        "                         # placed by the tool: [0, 2]\n", ""))
        self.studio_edit(lambda d: find(d["root"], "nmi").update(title="Magic"))
        with self.assertRaises(df.Failed) as e:
            self.build()
        self.assertIn("nmi (node): spec deleted, project", str(e.exception))

    def test_same_slot_taken_on_both_sides_is_a_conflict(self):
        write(self.paths.spec("fx"), self.spec0.replace("{at: [0, 1], uc: C1.2", "{at: [3, 3], uc: C1.2"))
        self.studio_edit(lambda d: find(d["root"], "x4").update(at=[3, 3]))
        with self.assertRaises(df.Failed) as e:
            self.build()
        self.assertIn("the same key on both sides", str(e.exception))

    def test_pack_both_ways(self):
        """The spec's pack is the project's settings.pack: a change on either side reaches the other."""
        write(self.paths.spec("fx"), self.spec0.replace("pack: magicwb", "pack: os4"))
        self.build()
        self.assertEqual(y.load(read(self.paths.project("fx")))["settings"], {"brightness": 50, "pack": "os4"})
        self.studio_edit(lambda d: d["settings"].update(pack="keycap"))
        self.build()
        self.assertIn("\npack: keycap\n", self.spec_text())
        self.studio_edit(lambda d: d["settings"].pop("pack"))  # no pack: magicwb, or the deck's own setting
        self.build()
        self.assertNotIn("pack:", self.spec_text())
        self.assertEqual(y.load(read(self.paths.project("fx")))["settings"], {"brightness": 50})
        write(self.paths.spec("fx"), self.spec_text().replace("settings: {brightness: 50}",
                                                              "settings: {brightness: 50, pack: os4}"))
        self.assertIn("top-level 'pack'", " ".join(str(i) for i in self.pipe.check("fx")))

    def test_shared_menu_edited_in_its_project(self):
        self.studio_edit(lambda d: find(d["root"], "x2").update(title="Double"), "fxspeed", shared=True)
        self.build()
        self.assertIn("x2:     {at: [0, 2], uc: C2.1, action: set.speed, value: {value: 2}, label: X2, title: Double}",
                      self.spec_text("fxspeed", True))

    def test_studio_saved_move(self):
        """A project as Stream Deck Studio saved it after moving `reset` onto `nmi` (they swap) and renaming the
        tools menu's title (tests/deckflow/fx.studio-saved.deck.yaml, captured from the studio's editor routes)."""
        saved = os.path.join(FIXTURE, "fx.studio-saved.deck.yaml")
        if not os.path.exists(saved):
            self.skipTest("no studio capture")
        shutil.copyfile(saved, self.paths.project("fx"))
        self.build()
        text = self.spec_text()
        self.assertIn("reset:  {at: [0, 2], uc: C1.2, action: mach.reset}", text)
        self.assertIn("nmi:    {uc: C1.3, action: mach.nmi, at: [0, 1]}", text)
        self.assertIn("menu tools:\n  title: Tools and saves", text)


if __name__ == "__main__":
    unittest.main()
