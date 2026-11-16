"""DSK / EDSK, raw PC floppy, MGT and PRO: the ports of unreal-disk's loader_dsk / loader_rawpc / loader_mgt /
loader_pro, checked on hand-built images and against zxdisk (skipped when the binary is not built)."""

from __future__ import annotations

import importlib.util
import json
import struct

import pytest

from conftest import REPO_ROOT
from diskconverter import formats
from diskconverter.disk_image import DiskConversionError
from diskconverter.formats import dsk, mgt, pro, rawpc

_TOOL = REPO_ROOT / "tools" / "verification" / "disk" / "parity.py"
_spec = importlib.util.spec_from_file_location("disk_parity_dsk", _TOOL)
parity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(parity)
ZXDISK = parity.find_zxdisk(None)

FIXTURES = [
    "testdata/loaders/dsk/edsk-protected.dsk",
    "testdata/loaders/dsk/plus3-blank.dsk",
    "testdata/machines/plus3/cpm/zx-cpm-2.2.dsk",
    "testdata/loaders/mgt/synthetic.img",
    "testdata/loaders/mgt/synthetic.mgt",
    "testdata/loaders/pro/ReadFile.pro",
    "testdata/machines/sprinter/dss_1_62_92.img",
]


def build_edsk(tracks: list[list[tuple[int, int, int, int, int, int, bytes]]], cylinders: int, sides: int,
               creator: bytes = b"test          ") -> bytes:
    """An Extended DSK built by hand. tracks: per (cylinder, side), sectors (C, H, R, N, ST1, ST2, stored bytes[,
    stored length])."""
    header = bytearray(256)
    header[0:34] = b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"
    header[0x22:0x30] = creator
    header[0x30] = cylinders
    header[0x31] = sides
    blocks = bytearray()
    for index, sectors in enumerate(tracks):
        info = bytearray(256)
        info[0:12] = b"Track-Info\r\n"
        info[0x10] = index // sides
        info[0x11] = index % sides
        info[0x12], info[0x13], info[0x14], info[0x15], info[0x16], info[0x17] = 1, 2, 2, len(sectors), 0x2A, 0xE5
        body = bytearray()
        for i, (c, h, r, n, st1, st2, data, *stored) in enumerate(sectors):
            length = stored[0] if stored else len(data)  # a stored length other than the bytes (CPDRead slots)
            info[0x18 + i * 8:0x20 + i * 8] = bytes((c, h, r, n, st1, st2)) + struct.pack("<H", length)
            body += data
        block = info + body
        block += bytes(-len(block) % 256)
        header[0x34 + index] = len(block) // 256
        blocks += block
    return bytes(header + blocks)


def _sector_data(seed: int, size: int = 512) -> bytes:
    return bytes((seed * 7 + i) & 0xFF for i in range(size))


def test_edsk_status_bits_map_to_the_model():
    weak_a = _sector_data(3)
    weak_b = bytes(v ^ 0xFF if 10 <= i < 20 else v for i, v in enumerate(weak_a))
    sectors = [
        (0, 0, 1, 2, 0, 0, _sector_data(1)),
        (0, 0, 2, 2, 0x20, 0x20, _sector_data(2)),  # data CRC error
        (0, 0, 3, 2, 0, 0, weak_a + weak_b),        # weak sector: two copies
        (0, 0, 4, 2, 0, 0x40, _sector_data(4)),     # deleted data mark
        (0, 0, 5, 2, 0x20, 0, _sector_data(5)),     # ID CRC error
        (0, 0, 6, 2, 0x04, 0x01, b""),              # ID field only
    ]
    disk = dsk.parse(build_edsk([sectors], 1, 1))
    track = disk.get_track(0, 0)
    view = {s.number: s for s in track.index}
    assert len(view) == 6
    assert view[1].is_data_crc_valid() and view[1].data_bytes() == _sector_data(1)
    assert not view[2].is_data_crc_valid()
    assert [track.weak_byte(view[3].data + i) for i in range(9, 21)] == [False] + [True] * 10 + [False]
    assert view[4].deleted
    assert not view[5].is_id_crc_valid() and view[5].is_data_crc_valid()
    assert not view[6].has_data

    out, warnings = dsk.serialize(disk)
    assert warnings == []
    again = dsk.parse(out)
    assert bytes(again.get_track(0, 0).raw) == bytes(track.raw)
    assert again.get_track(0, 0).weak_bitmap() == track.weak_bitmap()


def test_dsk_refusals_carry_the_loader_message():
    with pytest.raises(DiskConversionError, match="DSK declares 0 tracks"):
        dsk.parse(build_edsk([], 0, 1))
    truncated = build_edsk([[(0, 0, 1, 2, 0, 0, _sector_data(1))]], 1, 1)[:600]
    with pytest.raises(DiskConversionError, match="sector data truncated"):
        dsk.parse(truncated)


