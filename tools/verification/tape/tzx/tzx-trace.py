#!/usr/bin/env python3
"""Stepwise trace of one TZX file's block walk (debug aid for tzx-blockscan).

Prints every block the scanner's skip() table visits — offset, id, name, body
length, end offset and the first body bytes — so a file the scanner rejects
can be traced to the exact block that breaks the walk.

Usage: python3 tools/verification/tape/tzx/tzx-trace.py FILE [FILE...]
Exit status: 0 if every file walks to EOF, 1 otherwise.
"""
import importlib.util
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location(
    'tzx_blockscan', os.path.join(_HERE, 'tzx-blockscan.py'))
mod = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(mod)
BLOCKS, skip = mod.BLOCKS, mod.skip  # noqa: E402


def trace(path):
    """Walk one file; return True when the walk reaches EOF cleanly."""
    with open(path, 'rb') as f:
        data = f.read()
    print(f"{path}: {len(data)} bytes, v{data[8]}.{data[9]}")
    pos = 10
    while pos < len(data):
        bt = data[pos]
        end = skip(data, pos + 1, bt)
        if end is None:
            print(f"  @{pos:#06x}: id {bt:#04x} ({BLOCKS.get(bt, '?')}) -> UNSUPPORTED")
            print(f"           next bytes: {data[pos:pos + 24].hex(' ')}")
            return False
        body = data[pos + 1:min(end, pos + 1 + 16)]
        print(f"  @{pos:#06x}: id {bt:#04x} ({BLOCKS.get(bt, '?'):12s}) body[{end - pos - 1:>6}] end@{end:#07x}: {body.hex(' ')}")
        if end > len(data):
            print("           TRUNCATED")
            return False
        pos = end
    print(f"  EOF @ {len(data):#x} — clean walk")
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for p in sys.argv[1:]:
        if not os.path.isfile(p):
            print(f"{p}: not found")
            return 2
    return 0 if all(trace(p) for p in sys.argv[1:]) else 1


if __name__ == '__main__':
    sys.exit(main())
