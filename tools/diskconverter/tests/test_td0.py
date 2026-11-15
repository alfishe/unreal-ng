"""TD0 reading: a sector with N > 3 (uPD765: really more than 1K) is skipped with a warning (R-09 / DC-13)."""

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
            body += struct.pack("<H", len(data) + 1) + bytes([td0.ENCODING_RAW]) + data
    body.append(td0.END_OF_IMAGE)
    return bytes(header + body)


def test_r09_td0_size_code_4_sector_skipped_with_warning(scratch, capsys):
    sectors = [
        (0, 0, 1, 1, 0, bytes([1]) * 256),
        (0, 0, 2, 4, 0, bytes([2]) * 2048),  # N=4: 2K on a uPD765, no WD1793 equivalent
        (0, 0, 3, 1, 0, bytes([3]) * 256),
    ]
    path = scratch / "n4.td0"
    path.write_bytes(build_td0([(0, 0, sectors), (1, 0, [(1, 0, 1, 2, 0, bytes([9]) * 512)])]))

    disk = td0.read(str(path))
    out = capsys.readouterr().out
    assert "sector=2: size code 4 has no WD1793 equivalent" in out
    assert "header CRC mismatch" not in out

    track0 = disk.get_track(0, 0).sectors
    assert [(s.number, s.size_code) for s in track0] == [(1, 1), (3, 1)]
    assert track0[0].data == bytes([1]) * 256
    assert track0[1].data == bytes([3]) * 256  # the data after the skipped block is still in step
    track1 = disk.get_track(1, 0).sectors
    assert [(s.number, s.size_code, s.data) for s in track1] == [(1, 2, bytes([9]) * 512)]
    assert disk.cylinders == 2
