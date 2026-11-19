"""Teledisk advanced ("td") decompression: the decoded stream parses as a TD0 body and its sectors equal
the C++ loader's (zxdisk dump)."""

from __future__ import annotations

import functools
import json
import os
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

from diskconverter import td0lzh

REPO_ROOT = Path(__file__).resolve().parents[3]
ADV_SAMPLE = REPO_ROOT / "testdata" / "loaders" / "td0" / "trdos-sample-adv.td0"
PLAIN_SAMPLE = REPO_ROOT / "testdata" / "loaders" / "td0" / "trdos-sample.td0"
ZXDISK = Path(os.environ.get("ZXDISK") or REPO_ROOT / "cmake-build-agent-release" / "bin" / "zxdisk")


def _crc16(data: bytes) -> int:
    """Teledisk CRC (polynomial A097, preset 0)"""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0xA097) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _decode_block(encoding: int, payload: bytes, size: int) -> bytes:
    """Sector data encodings 0 (raw), 1 (repeated 2-byte pattern), 2 (RLE runs)"""
    out = bytearray()
    if encoding == 0:
        out += payload[:size]
    elif encoding == 1:
        count = struct.unpack_from("<H", payload)[0]
        out += payload[2:4] * count
    elif encoding == 2:
        p = 0
        while p + 2 <= len(payload) and len(out) < size:
            code, count = payload[p], payload[p + 1]
            p += 2
            if code == 0:
                out += payload[p : p + count]
                p += count
            else:
                out += payload[p : p + code * 2] * count
                p += code * 2
    else:
        raise AssertionError(f"unknown sector encoding {encoding}")
    assert len(out) >= size, f"encoding {encoding} produced {len(out)} of {size} bytes"
    return bytes(out[:size])


def parse_td0_body(body: bytes, has_comment: bool) -> list[tuple[int, int, list[tuple[int, int, bytes | None]]]]:
    """Minimal TD0 body parser: [(cylinder, head, [(R, N, data or None)])] in file order, sectors the C++
    loader skips (no ID field, size code > 6) left out.
    Checks the comment CRC, track header CRCs, sector data CRCs and the 0xFF end marker."""
    offset = 0
    if has_comment:
        crc, length = struct.unpack_from("<HH", body, 0)
        assert _crc16(body[2 : 10 + length]) == crc, "comment block CRC"
        offset = 10 + length
    tracks = []
    while True:
        assert offset < len(body), "end-of-image marker missing"
        if body[offset] == 0xFF:
            break
        count, cylinder, head, crc = body[offset : offset + 4]
        assert _crc16(body[offset : offset + 3]) & 0xFF == crc, f"track header CRC at {offset}"
        offset += 4
        sectors = []
        for _ in range(count):
            _c, _h, r, n, flags, crc = body[offset : offset + 6]
            offset += 6
            data = None
            if not flags & 0x30:
                block_length = struct.unpack_from("<H", body, offset)[0]
                block = body[offset + 2 : offset + 2 + block_length]
                assert len(block) == block_length, "data block truncated"
                offset += 2 + block_length
                if n > 6:
                    continue  # the C++ loader skips invalid size codes
                data = _decode_block(block[0], block[1:], 128 << n)
                assert _crc16(data) & 0xFF == crc, f"sector data CRC (cylinder {cylinder} head {head} sector {r})"
            if flags & 0x40:
                continue  # data without an ID field: skipped by the C++ loader
            sectors.append((r, n, data))
        tracks.append((cylinder, head & 0x7F, sectors))
    return tracks


def zxdisk_sectors(image: Path) -> dict[tuple[int, int], list[tuple[int, int, bytes | None]]]:
    """Sectors of the C++ load: per (cylinder, head), [(R, N, data or None)] in track order"""
    dump = json.loads(subprocess.run([str(ZXDISK), "dump", str(image)], check=True, capture_output=True).stdout)
    result = {}
    for track in dump["tracks"]:
        raw = bytes.fromhex(track["raw"])
        result[(track["c"], track["h"])] = [
            (s["r"], s["n"], raw[s["data"] : s["data"] + s["size"]] if s["has_data"] else None) for s in track["sectors"]
        ]
    return result


@functools.lru_cache(maxsize=1)
def _decoded_sample() -> tuple[bytes, bytes]:
    image = ADV_SAMPLE.read_bytes()
    assert image[:2] == b"td"
    return image[:12], td0lzh.decompress(image[12:])


def test_adv_sample_decodes_to_a_td0_body():
    header, body = _decoded_sample()
    assert _crc16(header[:10]) == struct.unpack_from("<H", header, 10)[0]
    tracks = parse_td0_body(body, bool(header[7] & 0x80))
    assert len(tracks) == 160
    assert all(len(sectors) == 16 for _, _, sectors in tracks)


def test_adv_sample_equals_the_uncompressed_sample():
    _, body = _decoded_sample()
    plain = PLAIN_SAMPLE.read_bytes()[12:]
    assert body[: len(plain)] == plain
    # Trailing junk from the zero bits that finish the last symbol is cut by the checked variant
    assert body[: td0lzh.body_length(body, has_comment=False)] == plain


@pytest.mark.skipif(not ZXDISK.exists(), reason="zxdisk binary not built")
def test_adv_sample_sectors_equal_cpp_load():
    header, body = _decoded_sample()
    reference = zxdisk_sectors(ADV_SAMPLE)
    tracks = parse_td0_body(body, bool(header[7] & 0x80))
    assert len(tracks) == len(reference)
    for cylinder, head, sectors in tracks:
        assert reference[(cylinder, head)] == sectors, f"cylinder {cylinder} head {head}"


def test_empty_stream_decodes_to_nothing():
    assert td0lzh.decompress(b"") == b""


def test_truncated_stream_rejected_by_checked_decode():
    image = ADV_SAMPLE.read_bytes()
    with pytest.raises(ValueError, match="truncated"):
        td0lzh.decompress_checked(image[12:5000], has_comment=False)


def test_max_out_caps_the_output():
    _, body = _decoded_sample()
    assert td0lzh.decompress(ADV_SAMPLE.read_bytes()[12:], max_out=1000) == body[:1000]
