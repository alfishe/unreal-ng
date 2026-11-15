"""SCL reading lays the files out as TR-DOS does (found by tools/verification/disk/sector-parity.py, R-08)."""

from __future__ import annotations

import struct

from diskconverter.formats import scl


def build_scl(files: list[tuple[bytes, bytes]]) -> bytes:
    """files: (14-byte descriptor without the location, data of whole sectors)"""
    body = bytearray(b"SINCLAIR" + bytes([len(files)]))
    for descriptor, _ in files:
        body += descriptor
    for _, data in files:
        body += data
    return bytes(body) + struct.pack("<I", sum(body) & 0xFFFFFFFF)


def test_scl_first_file_on_logical_track_1(scratch):
    """Logical track 1 is cylinder 0 head 1 on a double-sided disk; the first free location follows the files"""
    first = b"FIRST   C" + struct.pack("<HHB", 0x8000, 18 * 256, 18)
    second = b"SECOND  C" + struct.pack("<HHB", 0x9000, 256, 1)
    (scratch / "two.scl").write_bytes(build_scl([(first, bytes([0x11]) * 18 * 256), (second, bytes([0x22]) * 256)]))
    disk = scl.read(str(scratch / "two.scl"))

    catalog = disk.sector_bytes(0, 0, 1)
    assert catalog[13:16] == bytes([18, 0, 1])  # sectors, first sector, first track
    assert catalog[16 + 13 : 16 + 16] == bytes([1, 2, 2])  # 16 sectors of track 1 + 2 of track 2
    assert disk.sector_bytes(0, 1, 1) == bytes([0x11]) * 256
    assert disk.sector_bytes(1, 0, 2) == bytes([0x11]) * 256
    assert disk.sector_bytes(1, 0, 3) == bytes([0x22]) * 256
    info = disk.sector_bytes(0, 0, 9)
    assert (info[0xE1], info[0xE2], info[0xE4]) == (3, 2, 2)
    assert info[0xE5] | (info[0xE6] << 8) == (80 * 2 - 1) * 16 - 19


def test_scl_volume_sector_as_trdos_format_leaves_it(scratch):
    """Bytes #EA..#F2 of the volume sector are spaces, as TR-DOS FORMAT and the C++ loader write them"""
    (scratch / "empty.scl").write_bytes(build_scl([]))
    info = scl.read(str(scratch / "empty.scl")).sector_bytes(0, 0, 9)
    assert info[0xE7] == 0x10
    assert info[0xEA:0xF3] == b" " * 9
    assert info[0xF3:0xF5] == bytes(2)
