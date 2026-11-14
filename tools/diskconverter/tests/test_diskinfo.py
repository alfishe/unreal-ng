"""tools/diskinfo shows the raw N and checks the ID CRC over it (ZF6 mastering, DISK-05)."""

from __future__ import annotations

import importlib.util
from pathlib import Path

from conftest import build_udi, mfm_field, mfm_track
from diskconverter.disk_image import Sector
from diskconverter.formats import udi

_spec = importlib.util.spec_from_file_location("diskinfo", Path(__file__).resolve().parents[2] / "diskinfo" / "diskinfo.py")
diskinfo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(diskinfo)


def test_dc07_idam_line_raw_n_and_valid_crc():
    sector = Sector(cylinder=0xA4, head=0x42, number=0xF7, size_code=0xF7, data=bytes(1024))
    line = diskinfo.idam_line(sector)
    assert line.startswith("C=164 H=66 R=247 N=#F7 (1024 B)")
    assert "id-crc 9FF8 ok" in line


def test_idam_line_standard_sector():
    line = diskinfo.idam_line(Sector(cylinder=1, head=0, number=1, size_code=1, data=bytes(256)))
    assert "N=1 (256 B)" in line


def test_idam_line_and_track_summary_flag_bad_id_crc(scratch):
    id_bytes, clocks = mfm_field(bytes([0xFE, 0, 0, 1, 1]), gap=80)
    id_bytes = id_bytes[:-1] + bytes([id_bytes[-1] ^ 0x01])
    raw, track_clocks = mfm_track([(id_bytes, clocks), mfm_field(bytes([0xFB]) + bytes(256))])
    (scratch / "bad.udi").write_bytes(build_udi([(raw, track_clocks)], cylinders=1, heads=1))
    track = udi.read(str(scratch / "bad.udi")).get_track(0, 0)
    assert "ERROR" in diskinfo.idam_line(track.sectors[0])
    assert "1 id-crc errors" in diskinfo.describe_track(track, 0, 0)


def test_marker_lines_raw_n():
    raw, _ = mfm_track([mfm_field(bytes([0xFE, 0xA4, 0x42, 0xF7, 0xF7]), gap=80), mfm_field(bytes([0xFB]) + bytes(1024))])
    lines = diskinfo.marker_lines(raw)
    assert "IDAM C=164 H=66 R=247 N=#F7 (1024 B)" in lines[0]
    assert "data 1024 B" in lines[1]


def test_protection_analysis_lists_raw_n(scratch, capsys):
    raw, clocks = mfm_track([mfm_field(bytes([0xFE, 0xA4, 0x42, 0xF7, 0xF7]), gap=80), mfm_field(bytes([0xFB]) + bytes(1024))])
    (scratch / "key.udi").write_bytes(build_udi([(raw, clocks)], cylinders=1, heads=1))
    import subprocess
    import sys

    out = subprocess.run(
        [sys.executable, "-I", str(Path(diskinfo.__file__)), str(scratch / "key.udi")], capture_output=True, text=True
    ).stdout
    assert "size codes N > 3 on logical 0" in out
