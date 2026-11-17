"""TD0 reading (R-09 / DC-13): a sector with N > 3 (uPD765: more than 1K) is kept with its size code masked to
N & 3 in the ID field and a warning, as LoaderTD0::buildImage does (see the parity report: FDI keeps the raw N)."""

from __future__ import annotations

import struct

from diskconverter.formats import td0


def build_td0(tracks: list[tuple[int, int, list[tuple[int, int, int, int, int, bytes]]]]) -> bytes:
    """An uncompressed ("TD") TD0 built by hand: tracks of (cylinder, head, sectors), each sector
    (C, H, R, N, flags, data) stored raw (encoding 0). Header CRC is Teledisk's own, as the reader checks."""
    header = bytearray(b"TD" + bytes([0, 0, 0x15, 0x02, 0x03, 0x00, 0x00, 0x01]))
    header += struct.pack("<H", td0._crc16(bytes(header)))
    body = bytearray()
    for cylinder, head, sectors in tracks:
        body += bytes([len(sectors), cylinder, head, 0])
        for c, h, r, n, flags, data in sectors:
            body += bytes([c, h, r, n, flags, 0])
            if flags & td0.FLAGS_NO_DATA_BLOCK:
                continue
            body += struct.pack("<H", len(data) + 1) + bytes([td0.ENCODING_RAW]) + data
    body.append(td0.END_OF_IMAGE)
    return bytes(header + body)


def test_r09_td0_size_code_4_masked_with_warning(scratch):
    sectors = [
        (0, 0, 1, 1, 0, bytes([1]) * 256),
        (0, 0, 2, 4, 0, bytes([2]) * 2048),  # N=4: 2K on a uPD765, no WD1793 equivalent
        (0, 0, 3, 1, 0, bytes([3]) * 256),
    ]
    path = scratch / "n4.td0"
    path.write_bytes(build_td0([(0, 0, sectors), (1, 0, [(1, 0, 1, 2, 0, bytes([9]) * 512)])]))

    disk = td0.read(str(path))
    assert "TD0 cylinder 0 head 0 sector 2: size code 4 masked to 0 (WD1793 uses 2 bits)" in disk.warnings
    assert not any(w.startswith("TD0 header CRC mismatch") for w in disk.warnings)

    track0 = disk.get_track(0, 0).sectors
    assert [(s.number, s.size_code) for s in track0] == [(1, 1), (2, 0), (3, 1)]
    assert track0[0].data == bytes([1]) * 256
    assert track0[1].data == bytes([2]) * 128  # the first 128 bytes of the 2K block
    assert track0[2].data == bytes([3]) * 256  # the data after the long block is still in step
    track1 = disk.get_track(1, 0).sectors
    assert [(s.number, s.size_code, s.data) for s in track1] == [(1, 2, bytes([9]) * 512)]
    assert disk.cylinders == 2


def test_td0_round_trip_keeps_flags(scratch):
    """A deleted sector, a bad data CRC, an ID-only sector and an FM track survive TD0 -> TD0"""
    sectors = [
        (0, 0, 1, 1, 0, bytes(range(256))),
        (0, 0, 2, 1, td0.FLAG_DELETED, bytes([0xE5]) * 256),
        (0, 0, 3, 1, td0.FLAG_DATA_CRC_ERROR, bytes([7]) * 256),
        (0, 0, 4, 1, td0.FLAG_NO_DATA, b""),
    ]
    data = build_td0([(0, 0, sectors)])
    disk = td0.parse(data)
    out, warnings = td0.serialize(disk)
    again = td0.parse(out)
    got = [(s.number, s.deleted, s.crc_valid, s.no_data) for s in again.get_track(0, 0).sectors]
    assert got == [(1, False, True, False), (2, True, True, False), (3, False, False, False), (4, False, True, True)]
    assert again.get_track(0, 0).sectors[0].data == bytes(range(256))
    assert again.warnings == []  # the writer's CRCs are right
