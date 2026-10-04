"""Conformance of the schema-2 reader (src/ttdcontainer.py) with the C++ writer.

testdata/ttd/v2/ holds files the C++ writer produced
(TTDSessionFile_Test.DISABLED_WriteAnalyzerFixtures) and expected.json, the
counts the C++ reader finds in them. Here the Python reader must find the same
counts, validate every file (CRCs, parts, every version's content, dependency
lists), and report damage the way the C++ reader does.

Run from the project root:
    python3 -m unittest discover -s tools/verification/ttd-analyzer/tests -t tools/verification/ttd-analyzer
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from src.ttdcontainer import (RECORD_HEADER, TtdContainerError, open_container, read_record, read_session,
                              reachable, validate)

PROJECT_ROOT = Path(__file__).resolve().parents[4]
FIXTURES = PROJECT_ROOT / "testdata/ttd/v2"


class FixtureTest(unittest.TestCase):
    def setUp(self):
        self.expected = json.loads((FIXTURES / "expected.json").read_text())

    def test_counts_match_the_cpp_reader(self):
        for name, want in self.expected.items():
            with self.subTest(name):
                c = open_container((FIXTURES / name).read_bytes())
                s = read_session(c)
                self.assertEqual(len(s.checkpoints), want["checkpoints"])
                self.assertEqual(len(s.versions), want["versions"])
                self.assertEqual(len(c.parts), want["parts"])
                self.assertEqual(s.events, want["events"])
                self.assertEqual(s.bus_reads, want["bus_reads"])
                self.assertEqual(s.bus_writes, want["bus_writes"])
                self.assertEqual(c.finalized, want["finalized"])
                self.assertEqual(s.converted_from_v1, want["converted_from_v1"])

    def test_every_file_validates(self):
        for name in self.expected:
            with self.subTest(name):
                problems = validate((FIXTURES / name).read_bytes())
                if name == "synthetic-unfinished.ttd":
                    expected = ("not finalized", "incomplete", "scanning")
                    self.assertTrue(problems and all(any(e in p for e in expected) for p in problems), problems)
                else:
                    self.assertEqual(problems, [])

    def test_unknown_ancillary_stream_is_skipped(self):
        c = open_container((FIXTURES / "synthetic-ancillary.ttd").read_bytes())
        self.assertTrue(any("screenshot" in n and "skipped" in n for n in c.notes), c.notes)

    def test_segments_restart_at_each_baseline(self):
        s = read_session(open_container((FIXTURES / "synthetic.ttd").read_bytes()))
        baselines = [cp.frame for cp in s.checkpoints if cp.baseline]
        self.assertEqual(baselines, [0, 20, 40])
        for cp in s.checkpoints:
            if cp.baseline and cp.frame > 0:
                for _, _, number in cp.changes:
                    self.assertEqual(s.versions[number].base, -1, "a baseline stores pieces whole")


class DamageTest(unittest.TestCase):
    def test_a_flipped_payload_byte_is_a_hole(self):
        data = bytearray((FIXTURES / "synthetic.ttd").read_bytes())
        c = open_container(bytes(data))
        rec = c.parts[1].records[0]
        data[rec.offset + RECORD_HEADER + rec.stored_size // 2] ^= 0x20
        damaged = open_container(bytes(data))
        with self.assertRaises(ValueError):
            read_record(damaged, damaged.parts[1].records[0])
        problems = validate(bytes(data))
        self.assertTrue(any("CRC" in p for p in problems), problems)

    def test_damage_follows_the_dependencies(self):
        c = open_container((FIXTURES / "synthetic.ttd").read_bytes())
        c.parts[0].damaged = True
        # Parts 1 and 2 hold frames of the first segment (0-19, 8 per part): they need part 0
        self.assertFalse(reachable(c, 1))
        self.assertFalse(reachable(c, 2))

    def test_nothing_after_a_baseline_needs_an_earlier_part(self):
        c = open_container((FIXTURES / "synthetic.ttd").read_bytes())
        s = read_session(c)
        baseline_part = next(cp.part for cp in s.checkpoints if cp.baseline and cp.frame == 20)
        for part in c.parts:
            if part.first_frame >= 24 and part.first_frame < 40:
                self.assertTrue(all(d >= baseline_part for d in part.dependencies),
                                f"part {part.index} lists {part.dependencies}")

    def test_not_a_session_file(self):
        with self.assertRaises(TtdContainerError):
            open_container(b"\0" * 100)
        data = bytearray((FIXTURES / "synthetic.ttd").read_bytes())
        data[4] = 1
        with self.assertRaisesRegex(TtdContainerError, "schema v1"):
            open_container(bytes(data))


if __name__ == "__main__":
    unittest.main()
