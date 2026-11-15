#!/usr/bin/env python3
"""Compare a disk image (e.g. one the emulator exported) with a reference dump, track by track.

    diskcompare.py <image> <reference> [--image-format FMT] [--reference-format FMT] [--json] [-v]

Reads every format tools/diskconverter reads (SCL, TRD, FDI, UDI, TD0) through
its `diskconverter` package. For each physical track (cylinder, head) the sectors are compared
in stream order: C, H, R, raw N, data bytes, deleted mark, ID-only state, ID CRC (validity and
value) and data CRC (validity and value). A CRC value is compared only when both sides know it:
a valid CRC is the computed one; a broken one is known only from formats that store the raw
bytes (UDI). FDI/TD0 keep just a flag (the loader writes the computed CRC inverted, as unreal-disk
does), so for them only the validity is compared. When one
track's sector list is a rotation of the other's (the dump started at another index position)
the rotation is undone first.

Unreliable dumps: a reference track that repeats the previous track (logical order,
cylinder * heads + head) sector for sector, and whose ID fields all claim a cylinder other than
the physical one, is what a dumper produces when it runs past the formatted area and keeps
returning the last track it found. Such tracks are flagged `unreliable-dump` and skipped.

Tracks outside the reference geometry (more cylinders or heads in the image) are listed as
`extra` and do not count as mismatches.

Exit codes: 0 every compared track matches; 1 at least one mismatch; 2 bad usage or an image
could not be read.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "diskconverter"))

from diskconverter import formats  # noqa: E402
from diskconverter.disk_image import DiskConversionError, DiskImage, Sector, Track  # noqa: E402

MATCH = "match"
MISMATCH = "mismatch"
UNRELIABLE = "unreliable-dump"
EXTRA = "extra"


RAW_CRC_FORMATS = (None, "udi")  # formats whose broken CRCs are the bytes of the original medium


def keeps_raw_crcs(disk: DiskImage) -> bool:
    """True when a broken CRC in this image is the original medium's value (UDI, or built in memory)"""
    return getattr(disk, "format", None) in RAW_CRC_FORMATS


def id_crc(sector: Sector, raw: bool = True) -> bytes | None:
    """The ID CRC bytes as they are on the disk, or None when the source format does not know them."""
    if sector.id_crc_valid or raw:
        return sector.id_crc_stored
    return None


def data_crc(sector: Sector, raw: bool = True) -> bytes | None:
    if sector.no_data:
        return None
    if sector.crc_valid or raw:
        return sector.data_crc_stored
    return None


def sector_key(sector: Sector) -> tuple:
    """Everything the comparison looks at, for equality tests between whole tracks."""
    return (
        sector.cylinder, sector.head, sector.number, sector.size_code, sector.no_data, sector.deleted,
        sector.data, sector.id_crc_valid, id_crc(sector), sector.crc_valid, data_crc(sector),
    )


def _hex(value: bytes | None) -> str:
    return "unknown" if value is None else value.hex().upper()


def _num(value: int) -> str:
    return f"#{value:02X}" if value > 9 else str(value)


