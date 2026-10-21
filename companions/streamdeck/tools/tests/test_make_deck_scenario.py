#!/usr/bin/env python3
"""Tests of the scenario generator (tools/make_deck_scenario.py) on its fixture spec: the checked-in output is the
generator's, the output is deterministic, the spec's mistakes stop it.

  python3 companions/streamdeck/tools/tests/test_make_deck_scenario.py
"""
import copy
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import make_deck_scenario as gen  # noqa: E402

SPEC = os.path.join(HERE, "fixture.spec.json")
CATALOGUE = os.path.join(HERE, "fixture.catalogue.json")
EXPECTED = os.path.join(HERE, "fixture.deck.json")


class GeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = gen.load_json(SPEC)
        cls.catalogue = gen.load_json(CATALOGUE)
        cls.doc = gen.generate(cls.catalogue, copy.deepcopy(cls.spec))
        cls.text = gen.dumps(cls.doc)

    def test_checked_in_output_is_the_generators(self):
        with open(EXPECTED, encoding="utf-8") as f:
            self.assertEqual(f.read(), self.text, "regenerate tests/fixture.deck.json")
        self.assertEqual(gen.main([SPEC, "--catalogue", CATALOGUE, "--out", EXPECTED, "--check"]), 0)

    def test_deterministic(self):
        again = gen.dumps(gen.generate(copy.deepcopy(self.catalogue), copy.deepcopy(self.spec)))
        self.assertEqual(again, self.text)
        with tempfile.TemporaryDirectory() as t:
            out = os.path.join(t, "a.deck.json")
            self.assertEqual(gen.main([SPEC, "--catalogue", CATALOGUE, "--out", out]), 0)
            with open(out, encoding="utf-8") as f:
                self.assertEqual(f.read(), self.text)
        for local in ("/Users/", "/Volumes/", "C:\\\\", "/home/"):
            self.assertNotIn(local, self.text)

    def test_v2_document(self):
        d = self.doc
        self.assertEqual((d["format"], d["version"]), ("streamdeck-scenario", 2))
        root = d["root"]
        self.assertEqual(root["context"], "play")
        run = root["items"][0]
        self.assertEqual(run["do"], {"kind": "action", "id": "run.pause_toggle"})
        self.assertEqual(run["state"], "run.state.paused")
        self.assertTrue(run["face"]["image"].endswith("/pause.png") and run["face_on"]["image"].endswith("/play.png"))
        turbo = root["items"][1]
        self.assertEqual(turbo["at"], [0, 1])  # placed after the given slot
        self.assertEqual(turbo["do"], {"kind": "action", "id": "run.turbo_hold", "hold": True})
        soft = root["items"][2]
        self.assertEqual(soft["nav"], {"back": [0, 0]})
        bench = soft["items"][0]
        self.assertEqual(bench["at"], [0, 1])  # the back key's slot stays free
        self.assertEqual(bench["do"]["kind"], "multi")
        self.assertEqual(bench["do"]["steps"][0]["value"], {"model": "PENTAGON", "ram_kb": 128, "replace": True})
        self.assertEqual(root["dials"][0]["turn"]["id"], "ttd.seek_by")
        self.assertEqual(bench["face"], {"label": "BENCH", "icon": "record", "bg": gen.BG, "fg": gen.FG})  # built-in

    def expect_error(self, edit, text):
        spec = copy.deepcopy(self.spec)
        edit(spec)
        with self.assertRaises(gen.SpecError) as e:
            gen.generate(self.catalogue, spec)
        self.assertIn(text, str(e.exception))

    def test_mistakes_stop_it(self):
        items = lambda s: s["root"]["items"]  # noqa: E731
        self.expect_error(lambda s: items(s)[0].update(action="run.nothing"), "not in the catalogue")
        self.expect_error(lambda s: items(s)[0].update(icon="nowhere"), "icon 'nowhere'")
        self.expect_error(lambda s: items(s)[2]["items"][0]["steps"][1]["value"].update(path="$data/missing.z80"),
                          "not in data/")
        self.expect_error(lambda s: items(s).append({"id": "sh", "kind": "shell", "command": "ls"}), "ACC-5")
        self.expect_error(lambda s: items(s)[2]["items"][0].update(at=[0, 0]), "back key")
        self.expect_error(lambda s: items(s).append({"id": "run", "action": "run.pause_toggle"}), "duplicate ids")
        self.expect_error(lambda s: items(s).extend({"id": f"k{i}", "action": "run.pause_toggle"} for i in range(40)),
                          "more keys than")
        self.assertEqual(gen.main([SPEC, "--catalogue", CATALOGUE, "--out", EXPECTED + ".nope", "--check"]), 1)


if __name__ == "__main__":
    unittest.main()
