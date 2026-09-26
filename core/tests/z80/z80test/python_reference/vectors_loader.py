#!/usr/bin/env python3
"""
Parses core/tests/z80/z80test/z80test_vectors.h (the single source of truth,
shared with the C++ gtest runner) into Python vector dicts. No dependency on
the original tests.asm, which is no longer in the tree.
"""

import re
from pathlib import Path

VECTORS_HEADER = Path(__file__).resolve().parents[1] / "z80test_vectors.h"

_COMMENT_OR_SPACE = r'(?:\s|//[^\n]*\n)*'

_ENTRY_RE = re.compile(
    r'\{"(?P<name>[^"]*)",\s*'
    r'\{(?P<base>[^}]*)\},\s*'
    r'\{(?P<counter>[^}]*)\},\s*'
    r'\{(?P<shifter>[^}]*)\},'
    + _COMMENT_OR_SPACE +
    r'(?P<crc>0x[0-9A-Fa-f]+),\s*(?P<iters>\d+)\}',
    re.MULTILINE,
)


def _parse_bytes(s):
    return [int(x.strip(), 16) for x in s.split(",") if x.strip()]


def load_vectors(path=None):
    path = Path(path) if path else VECTORS_HEADER
    text = path.read_text()
    vectors = []
    for m in _ENTRY_RE.finditer(text):
        vectors.append({
            "name": m.group("name"),
            "base": _parse_bytes(m.group("base")),
            "counter": _parse_bytes(m.group("counter")),
            "shifter": _parse_bytes(m.group("shifter")),
            "expected_crc": int(m.group("crc"), 16),
            "expected_iterations": int(m.group("iters")),
        })
    return vectors


if __name__ == "__main__":
    vecs = load_vectors()
    print(f"Parsed {len(vecs)} vectors from {VECTORS_HEADER}")
    for v in vecs[:5]:
        print(f"  {v['name']!r}: {v['expected_iterations']} iter, CRC=0x{v['expected_crc']:08X}")
