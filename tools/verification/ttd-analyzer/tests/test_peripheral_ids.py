"""The analyzer's peripheral id table comes from the emulator's enum.

src/ttd_format.py PERIPHERAL_ID_NAMES names the device blobs in reports and in
`validate`; an id missing there makes `validate` report every checkpoint of a
recording that carries that device as an error. The table is read from
`enum class PeripheralId` in core/src/debugger/ttd/ttdserializable.h at import,
so it cannot drift from the C++ ids; these tests pin the reader and run
`validate` on a recording that carries the newest ids.

Run from the project root:
    python3 -m unittest discover -s tools/verification/ttd-analyzer/tests -t tools/verification/ttd-analyzer
"""

from __future__ import annotations

import unittest
from pathlib import Path

from src.integrity_check import check_integrity
from src.ttd_format import (
    PERIPHERAL_ID_HEADER,
    PERIPHERAL_ID_NAMES,
    parse_file,
    parse_peripheral_id_enum,
)

ROOT = Path(__file__).resolve().parents[4]
SPRINTER_BOOT = ROOT / "testdata" / "machines" / "sprinter" / "ttd" / "boot.ttd"


class PeripheralIdTableTest(unittest.TestCase):
    def test_table_is_read_from_the_cpp_enum(self):
        text = PERIPHERAL_ID_HEADER.read_text(encoding="utf-8")
        body = text[text.index("enum class PeripheralId"):]
        body = body[:body.index("Count")]
        # Every enumerator of the header is in the table, under its own id
        for pid, name in PERIPHERAL_ID_NAMES.items():
            self.assertIn(f"{name} = {pid},", " ".join(body.split()).replace(" ,", ","))
        # Ids are dense from 0 (the registry and the peripheral mask rely on it)
        self.assertEqual(sorted(PERIPHERAL_ID_NAMES), list(range(len(PERIPHERAL_ID_NAMES))))
        # The ids the table was missing when it was a hand-kept copy
        self.assertEqual(PERIPHERAL_ID_NAMES[44], "ProfiXtKbc")
        self.assertEqual(PERIPHERAL_ID_NAMES[45], "EthernetNics")
        self.assertEqual(PERIPHERAL_ID_NAMES[46], "SlotSerial1")
        self.assertEqual(PERIPHERAL_ID_NAMES[47], "SlotSerial2")

    def test_parser_takes_comments_and_refuses_an_enumerator_without_id(self):
        ids = parse_peripheral_id_enum(
            "enum class PeripheralId : uint8_t\n{\n"
            "    A = 0,  // one, with a comma, in the comment\n"
            "    B = 1,\n"
            "    // Future: C, D\n"
            "    Count\n};\n"
        )
        self.assertEqual(ids, {0: "A", 1: "B"})
        with self.assertRaises(ValueError):
            parse_peripheral_id_enum("enum class PeripheralId { A = 0, B, Count };")
        with self.assertRaises(ValueError):
            parse_peripheral_id_enum("enum class Other { A = 0, Count };")

    @unittest.skipUnless(SPRINTER_BOOT.exists(), "testdata Sprinter boot.ttd not present")
    def test_sprinter_recording_has_no_unknown_peripheral(self):
        # The Sprinter recording carries EthernetNics (45); the hand-kept table
        # stopped at 44 and `validate` failed every checkpoint
        dump = parse_file(str(SPRINTER_BOOT))
        ids = set()
        for cp in dump.checkpoints:
            ids.update(cp.peripheral_blobs)
        self.assertIn(45, ids)
        report = check_integrity(dump)
        unknown = [i for i in report.issues if i.code == "unknown_peripheral_id"]
        self.assertEqual(unknown, [])
        self.assertFalse([i for i in report.issues if i.severity == "error"])


if __name__ == "__main__":
    unittest.main()
