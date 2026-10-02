"""The analyzer's peripheral id table must equal the emulator's enum.

src/ttd_format.py PERIPHERAL_ID_NAMES names the device blobs in reports and in
`validate`; an id missing there makes `validate` report every checkpoint of a
recording that carries that device as an error. This test reads
`enum class PeripheralId` from core/src/debugger/ttd/ttdserializable.h, so a
new device id fails here instead of in someone's validation run.

Run from the project root:
    python3 -m unittest discover -s tools/verification/ttd-analyzer/tests -t tools/verification/ttd-analyzer
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from src.ttd_format import PERIPHERAL_ID_NAMES

HEADER = Path(__file__).resolve().parents[4] / "core" / "src" / "debugger" / "ttd" / "ttdserializable.h"


def cpp_peripheral_ids() -> dict:
    text = HEADER.read_text(encoding="utf-8")
    body = text[text.index("enum class PeripheralId"):]
    body = body[body.index("{") + 1:body.index("Count")]
    ids = {}
    for name, value in re.findall(r"^\s*(\w+)\s*=\s*(\d+)\s*,", body, re.MULTILINE):
        ids[int(value)] = name
    return ids


class PeripheralIdTableTest(unittest.TestCase):
    def test_table_equals_the_cpp_enum(self):
        cpp = cpp_peripheral_ids()
        self.assertTrue(cpp, f"no PeripheralId values parsed from {HEADER}")
        self.assertEqual(PERIPHERAL_ID_NAMES, cpp)


if __name__ == "__main__":
    unittest.main()
