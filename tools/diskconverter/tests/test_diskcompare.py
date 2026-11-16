"""tools/diskcompare: emulator image vs reference dump, track by track (R-07 / CMP-01)."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from conftest import build_udi, mfm_field, mfm_track
from diskconverter.disk_image import DiskImage, Sector
from diskconverter.formats import fdi, udi

_TOOL = Path(__file__).resolve().parents[2] / "diskcompare" / "diskcompare.py"
_spec = importlib.util.spec_from_file_location("diskcompare", _TOOL)
diskcompare = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(diskcompare)


def _disk(cylinders: int = 4, heads: int = 2) -> DiskImage:
    """Plain TR-DOS-like geometry, every sector filled with a byte unique to its position"""
    disk = DiskImage(cylinders=cylinders, heads=heads)
    for cyl in range(cylinders):
        for head in range(heads):
            disk.track(cyl, head).sectors = [
                Sector(cylinder=cyl, head=head, number=r, size_code=1, data=bytes([(cyl * 2 + head + r) & 0xFF]) * 256)
                for r in range(1, 17)
            ]
    return disk


def _run(scratch: Path, image: DiskImage, reference: DiskImage, image_name="image.udi", ref_name="ref.fdi"):
    for disk, name in ((image, image_name), (reference, ref_name)):
        module = udi if name.endswith(".udi") else fdi
        module.write(disk, str(scratch / name))
    proc = subprocess.run(
        [sys.executable, "-I", str(_TOOL), str(scratch / image_name), str(scratch / ref_name), "--json"],
        capture_output=True,
        text=True,
    )
    return proc.returncode, json.loads(proc.stdout)


def _track(result: dict, cyl: int, head: int) -> dict:
    return next(t for t in result["tracks"] if (t["cylinder"], t["head"]) == (cyl, head))


def test_cmp_identical_images_match(scratch):
    code, result = _run(scratch, _disk(), _disk())
    assert code == 0
    assert result["summary"] == {"match": 8, "mismatch": 0, "unreliable-dump": 0, "extra": 0}


def test_cmp_rotated_track_still_matches(scratch):
    image = _disk()
    sectors = image.get_track(1, 0).sectors
    image.get_track(1, 0).sectors = sectors[5:] + sectors[:5]
    code, _ = _run(scratch, image, _disk())
    assert code == 0


def test_cmp_raw_n_mismatch(scratch):
    image = _disk()
    key = Sector(cylinder=0xA4, head=0x42, number=0xF7, size_code=0xF7, data=bytes(1024))
    image.track(2, 1).sectors = [key]
    reference = _disk()
    reference.track(2, 1).sectors = [Sector(cylinder=0xA4, head=0x42, number=0xF7, size_code=0x03, data=bytes(1024))]
    code, result = _run(scratch, image, reference)
    assert code == 1
    track = _track(result, 2, 1)
    assert track["status"] == "mismatch"
    assert track["differences"][0].startswith("sector 1 (R=#F7): N image=#F7 reference=3")
    assert result["summary"]["mismatch"] == 1


def test_cmp_id_crc_value_mismatch(scratch):
    """Both IDs broken, but with different stored bytes: only UDI carries them, so both sides are UDI"""
    tracks = []
    for crc in (b"\x12\x34", b"\x12\x35"):
        id_bytes, id_clocks = mfm_field(bytes([0xFE, 0, 0, 1, 1]), gap=80)
        tracks.append(mfm_track([(id_bytes[:-2] + crc, id_clocks), mfm_field(bytes([0xFB]) + bytes(256))]))
    (scratch / "image.udi").write_bytes(build_udi([tracks[0]], cylinders=1, heads=1))
    (scratch / "ref.udi").write_bytes(build_udi([tracks[1]], cylinders=1, heads=1))
    result = diskcompare.compare_disks(udi.read(str(scratch / "image.udi")), udi.read(str(scratch / "ref.udi")))
    assert _track(result, 0, 0)["differences"] == ["sector 1 (R=1): id-crc value image=1234 reference=1235"]


def test_cmp_id_crc_validity_mismatch_and_unknown_value(scratch):
    image = _disk()
    sectors = image.get_track(0, 1).sectors
    sectors[3].id_crc_valid = False  # FDI cannot carry it: the image is UDI
    image.get_track(0, 1).sectors = sectors
    code, result = _run(scratch, image, _disk())
    assert code == 1
    assert _track(result, 0, 1)["differences"] == ["sector 4 (R=4): id-crc image=bad reference=ok"]

    # A bad ID CRC on both sides, value known only from UDI: validity matches, the value is not compared
    reference = _disk()
    fdi.write(reference, str(scratch / "ref.fdi"))
    disk_ref = fdi.read(str(scratch / "ref.fdi"))
    view = disk_ref.get_track(0, 1).index[3]
    view.set_id_crc(view.compute_id_crc() ^ 0xFFFF)  # FDI cannot store it; as if a flag-only format kept it
    result = diskcompare.compare_disks(udi.read(str(scratch / "image.udi")), disk_ref)
    assert result["summary"]["mismatch"] == 0


def test_cmp_duplicate_reference_tracks_skipped(scratch):
    """The dumper ran past the formatted area: reference logical tracks 5..7 repeat track 4 (cyl 2 head 0,
    the key track whose ID claims C=#A4, as in ZXF61_O.FDI where tracks 120..159 repeat track 119)"""
    key = [Sector(cylinder=0xA4, head=0x42, number=0xF7, size_code=0xF7, data=bytes(range(256)) * 4)]
    reference = _disk()
    reference.track(2, 0).sectors = key
    for cyl, head in ((2, 1), (3, 0), (3, 1)):
        reference.track(cyl, head).sectors = [Sector(**vars(s)) for s in key]
    image = _disk()
    image.track(2, 0).sectors = [Sector(**vars(s)) for s in key]
    code, result = _run(scratch, image, reference)
    assert code == 0
    assert [t["logical"] for t in result["tracks"] if t["status"] == "unreliable-dump"] == [5, 6, 7]
    assert _track(result, 2, 0)["status"] == "match"


def test_cmp_duplicate_with_own_cylinder_is_compared(scratch):
    """A repeat whose IDs claim the physical cylinder is not a dumper artefact: still compared"""
    reference = DiskImage(cylinders=1, heads=2)
    sectors = [Sector(cylinder=0, head=0, number=1, size_code=1, data=bytes(256))]
    reference.track(0, 0).sectors = sectors
    reference.track(0, 1).sectors = [Sector(**vars(s)) for s in sectors]
    image = DiskImage(cylinders=1, heads=2)
    image.track(0, 0).sectors = [Sector(**vars(s)) for s in sectors]
    code, result = _run(scratch, image, reference)
    assert code == 1
    assert _track(result, 0, 1)["status"] == "mismatch"


def test_cmp_extra_cylinders_and_text_report(scratch):
    image = _disk(cylinders=5)
    udi.write(image, str(scratch / "image.udi"))
    fdi.write(_disk(), str(scratch / "ref.fdi"))
    proc = subprocess.run(
        [sys.executable, "-I", str(_TOOL), str(scratch / "image.udi"), str(scratch / "ref.fdi")],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0
    assert "extra (not in reference): logical 8, 9" in proc.stdout
    assert "summary: 8 match, 0 mismatch, 0 unreliable-dump, 2 extra" in proc.stdout
