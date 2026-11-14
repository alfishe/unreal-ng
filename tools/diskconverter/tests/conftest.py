"""Shared fixtures for the diskconverter tests (run: python3 -m pytest tools/diskconverter/tests)."""

from __future__ import annotations

import shutil
import struct
import sys
from pathlib import Path

import pytest

TOOL_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = TOOL_DIR.parents[1]
sys.path.insert(0, str(TOOL_DIR))


@pytest.fixture
def scratch(request) -> Path:
    """A per-test directory under the repository's scratch/ (artifacts never go to the system temp)."""
    path = REPO_ROOT / "scratch" / "diskconverter-tests" / request.node.name
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    yield path
    shutil.rmtree(path, ignore_errors=True)


def build_fdi(tracks: list[list[tuple[int, int, int, int, int, bytes]]], cylinders: int, heads: int) -> bytes:
    """An FDI file built by hand (independent of the writer under test).
    tracks: per (cylinder, head) in order, a list of sectors (C, H, R, N, flags, data)."""
    track_headers = bytearray()
    data_area = bytearray()
    for sectors in tracks:
        track_offset = len(data_area)
        track_headers += struct.pack("<IHB", track_offset, 0, len(sectors))
        for c, h, r, n, flags, data in sectors:
            track_headers += struct.pack("<BBBBBH", c, h, r, n, flags, len(data_area) - track_offset)
            data_area += data
    data_offset = 14 + len(track_headers)
    header = b"FDI" + bytes([0]) + struct.pack("<HHHHH", cylinders, heads, 0, data_offset, 0)
    return header + bytes(track_headers) + bytes(data_area)