def compare_sectors(image: Sector, reference: Sector, image_raw: bool = True, reference_raw: bool = True) -> list[str]:
    """Differences between two sectors, each a short `field image=X reference=Y` text."""
    diffs: list[str] = []
    for name, a, b in (
        ("C", image.cylinder, reference.cylinder),
        ("H", image.head, reference.head),
        ("R", image.number, reference.number),
        ("N", image.size_code, reference.size_code),
    ):
        if a != b:
            diffs.append(f"{name} image={_num(a)} reference={_num(b)}")

    if image.id_crc_valid != reference.id_crc_valid:
        diffs.append(
            f"id-crc image={'ok' if image.id_crc_valid else 'bad'} reference={'ok' if reference.id_crc_valid else 'bad'}"
        )
    else:
        a, b = id_crc(image, image_raw), id_crc(reference, reference_raw)
        if a is not None and b is not None and a != b:
            diffs.append(f"id-crc value image={_hex(a)} reference={_hex(b)}")

    if image.no_data != reference.no_data:
        diffs.append(
            f"data-field image={'none' if image.no_data else 'present'} reference={'none' if reference.no_data else 'present'}"
        )
        return diffs
    if image.no_data:
        return diffs

    if image.deleted != reference.deleted:
        diffs.append(f"deleted image={image.deleted} reference={reference.deleted}")
    if image.data != reference.data:
        if len(image.data) != len(reference.data):
            diffs.append(f"data length image={len(image.data)} reference={len(reference.data)}")
        else:
            differing = [i for i, (x, y) in enumerate(zip(image.data, reference.data)) if x != y]
            diffs.append(f"data {len(differing)} byte(s) differ, first at offset {differing[0]}")
    if image.crc_valid != reference.crc_valid:
        diffs.append(
            f"data-crc image={'ok' if image.crc_valid else 'bad'} reference={'ok' if reference.crc_valid else 'bad'}"
        )
    elif image.data == reference.data and image.deleted == reference.deleted:
        # With different data both CRCs differ trivially; only compare values over the same bytes
        a, b = data_crc(image, image_raw), data_crc(reference, reference_raw)
        if a is not None and b is not None and a != b:
            diffs.append(f"data-crc value image={_hex(a)} reference={_hex(b)}")
    return diffs


def _align(image: list[Sector], reference: list[Sector]) -> list[Sector]:
    """Rotate the image's sector list when it is the reference's started at another index position."""
    if len(image) != len(reference) or len(image) < 2:
        return image
    ids_img = [(s.cylinder, s.head, s.number) for s in image]
    ids_ref = [(s.cylinder, s.head, s.number) for s in reference]
    if ids_img == ids_ref:
        return image
    for shift in range(1, len(image)):
        if ids_img[shift:] + ids_img[:shift] == ids_ref:
            return image[shift:] + image[:shift]
    return image


def compare_tracks(image: Track | None, reference: Track | None, image_raw: bool = True,
                   reference_raw: bool = True) -> list[str]:
    img = list(image.sectors) if image else []
    ref = list(reference.sectors) if reference else []
    if not img and not ref:
        return []
    if not img:
        return [f"unformatted in image, reference has {len(ref)} sector(s)"]
    if not ref:
        return [f"image has {len(img)} sector(s), reference track is unformatted"]

    diffs: list[str] = []
    if len(img) != len(ref):
        diffs.append(f"sector count image={len(img)} reference={len(ref)}")
    img = _align(img, ref)
    for index, (a, b) in enumerate(zip(img, ref), 1):
        for diff in compare_sectors(a, b, image_raw, reference_raw):
            diffs.append(f"sector {index} (R={_num(b.number)}): {diff}")
    for index, sector in enumerate(img[len(ref):], len(ref) + 1):
        diffs.append(f"sector {index} (R={_num(sector.number)}): only in image")
    for index, sector in enumerate(ref[len(img):], len(img) + 1):
        diffs.append(f"sector {index} (R={_num(sector.number)}): only in reference")
    return diffs


def unreliable_reference_tracks(reference: DiskImage) -> set[tuple[int, int]]:
    """Reference tracks that repeat the previous logical track and claim another cylinder."""
    flagged: set[tuple[int, int]] = set()
    order = [(cyl, head) for cyl in range(reference.cylinders) for head in range(reference.heads)]
    for prev, cur in zip(order, order[1:]):
        trk, prev_trk = reference.get_track(*cur), reference.get_track(*prev)
        if not trk or not trk.sectors or not prev_trk:
            continue
        if any(sector.cylinder == cur[0] for sector in trk.sectors):
            continue
        if [sector_key(s) for s in trk.sectors] == [sector_key(s) for s in prev_trk.sectors]:
            flagged.add(cur)
    return flagged