def test_raw_pc_round_trip_and_refusal(scratch):
    image = bytes((i * 13 + (i >> 9)) & 0xFF for i in range(rawpc.IMAGE_SIZE_DD))
    disk = rawpc.parse(image)
    assert (disk.cylinders, disk.heads, disk.get_track(0, 0).sector_count()) == (80, 2, 9)
    assert disk.get_track(0, 0).length == 6250
    assert rawpc.serialize(disk)[0] == image
    path = scratch / "pc.img"
    path.write_bytes(image)
    assert formats.probe(str(path)) == "rawpc"
    # a 720 KB disk is not +D: .img goes to the raw PC writer, .mgt refuses
    assert formats.serialize(disk, str(scratch / "out.img"))[0] == image
    with pytest.raises(DiskConversionError, match="MGT save refused: Track cylinder 0 side 0 has 9 sectors"):
        formats.serialize(disk, str(scratch / "out.mgt"))


def test_mgt_track_order_follows_the_extension(scratch):
    image = bytes((i // 5120) & 0xFF for i in range(mgt.IMAGE_SIZE_80))
    cyl_major = mgt.parse(image, mgt.CYLINDER_MAJOR)
    side_major = mgt.parse(image, mgt.SIDE_MAJOR)
    assert cyl_major.get_track(1, 0).find_index(1).data_bytes()[0] == 2
    assert side_major.get_track(1, 0).find_index(1).data_bytes()[0] == 1
    assert side_major.get_track(0, 1).find_index(1).data_bytes()[0] == 80
    # .img keeps an MGT disk MGT, written side-major
    assert formats.serialize(cyl_major, str(scratch / "x.img"))[0] == mgt.serialize(cyl_major, mgt.SIDE_MAJOR)[0]
    assert formats.serialize(cyl_major, str(scratch / "x.mgt"))[0] == image
    with pytest.raises(DiskConversionError, match="Raw PC floppy save refused: Track cylinder 0 side 0 has 10 sectors"):
        formats.serialize(cyl_major, str(scratch / "x.ima"))


def test_pro_system_track_numbers_its_fifth_sector_nine():
    image = bytes((i // 1024) & 0xFF for i in range(80 * 2 * 5 * 1024))
    disk = pro.parse(image)
    assert [s.number for s in disk.get_track(0, 0).index] == [1, 2, 3, 4, 9]
    assert [s.number for s in disk.get_track(0, 1).index] == [1, 2, 3, 4, 5]
    assert pro.serialize(disk)[0] == image
    with pytest.raises(DiskConversionError, match="PRO save refused: Image has 1 side"):
        pro.serialize(dsk.parse(build_edsk([[(0, 0, 1, 2, 0, 0, _sector_data(1))]], 1, 1)))


@pytest.mark.skipif(ZXDISK is None, reason="zxdisk is not built (tools/build/build.sh zxdisk)")
@pytest.mark.parametrize("fixture", FIXTURES)
def test_dump_and_convert_match_zxdisk(fixture, scratch):
    image = REPO_ROOT / fixture
    if not image.exists():
        pytest.skip(f"{fixture} is not in this checkout")
    result = parity.check_image(str(image), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)


@pytest.mark.skipif(ZXDISK is None, reason="zxdisk is not built (tools/build/build.sh zxdisk)")
def test_hand_built_edsk_matches_zxdisk(scratch):
    weak_a = _sector_data(3)
    weak_b = bytes(v ^ 0x55 if i % 50 == 0 else v for i, v in enumerate(weak_a))
    sectors = [(0, 0, 1, 2, 0, 0, _sector_data(1)), (0, 0, 2, 2, 0x20, 0x20, _sector_data(2)),
               (0, 0, 3, 2, 0, 0, weak_a + weak_b), (0, 0, 4, 2, 0, 0x40, _sector_data(4)),
               (0, 0, 5, 2, 0x20, 0, _sector_data(5)), (0, 0, 6, 2, 0x04, 0x01, b""),
               (0, 0, 7, 6, 0, 0, _sector_data(7, 0x1800))]  # N = 6: a long field as CPCEMU stores it
    path = scratch / "hand.dsk"
    path.write_bytes(build_edsk([sectors, []], 1, 2))
    result = parity.check_image(str(path), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)


@pytest.mark.skipif(ZXDISK is None, reason="zxdisk is not built (tools/build/build.sh zxdisk)")
def test_long_fields_cut_and_cpdread_slots_match_zxdisk(scratch):
    # three N = 6 fields as CPCEMU stores them (3 x 0x1800 bytes) do not fit a revolution and show no overlap: cut
    long_track = [(0, 0, r, 6, 0, 0, _sector_data(r, 0x1800)) for r in (1, 2, 3)]
    path = scratch / "cut.dsk"
    path.write_bytes(build_edsk([long_track], 1, 1))
    result = parity.check_image(str(path), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)
    assert any("cut by" in w for w in formats.load(str(path)).warnings)

    # CPDRead keeps a 128-byte sector in a 256-byte slot
    slotted = [(0, 0, r, 0, 0, 0, _sector_data(r, 128) + bytes(128), 128) for r in range(1, 6)]
    path = scratch / "cpdread.dsk"
    path.write_bytes(build_edsk([slotted], 1, 1, creator=b"CPDRead v3.24 "))
    result = parity.check_image(str(path), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)
    disk = formats.load(str(path))
    assert disk.get_track(0, 0).find_index(2).data_bytes() == _sector_data(2, 128)
