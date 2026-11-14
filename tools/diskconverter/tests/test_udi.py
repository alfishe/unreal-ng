"""UDI / raw MFM track decoding (ZF6 mastering: DISK-02)."""

from __future__ import annotations

from conftest import build_udi, mfm_field, mfm_track
from diskconverter.formats import fdi, udi


def _key_track():
    """The ZF6 key track as WRITE TRACK leaves it: ID C=#A4 H=#42 R=#F7 N=#F7 with a valid CRC, 1K data"""
    data = bytes((i * 3) & 0xFF for i in range(1024))
    return mfm_track([mfm_field(bytes([0xFE, 0xA4, 0x42, 0xF7, 0xF7]), gap=80), mfm_field(bytes([0xFB]) + data)]), data


def test_dc04_udi_key_sector_keeps_raw_n(scratch):
    (raw, clocks), data = _key_track()
    (scratch / "key.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    sectors = udi.read(str(scratch / "key.udi")).get_track(0, 0).sectors
    assert len(sectors) == 1
    sector = sectors[0]
    assert (sector.cylinder, sector.head, sector.number, sector.size_code) == (0xA4, 0x42, 0xF7, 0xF7)
    assert sector.data_size == 1024
    assert sector.data == data
    assert sector.crc_valid


def test_dc03_udi_to_fdi_keeps_raw_n(scratch):
    (raw, clocks), _ = _key_track()
    (scratch / "key.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    fdi.write(udi.read(str(scratch / "key.udi")), str(scratch / "key.fdi"))
    sector = fdi.read(str(scratch / "key.fdi")).get_track(0, 0).sectors[0]
    assert sector.size_code == 0xF7


def test_udi_round_trip_keeps_raw_n(scratch):
    (raw, clocks), data = _key_track()
    (scratch / "key.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    udi.write(udi.read(str(scratch / "key.udi")), str(scratch / "out.udi"))
    sector = udi.read(str(scratch / "out.udi")).get_track(0, 0).sectors[0]
    assert (sector.size_code, sector.data, sector.crc_valid) == (0xF7, data, True)