def compare_disks(image: DiskImage, reference: DiskImage) -> dict:
    unreliable = unreliable_reference_tracks(reference)
    image_raw, reference_raw = keeps_raw_crcs(image), keeps_raw_crcs(reference)
    cylinders = max(image.cylinders, reference.cylinders)
    heads = max(image.heads, reference.heads)
    tracks = []
    counts = {MATCH: 0, MISMATCH: 0, UNRELIABLE: 0, EXTRA: 0}
    for cyl in range(cylinders):
        for head in range(heads):
            entry = {"cylinder": cyl, "head": head, "logical": cyl * heads + head, "differences": []}
            img_trk = image.get_track(cyl, head)
            if cyl >= reference.cylinders or head >= reference.heads:
                entry["status"] = EXTRA
                entry["sectors"] = len(img_trk.sectors) if img_trk else 0
            elif (cyl, head) in unreliable:
                entry["status"] = UNRELIABLE
            else:
                entry["differences"] = compare_tracks(img_trk, reference.get_track(cyl, head), image_raw,
                                                      reference_raw)
                entry["status"] = MISMATCH if entry["differences"] else MATCH
            counts[entry["status"]] += 1
            tracks.append(entry)
    return {
        "image": {"cylinders": image.cylinders, "heads": image.heads},
        "reference": {"cylinders": reference.cylinders, "heads": reference.heads},
        "summary": counts,
        "tracks": tracks,
    }


def print_report(result: dict, image_path: str, reference_path: str, verbose: bool) -> None:
    img, ref, counts = result["image"], result["reference"], result["summary"]
    print(f"image:     {image_path} ({img['cylinders']} cylinders x {img['heads']} heads)")
    print(f"reference: {reference_path} ({ref['cylinders']} cylinders x {ref['heads']} heads)")
    unreliable = [t for t in result["tracks"] if t["status"] == UNRELIABLE]
    extra = [t for t in result["tracks"] if t["status"] == EXTRA]
    for track in result["tracks"]:
        label = f"track {track['cylinder']}/{track['head']} (logical {track['logical']})"
        if track["status"] == MISMATCH:
            print(f"{label}: MISMATCH")
            for diff in track["differences"]:
                print(f"  {diff}")
        elif verbose:
            print(f"{label}: {track['status']}")
    if unreliable and not verbose:
        print("unreliable-dump (skipped): logical " + ", ".join(str(t["logical"]) for t in unreliable))
    if extra and not verbose:
        print("extra (not in reference): logical " + ", ".join(str(t["logical"]) for t in extra))
    print(
        f"summary: {counts[MATCH]} match, {counts[MISMATCH]} mismatch, "
        f"{counts[UNRELIABLE]} unreliable-dump, {counts[EXTRA]} extra"
    )


def load(path: str, explicit: str | None) -> DiskImage:
    module = formats.resolve_input(path, explicit)
    disk = module.read(path)
    for warning in disk.warnings:
        print(f"warning: {path}: {warning}", file=sys.stderr)
    return disk


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", help="disk image under test (scl|trd|fdi|udi|td0)")
    parser.add_argument("reference", help="reference dump (scl|trd|fdi|udi|td0)")
    parser.add_argument("--image-format", metavar="FORMAT", help="force the image format")
    parser.add_argument("--reference-format", metavar="FORMAT", help="force the reference format")
    parser.add_argument("--json", action="store_true", help="print the result as JSON")
    parser.add_argument("-v", "--verbose", action="store_true", help="list every track, not only mismatches")
    args = parser.parse_args(argv)

    disks = []
    for path, explicit in ((args.image, args.image_format), (args.reference, args.reference_format)):
        try:
            disks.append(load(path, explicit))
        except ValueError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        except (DiskConversionError, OSError) as e:
            print(f"error: reading {path}: {e}", file=sys.stderr)
            return 2

    result = compare_disks(*disks)
    if args.json:
        json.dump({"image_path": args.image, "reference_path": args.reference, **result}, sys.stdout, indent=2)
        print()
    else:
        print_report(result, args.image, args.reference, args.verbose)
    return 1 if result["summary"][MISMATCH] else 0


if __name__ == "__main__":
    sys.exit(main())
