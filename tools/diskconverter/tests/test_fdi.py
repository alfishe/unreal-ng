"""FDI reader / writer: the raw N byte of the ID field survives (ZF6 mastering, DISK-01)."""

from __future__ import annotations

import pytest

from conftest import build_fdi
from diskconverter.disk_image import DiskConversionError
from diskconverter.formats import fdi, trd


def _key_sector_fdi(path):
    """One track, one sector C=#A4 H=#42 R=#F7 N=#F7, 1K data, CRC ok (flag bit N & 3 = 3)"""
    data = bytes(range(256)) * 4
    path.write_bytes(build_fdi([[(0xA4, 0x42, 0xF7, 0xF7, 0x08, data)]], cylinders=1, heads=1))
    return data


def test_dc01_read_keeps_raw_n(scratch, capsys):
    data = _key_sector_fdi(scratch / "key.fdi")
    disk = fdi.read(str(scratch / "key.fdi"))
    sector = disk.get_track(0, 0).sectors[0]
    assert (sector.cylinder, sector.head, sector.number) == (0xA4, 0x42, 0xF7)
    assert sector.size_code == 0xF7
    assert sector.data_size == 1024
    assert sector.data == data
    assert sector.crc_valid
    assert "masked" not in capsys.readouterr().out


def test_dc01_crc_flag_uses_n_and_3(scratch):
    path = scratch / "bad.fdi"
    path.write_bytes(build_fdi([[(0xA4, 0x42, 0xF7, 0xF7, 0x00, bytes(1024))]], cylinders=1, heads=1))
    assert not fdi.read(str(path)).get_track(0, 0).sectors[0].crc_valid


def test_dc02_write_keeps_raw_n_and_crc_flag(scratch):
    _key_sector_fdi(scratch / "key.fdi")
    disk = fdi.read(str(scratch / "key.fdi"))
    fdi.write(disk, str(scratch / "out.fdi"))
    out = (scratch / "out.fdi").read_bytes()
    # 14-byte header, 7-byte track header, then the sector header C H R N flags doff
    assert out[21:26] == bytes([0xA4, 0x42, 0xF7, 0xF7, 0x08])
    assert out == (scratch / "key.fdi").read_bytes()


def test_dc06_trd_refuses_n_f7(scratch):
    sectors = [(0, 0, r, 1, 0x02, bytes(256)) for r in range(1, 16)] + [(0, 0, 16, 0xF7, 0x08, bytes(1024))]
    (scratch / "odd.fdi").write_bytes(build_fdi([sectors], cylinders=1, heads=1))
    disk = fdi.read(str(scratch / "odd.fdi"))
    with pytest.raises(DiskConversionError):
        trd.write(disk, str(scratch / "odd.trd"))
