"""Hobeta, MB-02 and MDOS (D40 / D80): the ports of unreal-disk's loader_hobeta / loader_mb02 / loader_mdos.
The zxdisk comparisons are skipped when the binary is not built."""

from __future__ import annotations

import importlib.util
import json

import pytest

from conftest import REPO_ROOT
from diskconverter import formats
from diskconverter.disk_image import DiskConversionError
from diskconverter.formats import hobeta, mb02, mdos, trd

_TOOL = REPO_ROOT / "tools" / "verification" / "disk" / "parity.py"
_spec = importlib.util.spec_from_file_location("disk_parity_hmm", _TOOL)
parity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(parity)
ZXDISK = parity.find_zxdisk(None)

HOBETA = REPO_ROOT / "testdata" / "loaders" / "hobeta" / "hello.$C"
HOBETA_BAD = REPO_ROOT / "testdata" / "loaders" / "hobeta" / "bad-checksum.$C"
MB02 = REPO_ROOT / "testdata" / "loaders" / "mb02" / "SYS308.MB2"
MDOS = REPO_ROOT / "testdata" / "loaders" / "mdos" / "bootdisk.d80"


def _need(path):
    if not path.exists():
        pytest.skip(f"{path.name} is not in this checkout")
    return path.read_bytes()


def _hobeta_file(name: bytes, kind: int, body: bytes, reserved: int = 0) -> bytes:
    sectors = len(body) // 256
    head = name.ljust(8) + bytes([kind, 0x00, 0x80, len(body) & 0xFF, len(body) >> 8, reserved, sectors])
    total = hobeta.checksum(head)
    return head + bytes([total & 0xFF, total >> 8]) + body


