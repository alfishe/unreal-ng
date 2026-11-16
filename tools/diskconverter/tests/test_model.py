"""The stream model (disk_image): reindex on MFM and FM streams, broken CRCs, weak bytes through UDI."""

from __future__ import annotations

from conftest import build_udi, mfm_field, mfm_track
from diskconverter.disk_image import (FM, MFM, DiskImage, Sector, Track, TrackFormatSpec, crc_wd1793,
                                      crc_wd1793_fm, stored_u16)
from diskconverter.dump import dump
from diskconverter.formats import fdi, td0, udi


def test_mfm_reindex_finds_formatted_sectors():
    track = Track(0, 0)
    track.format_track(0, 0, TrackFormatSpec.trdos())
    assert track.length == 6250 and track.encoding == MFM
    index = track.index
    assert [s.number for s in index] == list(range(1, 17))
    first = index[0]
    assert (first.idam, first.dam, first.data, first.size) == (25, 69, 70, 256)  # 10 gap + 12 sync + A1 x 3
    assert all(s.has_data and s.is_id_crc_valid() and s.is_data_crc_valid() for s in index)
    assert track.clock_mark(first.idam - 1) and not track.clock_mark(first.idam)  # the A1 before FE is marked


def test_fm_reindex_uses_clock_marks():
    track = Track(0, 0)
    track.format_track(0, 0, TrackFormatSpec.ibm3740())
    assert track.encoding == FM and track.length == 3125
    index = track.index
    assert [s.number for s in index] == list(range(1, 17))
    assert all(s.size == 128 for s in index)
    sector = index[0]
    assert track.clock_mark(sector.idam) and track.clock_mark(sector.dam)  # FM: the mark byte itself is clocked
    raw = track.raw
    assert stored_u16(raw, sector.idam + 5) == crc_wd1793_fm(raw[sector.idam:sector.idam + 5])
    assert stored_u16(raw, sector.data + 128) == crc_wd1793_fm(raw[sector.dam:sector.data + 128])


def test_mfm_stream_without_clock_marks_still_indexes():
    """A stream whose bitmap is empty (an old UDI): every literal A1 A1 A1 counts"""
    formatted = Track(0, 0)
    formatted.format_track(0, 0, TrackFormatSpec.trdos())
    bare = Track(0, 0)
    bare.set_stream(formatted.raw, None, None, MFM)
    assert not bare.has_clock_marks()
    assert [s.number for s in bare.index] == list(range(1, 17))


def test_broken_crcs_are_stream_bytes():
    disk = DiskImage(1, 1)
    disk.track(0, 0).sectors = [
        Sector(cylinder=0, head=0, number=1, size_code=1, data=bytes(256), id_crc_valid=False),
        Sector(cylinder=0, head=0, number=2, size_code=1, data=bytes(range(256)), crc_valid=False),
    ]
    first, second = disk.get_track(0, 0).index
    assert not first.is_id_crc_valid() and first.is_data_crc_valid()
    assert first.id_crc == crc_wd1793(bytes([0xFE, 0, 0, 1, 1])) ^ 0xFFFF
    assert second.is_id_crc_valid() and not second.is_data_crc_valid()
    assert second.data_crc() == crc_wd1793(bytes([0xFB]) + bytes(range(256))) ^ 0xFFFF

    # FDI keeps the data CRC flag (the reader inverts the computed CRC again) but not the ID CRC error
    data, warnings = fdi.serialize(disk)
    assert any("sector 1: ID CRC error cannot be stored" in w for w in warnings)
    again = fdi.parse(data).get_track(0, 0).sectors
    assert [(s.id_crc_valid, s.crc_valid) for s in again] == [(True, True), (True, False)]


def test_udi_keeps_exact_broken_crc_and_clock_bitmap(scratch):
    id_bytes, id_clocks = mfm_field(bytes([0xFE, 0, 0, 1, 1]), gap=80)
    raw, clocks = mfm_track([(id_bytes[:-2] + b"\x12\x34", id_clocks), mfm_field(bytes([0xFB]) + bytes(256))])
    disk = udi.parse(build_udi([(raw, clocks)], cylinders=1, heads=1))
    out, _ = udi.serialize(disk)
    again = udi.parse(out)
    assert again.get_track(0, 0).raw == disk.get_track(0, 0).raw
    assert again.get_track(0, 0).clock == disk.get_track(0, 0).clock
    sector = again.get_track(0, 0).sectors[0]
    assert (sector.id_crc_valid, sector.id_crc_stored) == (False, b"\x12\x34")


def test_weak_bytes_round_trip_through_udi():
    disk = DiskImage(2, 1)
    for cylinder in range(2):
        disk.track(cylinder, 0).format_track(cylinder, 0, TrackFormatSpec.trdos())
    track = disk.get_track(1, 0)
    sector = track.index[4]
    for offset in range(sector.data + 10, sector.data + 20):
        track.set_weak_byte(offset, True)
    data, warnings = udi.serialize(disk)
    assert warnings == [] and b"UDIW" in data
    again = udi.parse(data)
    assert not again.get_track(0, 0).has_weak_bits()
    weak = again.get_track(1, 0)
    assert [o for o in range(weak.length) if weak.weak_byte(o)] == list(range(sector.data + 10, sector.data + 20))
    assert again.warnings == []
    assert dump(again) == dump(udi.parse(udi.serialize(again)[0]))  # stable through a second round
    # A sector-list format cannot hold them: FDI / TD0 drop the weak bitmap
    assert not fdi.parse(fdi.serialize(again)[0]).get_track(1, 0).has_weak_bits()
    assert not td0.parse(td0.serialize(again)[0]).get_track(1, 0).has_weak_bits()


def test_fm_track_survives_td0_and_warns_in_fdi():
    disk = DiskImage(1, 1)
    disk.track(0, 0).format_track(0, 0, TrackFormatSpec.ibm3740())
    data, warnings = td0.serialize(disk)
    assert warnings == []  # nominal FM length, valid CRCs: nothing lost
    again = td0.parse(data)
    assert dump(again, "x", []) == dump(disk, "x", [])  # the TD0 reader lays FM out exactly as formatted
    _, fdi_warnings = fdi.serialize(disk)
    assert "FDI cylinder 0 head 0: FM track stored as a plain sector list (density lost)" in fdi_warnings
