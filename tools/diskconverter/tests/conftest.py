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


def build_udi(tracks: list[tuple[bytes, list[int]]], cylinders: int, heads: int) -> bytes:
    """A UDI v1.0 file built by hand: per (cylinder, head), the raw MFM byte stream and the offsets of its
    missing-clock bytes (the A1 syncs)."""
    from diskconverter.formats.udi import _crc_udi

    body = bytearray(b"UDI!" + struct.pack("<I", 0) + bytes([0, cylinders - 1, heads - 1, 0]) + struct.pack("<I", 0))
    for raw, clocks in tracks:
        body += bytes([0]) + struct.pack("<H", len(raw)) + raw
        bitmap = bytearray((len(raw) + 7) // 8)
        for offset in clocks:
            bitmap[offset >> 3] |= 1 << (offset & 7)
        body += bitmap
    struct.pack_into("<I", body, 4, len(body))
    return bytes(body) + struct.pack("<I", _crc_udi(bytes(body)))


def crc_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT as the WD1793 computes it on the disk (preset FFFF before the A1 A1 A1)"""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def mfm_field(body: bytes, gap: int = 22) -> tuple[bytes, list[int]]:
    """Gap, sync, A1 A1 A1 and the field body followed by its CRC (big-endian on the disk).
    Returns the bytes and the offsets of the A1 (relative to the start of the returned bytes)."""
    prefix = bytes([0x4E] * gap + [0x00] * 12 + [0xA1] * 3)
    crc = crc_ccitt(bytes([0xA1] * 3) + body)
    clocks = [gap + 12, gap + 13, gap + 14]
    return prefix + body + bytes([crc >> 8, crc & 0xFF]), clocks


def mfm_track(fields: list[tuple[bytes, list[int]]], length: int = 6250) -> tuple[bytes, list[int]]:
    """Concatenate fields into a track padded with 4E"""
    raw = bytearray()
    clocks: list[int] = []
    for data, field_clocks in fields:
        clocks += [len(raw) + c for c in field_clocks]
        raw += data
    raw += bytes([0x4E] * (length - len(raw)))
    return bytes(raw), clocks
