"""F-4 (test-strategy.md): damaged branched files never make the tools raise anything but TtdError, and the
verdict is the one the format allows.

Seeded truncations and single-bit flips of every branched fixture (a fixed seed per file: a failure reproduces).
Every damaged copy:

- reads (open + branch-aware read) or raises TtdError, nothing else; validate returns problems, never raises;
- is reported damaged (validate finds a problem) - a flipped bit always breaks a CRC, a cut file has no trailer;
- when it reads, each line it keeps is a line of the original with the same parent and fork, and its checkpoints
  are a prefix of the original's with the same content CRCs (a damaged part ends its line; nothing loads changed);
- ttdtool validate exits 1, ttdtool info and tree exit 0 or 1, without a traceback.

The C++ reader's verdicts on fixed damaged copies are compared in test_reader.py (expected.json "damaged").
Bounded to a few seconds: 6 copies per file by default, 24 in the long tier (UNREAL_LONG_TESTS=1, as core-tests).
"""

import contextlib
import io
import os
import random

import pytest

import ttdtool
from ttdlib import TtdError
from ttdlib.session import content_crcs, open_branched, read_branched, validate_branched

FILES = ["two-branches.ttd", "deep-lineage.ttd", "mid-frame.ttd", "many-short.ttd"]
LONG = os.environ.get("UNREAL_LONG_TESTS") == "1"
FLIPS = 16 if LONG else 4
CUTS = 8 if LONG else 2


def lines(data: bytes):
    s = read_branched(open_branched(data))
    return {l.id: ((l.parent, l.fork_frame, l.fork_t), content_crcs(s, l.id)) for l in s.lines.values()}


def damaged_copies(data: bytes, seed: int):
    rng = random.Random(seed)
    for _ in range(FLIPS):
        at = rng.randrange(len(data))
        copy = bytearray(data)
        copy[at] ^= 1 << rng.randrange(8)
        yield f"flip at {at}", bytes(copy)
    for _ in range(CUTS):
        at = rng.randrange(1, len(data))
        yield f"cut at {at}", data[:at]


@pytest.mark.parametrize("name", FILES)
def test_damaged_copies(fixtures, name, tmp_path):
    data = (fixtures / name).read_bytes()
    original = lines(data)
    for what, copy in damaged_copies(data, seed=sum(name.encode())):
        problems = validate_branched(copy)
        assert problems, f"{name}, {what}: damage not found"
        try:
            got = lines(copy)
        except TtdError:
            got = None
        if got is not None:
            for line, (fork, crcs) in got.items():
                assert line in original, f"{name}, {what}: line {line} is not the original's"
                want_fork, want_crcs = original[line]
                assert fork == want_fork, f"{name}, {what}: line {line} forks elsewhere"
                assert crcs == want_crcs[:len(crcs)], f"{name}, {what}: line {line} loads changed"
        path = tmp_path / "copy.ttd"
        path.write_bytes(copy)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            assert ttdtool.main(["validate", str(path)]) == 1, f"{name}, {what}"
            assert ttdtool.main(["info", str(path)]) in (0, 1), f"{name}, {what}"
            assert ttdtool.main(["tree", str(path)]) in (0, 1), f"{name}, {what}"


def test_damaged_index_file(fixtures):
    """branches.tdx cut or flipped anywhere: read_index keeps the complete records before the damage or raises
    TtdError; the records it keeps are a prefix of the intact file's."""
    from ttdlib import folder as fld
    data = (fixtures / "folder" / fld.INDEX_FILE).read_bytes()
    whole = fld.read_index(data)
    for what, copy in damaged_copies(data, seed=7):
        try:
            got = fld.read_index(copy)
        except TtdError:
            continue
        assert got.records <= whole.records, what
        assert got.segments == whole.segments[:len(got.segments)], what
        assert got.creates == whole.creates[:len(got.creates)], what
