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


def _bad_id_crc_track():
    """16 TR-DOS sectors; sector 5 has its ID CRC broken (both bytes inverted)"""
    fields = []
    for r in range(1, 17):
        id_bytes, id_clocks = mfm_field(bytes([0xFE, 0, 0, r, 1]), gap=80 if r == 1 else 54)
        if r == 5:
            id_bytes = id_bytes[:-2] + bytes([id_bytes[-2] ^ 0xFF, id_bytes[-1] ^ 0xFF])
        fields.append((id_bytes, id_clocks))
        fields.append(mfm_field(bytes([0xFB]) + bytes([r]) * 256))
    return mfm_track(fields)


def test_dc05_bad_id_crc_detected_and_kept(scratch, capsys):
    raw, clocks = _bad_id_crc_track()
    (scratch / "bad.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    disk = udi.read(str(scratch / "bad.udi"))
    sectors = disk.get_track(0, 0).sectors
    assert len(sectors) == 16
    assert [s.number for s in sectors if not s.id_crc_valid] == [5]

    # UDI -> UDI reproduces the broken ID CRC
    udi.write(disk, str(scratch / "out.udi"))
    again = udi.read(str(scratch / "out.udi")).get_track(0, 0).sectors
    assert [s.number for s in again if not s.id_crc_valid] == [5]

    # UDI -> FDI warns: FDI has no place for it
    fdi.write(disk, str(scratch / "out.fdi"))
    assert "ID CRC error is not representable in FDI" in capsys.readouterr().out


def test_dc05_trd_refuses_bad_id_crc(scratch):
    import pytest

    from diskconverter.disk_image import DiskConversionError
    from diskconverter.formats import trd

    raw, clocks = _bad_id_crc_track()
    (scratch / "bad.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    disk = udi.read(str(scratch / "bad.udi"))
    assert not disk.get_track(0, 0).is_standard_trdos()
    with pytest.raises(DiskConversionError):
        trd.write(disk, str(scratch / "bad.trd"))


def test_dc08_marks_inside_data_are_no_sectors(scratch):
    """A data field (no ID before it, as after a damaged ID) whose bytes hold A1 A1 A1 FE + a valid ID:
    without missing clocks those bytes are data, not a sector"""
    from conftest import crc_ccitt

    fake_id = bytes([0xFE, 0x01, 0x00, 0x07, 0x01])
    crc = crc_ccitt(bytes([0xA1] * 3) + fake_id)
    payload = bytes([0xA1] * 3) + fake_id + bytes([crc >> 8, crc & 0xFF]) + bytes(246)
    raw, clocks = mfm_track([mfm_field(bytes([0xFB]) + payload, gap=80)])
    (scratch / "phantom.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    assert udi.read(str(scratch / "phantom.udi")).get_track(0, 0).sectors == []


def test_udi_without_clock_marks_still_decodes(scratch):
    """Files that never stored the marks (all-zero bitmap): every literal A1 counts"""
    (raw, _clocks), _ = _key_track()
    (scratch / "noclk.udi").write_bytes(build_udi([(raw, [])], cylinders=1, heads=1))
    assert len(udi.read(str(scratch / "noclk.udi")).get_track(0, 0).sectors) == 1
