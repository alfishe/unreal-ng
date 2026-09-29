"""Tests for the analyzer's port-journal search (src/port_search.py).

Two kinds:
  * hand-built journals - the same cases as the emulator's C++ tests
    (core/tests/debugger/ttd/ttdportsearch_test.cpp);
  * the recorded fixtures in testdata/ttd/port-journals/ - every question the
    emulator answered when they were recorded (expected.json, written by
    scripts/record_port_journal_fixtures.py) must get the same answer here.

Run from the project root:
    python3 -m unittest discover -s tools/verification/ttd-analyzer/tests -t tools/verification/ttd-analyzer
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from src.port_search import (PortQueryError, apply_option, build_query, search, search_dump)
from src.ttd_format import PortJournal, PortRecord, parse_file

PROJECT_ROOT = Path(__file__).resolve().parents[4]
FIXTURES = PROJECT_ROOT / "testdata/ttd/port-journals"


class Journals:
    """IN and OUT journals filled access by access, time advancing 10 T each."""

    def __init__(self):
        self.reads = PortJournal(records=[], cursors=[])
        self.writes = PortJournal(records=[], cursors=[])
        self.frame, self.t = 5, 100

    def inp(self, port, value, pc=0x8000):
        self.reads.records.append(PortRecord(self.frame, self.t, port, pc, value))
        self.t += 10

    def out(self, port, value, pc=0x9000):
        self.writes.records.append(PortRecord(self.frame, self.t, port, pc, value))
        self.t += 10

    def next_frame(self):
        self.frame, self.t = self.frame + 1, 5

    def find(self, event, arg=None, **options):
        q = build_query(event, arg)
        for name, value in options.items():
            apply_option(q, name, value)
        return search(self.reads, self.writes, q)


def values(result):
    return [h.record.value for h in result.hits]


class HandBuiltJournals(unittest.TestCase):

    def test_key_reports_each_press_once_at_the_read_that_saw_it(self):
        j = Journals()
        j.inp(0xFDFE, 0xFF)
        j.inp(0xFDFE, 0xFE)          # A down  <- hit
        j.inp(0xFDFE, 0xFE)          # held
        j.inp(0xFEFE, 0xFE)          # CAPS SHIFT's row
        j.inp(0xFDFE, 0xFF)          # released
        j.next_frame()
        j.inp(0xFDFE, 0xFE, 0x8124)  # A down again  <- hit
        r = j.find("key", "a")
        self.assertEqual([h.index for h in r.hits], [1, 5])
        self.assertEqual((r.hits[0].record.frame, r.hits[0].record.t_in_frame), (5, 110))
        self.assertEqual(r.hits[1].record.pc, 0x8124)

    def test_a_read_of_all_rows_is_some_key_not_a_particular_one(self):
        j = Journals()
        j.inp(0x00FE, 0xFF)
        j.inp(0x00FE, 0xFE)
        self.assertEqual(j.find("key", "a").hits, [])
        self.assertEqual(len(j.find("key").hits), 1)

    def test_key_names_and_aliases(self):
        j = Journals()
        j.inp(0xFEFE, 0xFE)  # CAPS SHIFT
        j.inp(0xFBFE, 0xFE)  # Q
        self.assertEqual(len(j.find("key", "cs").hits), 1)
        self.assertEqual(len(j.find("key", "Q").hits), 1)
        with self.assertRaises(PortQueryError):
            build_query("key", "left")

    def test_ear_reports_every_change(self):
        j = Journals()
        for v in (0xFF, 0xFF, 0xBF, 0xBF, 0xBE, 0xFF):
            j.inp(0x7FFE, v)
        self.assertEqual([h.index for h in j.find("ear").hits], [2, 5])

    def test_border_changes_across_high_bytes(self):
        # The ULA decodes A0 alone; Dizzy X alternates OUT #10FE and #00FE
        j = Journals()
        j.out(0x10FE, 0x11)
        j.out(0x00FE, 0x01)
        j.out(0x10FE, 0x13)
        self.assertEqual(values(j.find("beeper")), [0x01, 0x13])
        self.assertEqual(values(j.find("border")), [0x13])

    def test_ay_writes_follow_the_selection(self):
        j = Journals()
        for port, v in ((0xFFFD, 7), (0xBFFD, 0x38), (0xFFFD, 8), (0xBFFD, 0x0F), (0xFFFD, 0xFE), (0xBFFD, 0x0E),
                        (0xFFFD, 7), (0xBFFD, 0x3F)):
            j.out(port, v)
        r = j.find("ay-write", "7")
        self.assertEqual(values(r), [0x38, 0x3F])
        self.assertEqual({h.ay_register for h in r.hits}, {7})
        self.assertEqual(values(j.find("ay-write", "8")), [0x0F, 0x0E])
        self.assertEqual(len(j.find("ay-select").hits), 4)

    def test_ay_reads_follow_the_selection_in_time(self):
        j = Journals()
        j.out(0xFFFD, 14)
        j.inp(0xFFFD, 0xAA)
        j.out(0xFFFD, 3)
        j.inp(0xFFFD, 0x05)
        j.out(0xFFFD, 14)
        j.inp(0xFFFD, 0xAB)
        self.assertEqual(values(j.find("ay-read", "0x0E")), [0xAA, 0xAB])

    def test_limit_window_and_newest_first(self):
        j = Journals()
        for f in range(5):
            j.out(0x00FE, f)
            j.next_frame()
        r = j.find("border", limit=2)
        self.assertEqual((values(r), r.truncated), ([1, 2], True))
        r = j.find("border", limit=2, newest=True)
        self.assertEqual(values(r), [4, 3])
        self.assertEqual(values(j.find("border", **{"from": "7", "to": "8:4294967295"})), [2, 3])

    def test_bad_queries(self):
        for event, arg in (("nosuchevent", None), ("ear", "x"), ("ay-read", "16")):
            with self.assertRaises(PortQueryError):
                build_query(event, arg)
        q = build_query("in")
        for name, value in (("limit", "0"), ("value", "256"), ("trigger", "sometimes"), ("colour", "red")):
            with self.assertRaises(PortQueryError):
                apply_option(q, name, value)


@unittest.skipUnless((FIXTURES / "expected.json").exists(), "port-journal fixtures not recorded")
class RecordedFixtures(unittest.TestCase):
    """The analyzer answers every question exactly as the emulator did."""

    def test_every_answer_matches_the_emulator(self):
        expected = json.loads((FIXTURES / "expected.json").read_text())
        checked = 0
        for fixture in expected["fixtures"]:
            dump = parse_file(str(FIXTURES / fixture["file"]))
            for item in fixture["answers"]:
                q = dict(item["query"])
                event, arg = q.pop("event"), q.pop("arg", None)
                with self.subTest(file=fixture["file"], query=item["query"]):
                    r = search_dump(dump, event, arg, **q)
                    a = item["answer"]
                    self.assertEqual(r.direction, a["direction"])
                    self.assertEqual(len(r.hits), a["count"])
                    self.assertEqual(r.truncated, a["truncated"])
                    self.assertEqual(r.scanned, a["scanned"])
                    self.assertEqual([h.as_dict() for h in r.hits], a["hits"])
                checked += 1
        self.assertGreaterEqual(checked, 20)

    def test_a_file_without_journals_is_refused(self):
        dump = parse_file(str(FIXTURES / "dizzyx.ttd"))
        dump.port_reads = dump.port_writes = None
        with self.assertRaises(PortQueryError):
            search_dump(dump, "key")


if __name__ == "__main__":
    unittest.main()