def _mdos_image(cylinders: int, sides: int, sectors: int) -> bytes:
    data = bytearray(cylinders * sides * sectors * 512)
    data[0xB1] = 0x10 if sides == 2 else 0
    data[0xB2], data[0xB3] = cylinders, sectors
    data[204:208] = b"SDOS"
    for lsn in range(1, len(data) // 512):
        data[lsn * 512] = lsn & 0xFF
    return bytes(data)


def _mb02_image(tracks: int, sectors: int) -> bytes:
    data = bytearray(tracks * 2 * sectors * 1024)
    data[0x00], data[0x03] = 0x18, 0x02
    data[0x04], data[0x06], data[0x08], data[0x0A] = tracks, sectors, 2, 1
    for lsn in range(1, len(data) // 1024):
        data[lsn * 1024 + 1] = lsn & 0xFF
    return bytes(data)


# ------------------------------------------------------------------------------------------------- Hobeta

def test_hobeta_lands_in_catalog_entry_zero():
    body = bytes(range(256)) * 3
    disk = hobeta.parse(_hobeta_file(b"prog", ord("C"), body, reserved=0x55))
    assert (disk.cylinders, disk.heads, disk.format) == (80, 2, "hobeta")
    entry = disk.sector_bytes(0, 0, 1)[:16]
    assert entry == b"prog    C" + bytes([0x00, 0x80, 0x00, 0x03, 3, 0, 1])
    info = disk.sector_bytes(0, 0, 9)
    assert (info[0xE1], info[0xE2], info[0xE4]) == (3, 1, 1)
    assert info[0xE5] | info[0xE6] << 8 == 2544 - 3
    assert disk.sector_bytes(0, 1, 1) + disk.sector_bytes(0, 1, 2) + disk.sector_bytes(0, 1, 3) == body


def test_hobeta_rejects_bad_header():
    good = _hobeta_file(b"x", ord("B"), bytes(256))
    with pytest.raises(DiskConversionError, match="header checksum mismatch"):
        hobeta.parse(good[:15] + bytes([good[15] ^ 1]) + good[16:])
    with pytest.raises(DiskConversionError, match=r"declares 1 sectors \(273 bytes with header\), file is 529"):
        hobeta.parse(good + bytes(256))
    assert hobeta.detect(good) and not hobeta.detect(good[:-1])


def test_hobeta_fixture_and_save_refusal(scratch):
    _need(HOBETA)
    disk = formats.load(str(HOBETA))
    assert disk.format == "hobeta"
    data, _ = formats.serialize(disk, str(scratch / "out.trd"))
    assert data[0:8] == b"hello   "
    result = formats.save(disk, str(scratch / "out.$c"))
    assert not result.saved and result.reason == hobeta.SAVE_REFUSAL
    _need(HOBETA_BAD)
    with pytest.raises(DiskConversionError, match="has no valid Hobeta header"):
        formats.load(str(HOBETA_BAD))


# ------------------------------------------------------------------------------------------------- MB-02

def test_mb02_round_trip_and_layout():
    image = _mb02_image(4, 11)
    disk = mb02.parse(image)
    assert (disk.cylinders, disk.heads) == (4, 2)
    track = disk.get_track(1, 1)
    assert track.length == 12500 and track.sector_count() == 11
    assert all(s.size == 1024 and s.is_data_crc_valid() for s in track.index)
    assert mb02.serialize(disk)[0] == image
    dd = _mb02_image(2, 5)
    assert mb02.parse(dd).get_track(0, 0).length == 6250


def test_mb02_refusals():
    image = _mb02_image(2, 5)
    assert formats.probe_head(image, len(image), "mbd") == "mb02"
    assert not mb02.detect(image + bytes(1024))
    with pytest.raises(DiskConversionError, match=r"gives 2 x 2 x 5 sectors \(20480 bytes\), the file has 21504"):
        mb02.parse(image + bytes(1024))
    with pytest.raises(DiskConversionError, match="no MB-02 boot sector"):
        mb02.parse(bytes(len(image)))
    with pytest.raises(DiskConversionError, match="sector 1 is missing or not 1024 bytes"):
        mb02.serialize(trd.parse(bytes(655360)))
    with pytest.raises(DiskConversionError, match=r"Image has 1 side\(s\), an MB-02 image has 2"):
        mb02.serialize(mdos.parse(_mdos_image(40, 1, 9)))


def test_mb02_fixture_round_trip():
    data = _need(MB02)
    disk = formats.load(str(MB02))
    assert (disk.format, disk.cylinders, disk.heads) == ("mb02", 82, 2)
    assert mb02.serialize(disk)[0] == data


# ------------------------------------------------------------------------------------------------- MDOS

def test_mdos_round_trip_and_layout():
    for cylinders, sides, sectors, length in ((40, 1, 9, 6250), (80, 2, 9, 6250), (4, 2, 18, 12500)):
        image = _mdos_image(cylinders, sides, sectors)
        assert formats.probe_head(image[:formats.PROBE_BYTES], len(image), "d80") == "mdos"
        disk = mdos.parse(image)
        assert (disk.cylinders, disk.heads, disk.get_track(0, 0).length) == (cylinders, sides, length)
        assert mdos.serialize(disk)[0] == image


def test_mdos_refusals():
    image = _mdos_image(40, 2, 9)
    with pytest.raises(DiskConversionError, match=r"gives 40 x 2 x 9 sectors \(368640 bytes\), the file has 368641"):
        mdos.parse(image + b"\0")
    with pytest.raises(DiskConversionError, match='no MDOS boot sector'):
        mdos.parse(bytes(512))
    with pytest.raises(DiskConversionError, match="MDOS save refused: Track cylinder 0 side 0: sector 1 is missing"):
        mdos.serialize(trd.parse(bytes(655360)))


def test_mdos_fixture_round_trip():
    data = _need(MDOS)
    disk = formats.load(str(MDOS))
    assert (disk.format, disk.cylinders, disk.heads) == ("mdos", 80, 2)
    assert mdos.serialize(disk)[0] == data


# ------------------------------------------------------------------------------------------------- vs zxdisk

@pytest.mark.skipif(ZXDISK is None, reason="zxdisk is not built (tools/build/build.sh zxdisk)")
@pytest.mark.parametrize("image", [HOBETA, HOBETA_BAD, MB02, MDOS], ids=lambda p: p.name)
def test_matches_zxdisk(image, scratch):
    _need(image)
    result = parity.check_image(str(image), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)
