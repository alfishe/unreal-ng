#!/usr/bin/env python3
"""Inspect ZX-Spectrum/TR-DOS disk images: geometry summary plus full dumps.

    diskinfo.py <image> [--from FORMAT] [--tracks]
    diskinfo.py <image> --dump sectors [--track 0-2] [--side 1] [--sector 9]
    diskinfo.py <image> --dump idam   [--track 0] [--sector 1-8]
    diskinfo.py <image> --dump raw    [--track 5] [--out track5.bin]

Reads every format diskconvert.py can read - SCL, TRD, FDI, UDI and uncompressed
TD0 - through the same `diskconverter` package (imported from the sibling
directory), so both tools always agree on parsing.

Brief mode (the default) prints the geometry: cylinders x heads, the sector count
and sector sizes of every track (grouped into runs when uniform), then two
analysis blocks: `filesystems` (TR-DOS catalog, CP/M directory, MS-DOS FAT boot
sector, iS-DOS/NedoOS signatures) and `protection analysis` (ID/position
mismatches, non-TR-DOS reformatting, high sector numbers, deleted/CRC-error/
ID-only sectors, over-length cylinders, unformatted holes - with a verdict).
--tracks adds a one-line-per-track table.

Dump modes:
  sectors  sector data only, hexdumped in track stream order
  idam     each sector preceded by its decoded ID field (CHRN, size, data-CRC
           state, deleted/no-data flags) and the CRC bytes the fields carry
  raw      the raw MFM byte stream of each track - the original stream for UDI
           inputs, otherwise one synthesized by diskconverter.mfm (gaps rebuilt,
           nominal 6250-byte revolution) - with IDAM/DAM marker offsets listed
           ahead of the hexdump

Selection: --track (physical cylinders), --side (heads) and --sector (sector ID
number R) each accept comma-separated numbers and inclusive ranges: `0`, `0-3`,
`0,2,5-7`. --sector does not apply to --dump raw (a raw stream is whole-track).

--out FILE writes the payload as binary instead of hexdumping it: concatenated
sector data for --dump sectors, concatenated track streams for --dump raw.
`--out -` writes the binary to stdout.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "diskconverter"))

from diskconverter import formats, mfm, trdos
from diskconverter.disk_image import DiskConversionError, DiskImage, Sector, Track
from diskconverter.formats import udi as udi_format


# --------------------------------------------------------------- selection

def parse_spec(spec: str | None, option: str) -> set[int] | None:
    """'0,2,5-7' -> {0,2,5,6,7}; None (no option given) selects everything."""
    if spec is None:
        return None
    values: set[int] = set()
    for token in spec.split(","):
        token = token.strip()
        lo_text, sep, hi_text = token.partition("-")
        try:
            lo = int(lo_text)
            hi = int(hi_text) if sep else lo
        except ValueError:
            raise ValueError(
                f"bad --{option} item '{token}' (expected N or N-M, comma-separated)"
            ) from None
        if lo > hi:
            raise ValueError(f"--{option} range '{token}' is backwards")
        values.update(range(lo, hi + 1))
    return values


def fmt_numbers(values: set[int]) -> str:
    """{1,2,3,5,8} -> '1-3,5,8' - the inverse of parse_spec, for summaries."""
    parts: list[str] = []
    run_start: int | None = None
    prev: int | None = None
    for value in sorted(values):
        if prev is None or value != prev + 1:
            if run_start is not None:
                parts.append(str(run_start) if run_start == prev else f"{run_start}-{prev}")
            run_start = value
        prev = value
    if run_start is not None:
        parts.append(str(run_start) if run_start == prev else f"{run_start}-{prev}")
    return ",".join(parts)


def iter_selected_tracks(disk: DiskImage, cyl_sel: set[int] | None, head_sel: set[int] | None):
    """Physical tracks in logical order (cylinder-major, head-minor), filtered."""
    for cyl in range(disk.cylinders):
        if cyl_sel is not None and cyl not in cyl_sel:
            continue
        for head in range(disk.heads):
            if head_sel is not None and head not in head_sel:
                continue
            yield cyl, head


def warn_out_of_range(sel: set[int] | None, valid, option: str) -> None:
    if sel is None:
        return
    dropped = sel - set(valid)
    if dropped:
        print(
            f"note: --{option} selection {fmt_numbers(dropped)} is not present in the image; ignored",
            file=sys.stderr,
        )


# --------------------------------------------------------------- formatting

def hexdump(data: bytes, indent: str = "  ") -> str:
    lines = []
    for off in range(0, len(data), 16):
        chunk = data[off : off + 16]
        hex_part = " ".join(f"{byte:02X}" for byte in chunk)
        ascii_part = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        lines.append(f"{indent}{off:04X}  {hex_part:<47}  |{ascii_part}|")
    return "\n".join(lines)


def describe_track(trk: Track | None, cyl: int = -1, head: int = -1) -> str:
    if trk is None or not trk.sectors:
        return "unformatted"
    deleted = sum(1 for s in trk.sectors if s.deleted)
    bad_crc = sum(1 for s in trk.sectors if not s.crc_valid)
    no_data = sum(1 for s in trk.sectors if s.no_data)
    mismatched = sum(1 for s in trk.sectors if cyl >= 0 and (s.cylinder != cyl or s.head != head))
    sizes = "+".join(f"{size} B" for size in sorted({128 << (s.size_code & 3) for s in trk.sectors}))
    desc = f"{len(trk.sectors)} sector{'s' if len(trk.sectors) != 1 else ''}, R={fmt_numbers({s.number for s in trk.sectors})}, {sizes}"
    notes = []
    if mismatched:
        notes.append(f"{mismatched} id-mismatch")
    if deleted:
        notes.append(f"{deleted} deleted")
    if bad_crc:
        notes.append(f"{bad_crc} data-crc errors")
    if no_data:
        notes.append(f"{no_data} id-only")
    return desc + (f" [{'; '.join(notes)}]" if notes else "")


# ------------------------------------------------- filesystem detection

_TRDOS_DISK_TYPE_NAMES = {0x16: "DS80", 0x17: "DS40", 0x18: "SS80", 0x19: "SS40"}


def track_payload(disk: DiskImage, cyl: int, head: int) -> bytes:
    """A track's sector bytes concatenated in stream order (ID-only sectors
    contribute nothing)."""
    trk = disk.get_track(cyl, head)
    if trk is None:
        return b""
    return b"".join(sec.data for sec in trk.sectors if not sec.no_data)


def signature_hits(disk: DiskImage, sig: bytes) -> list[int]:
    """Logical track numbers whose payload contains `sig`, whole disk."""
    hits = []
    for logical in range(disk.cylinders * disk.heads):
        cyl, head = divmod(logical, disk.heads)
        if sig in track_payload(disk, cyl, head):
            hits.append(logical)
    return hits


def cpm_entry_state(entry: bytes) -> str | None:
    """'used' / 'empty' (0xE5 filler) / None (not a directory entry). Names are
    CP/M 8+3 with attribute flags allowed in bit 7; RC/block bytes are only
    sanity-ranged because Profs banked variants shuffle them."""
    if entry[0] == 0xE5:
        return "empty"
    if entry[0] > 31:  # user number (label extents 0x10-0x1F included)
        return None
    for ch in entry[1:12]:
        if not (0x20 <= (ch & 0x7F) <= 0x7E):
            return None
    if entry[12] > 0x1F:  # extent
        return None
    return "used"


def find_cpm_directory(disk: DiskImage) -> tuple[int, list[str]] | None:
    """Scans every logical track for a CP/M directory block: at least 3 valid
    used FCB entries within the first 12 entries (directories start with live
    entries; text/code data starts with letters, which fail the user-number
    check immediately, so false positives are rare)."""
    for logical in range(disk.cylinders * disk.heads):
        cyl, head = divmod(logical, disk.heads)
        payload = track_payload(disk, cyl, head)
        if len(payload) < 12 * 32:
            continue
        used: list[str] = []
        for i in range(12):
            entry = payload[i * 32 : (i + 1) * 32]
            if cpm_entry_state(entry) == "used":
                stem = bytes(b & 0x7F for b in entry[1:9]).decode("ascii", errors="replace").strip()
                ext = bytes(b & 0x7F for b in entry[9:12]).decode("ascii", errors="replace").strip()
                text = f"{stem}.{ext}" if ext else stem
                if text not in used:
                    used.append(text)
        if len(used) >= 3:
            return logical, used[:6]
    return None


def detect_fat_boot(disk: DiskImage) -> str | None:
    """MS-DOS FAT boot sector (first 512 bytes of track 0/0): validated BPB plus
    the 0x55AA tag. Catches the FAT12 disks Profs PQDOS and friends use."""
    payload = track_payload(disk, 0, 0)[:512]
    if len(payload) < 512 or payload[510:512] != b"\x55\xAA":
        return None
    oem = payload[3:11]
    if not all(0x20 <= b <= 0x7E for b in oem):
        return None
    bps = payload[11] | (payload[12] << 8)
    spc = payload[13]
    num_fats = payload[16]
    media = payload[21]
    if bps not in (128, 256, 512, 1024) or spc not in (1, 2, 4, 8, 16, 32, 64):
        return None
    if num_fats not in (1, 2) or not (media == 0xF0 or 0xF8 <= media <= 0xFE):
        return None
    fat_type = payload[54:62].decode("ascii", errors="replace").strip()
    label = payload[43:54].decode("ascii", errors="replace").strip()
    detail = f"MS-DOS {fat_type or 'FAT'}: boot sector OEM '{oem.decode('ascii')}', {bps} B/sector, {spc} sectors/cluster"
    if label:
        detail += f", label '{label}'"
    return detail


def detect_filesystems(disk: DiskImage) -> list[tuple[str, str]]:
    """Every filesystem recognized on the disk, with the evidence for each.
    Ordering: TR-DOS first (most common here), then structural detections, then
    signature scans. Confidence is part of the evidence text."""
    found: list[tuple[str, str]] = []
    if trdos.is_trdos(disk):
        info = disk.sector_bytes(0, 0, 9) or bytes(trdos.SECTOR_SIZE)
        label = info[0xF5:0xFD].decode("ascii", errors="replace").rstrip()
        type_name = _TRDOS_DISK_TYPE_NAMES.get(info[0xE3])
        try:
            files = trdos.parse_catalog(disk)
            deleted = sum(1 for entry in files if entry.deleted)
            detail = f"catalog with {len(files)} file(s)"
            if deleted:
                detail += f", {deleted} deleted"
            if label:
                detail += f", label '{label}'"
            if type_name:
                detail += f", {type_name}"
            free = info[0xE5] | (info[0xE6] << 8)
            detail += f", {free} free sector(s)"
            found.append(("TR-DOS", detail))
        except DiskConversionError:
            found.append(("TR-DOS", "valid disk-info sector, catalog unreadable"))

    fat = detect_fat_boot(disk)
    if fat:
        found.append(("MS-DOS", fat))

    if not any(name == "TR-DOS" for name, _ in found):
        directory = find_cpm_directory(disk)
        if directory is not None:
            logical, names = directory
            cyl, head = divmod(logical, disk.heads)
            detail = f"directory at track {cyl}/{head}: {', '.join(names)}"
            banners = signature_hits(disk, b"CP/M")
            if banners:
                detail += f"; 'CP/M' banner at logical {fmt_numbers(set(banners[:8]))}"
            found.append(("CP/M", detail))
        else:
            banners = [t for t in signature_hits(disk, b"CP/M") if t <= 3]
            if banners:
                found.append(("CP/M", f"'CP/M' banner in system track(s) {fmt_numbers(set(banners))}, no directory found"))
            else:
                banners = signature_hits(disk, b"CP/M")
                if banners:
                    found.append(("CP/M (traces)", f"'CP/M' text in data area (logical {fmt_numbers(set(banners[:8]))}), no directory found"))

    for name, sigs in (("iS-DOS", (b"iS-DOS", b"IS-DOS")), ("NedoOS", (b"NedoOS", b"NEO-DOS", b"NEOFS"))):
        for sig in sigs:
            hits = signature_hits(disk, sig)
            if hits:
                where = fmt_numbers(set(hits[:8]))
                confidence = "likely" if hits[0] <= 1 else "traces"
                found.append((f"{name} ({confidence})", f"'{sig.decode()}' signature at logical track(s) {where}"))
                break

    return found


# ------------------------------------------------- protection analysis


def analyze_protection(disk: DiskImage, fs_names: list[str] | None = None) -> tuple[list[str], str]:
    """Collects copy-protection signals a WD1793-visible layout can carry and
    renders a verdict. Detection is purely structural - signal counts only, no
    title/string matching. (The signal taxonomy was validated against the
    fixtures discussed in docs/disasm/black-raven-voron-protection and the
    Zvezdnoe Nasledie dump, but no specific layout is named or assumed.)"""
    total_logical = disk.cylinders * disk.heads
    findings: list[str] = []
    strong = 0

    # ID/position mismatch: the head reads CHRN that does not match where it is.
    mismatch_tracks: list[int] = []
    example = ""
    for cyl, head in iter_selected_tracks(disk, None, None):
        trk = disk.get_track(cyl, head)
        if not trk or not trk.sectors:
            continue
        bad = [s for s in trk.sectors if s.cylinder != cyl or s.head != head]
        if bad:
            mismatch_tracks.append(cyl * disk.heads + head)
            if not example:
                s = bad[0]
                example = f"e.g. IDs claim C={s.cylinder} H={s.head} on physical {cyl}/{head}"
    if mismatch_tracks:
        strong += 1
        findings.append(
            f"ID/position mismatch on {len(mismatch_tracks)} track(s) (logical "
            f"{fmt_numbers(set(mismatch_tracks[:20]))}{'...' if len(mismatch_tracks) > 20 else ''}) - {example}"
        )

    # Tracks that are not plain 16x256 TR-DOS, and sectors numbered >= 0xC0:
    # reformatting data tracks with exotic CHRN is the classic loader check.
    nonstandard: list[int] = []
    nonstandard_desc: dict[str, int] = {}
    high_r: set[int] = set()
    dup_r_tracks: list[int] = []
    for cyl, head in iter_selected_tracks(disk, None, None):
        trk = disk.get_track(cyl, head)
        if trk is None:
            continue
        if trk.sectors and not trk.is_standard_trdos():
            logical = cyl * disk.heads + head
            nonstandard.append(logical)
            key = describe_track(trk, cyl, head)
            nonstandard_desc[key] = nonstandard_desc.get(key, 0) + 1
        for s in trk.sectors:
            if s.number >= 0xC0:
                high_r.add(s.number)
        if len({s.number for s in trk.sectors}) != len(trk.sectors):
            dup_r_tracks.append(cyl * disk.heads + head)
    if nonstandard:
        dominant, count = max(nonstandard_desc.items(), key=lambda item: item[1])
        fs_native = any(name.split(" (")[0] in ("CP/M", "MS-DOS") for name in (fs_names or []))
        if len(nonstandard) >= total_logical - 2:
            # Uniform whole-disk geometry is a machine's native format, not a
            # protection trick - a reformat-for-protection always leaves the
            # TR-DOS system track(s) readable (VORON1 keeps 0/1 standard).
            note = " (native geometry for the detected filesystem)" if fs_native else " (a machine-native format, not a protection trick)"
            findings.append(f"non-TR-DOS track geometry throughout: '{dominant}' ({count} track(s)){note}")
        else:
            strong += 1
            if count >= 3 or len(nonstandard_desc) <= 2:
                variants = f", {len(nonstandard_desc)} variant(s)" if len(nonstandard_desc) > 1 else ""
                findings.append(
                    f"non-TR-DOS track geometry on {len(nonstandard)} of {total_logical} track(s), "
                    f"mostly '{dominant}' ({count}){variants}"
                )
            else:
                findings.append(
                    f"non-TR-DOS track geometry on {len(nonstandard)} of {total_logical} track(s), "
                    f"spread across {len(nonstandard_desc)} different variant(s)"
                )
    if high_r:
        strong += 1
        findings.append(f"sector numbers R>=0xC0: {fmt_numbers(high_r)} (standard TR-DOS numbers are 1-16)")
    if dup_r_tracks:
        strong += 1
        findings.append(f"duplicate sector IDs within a track at logical {fmt_numbers(set(dup_r_tracks[:12]))}")

    # Per-sector flags a normal TR-DOS disk never carries.
    deleted: list[int] = []
    crc_errors: list[int] = []
    id_only: list[int] = []
    for cyl, head in iter_selected_tracks(disk, None, None):
        trk = disk.get_track(cyl, head)
        if not trk:
            continue
        for s in trk.sectors:
            logical = cyl * disk.heads + head
            if s.deleted:
                deleted.append(logical)
            if not s.crc_valid and not s.no_data:
                crc_errors.append(logical)
            if s.no_data:
                id_only.append(logical)
    if deleted:
        strong += 1
        findings.append(f"deleted (F8) data marks on logical {fmt_numbers(set(deleted[:12]))}")
    if crc_errors:
        strong += 1
        findings.append(
            f"data-CRC errors on logical {fmt_numbers(set(crc_errors[:12]))} (deliberately bad CRC, or media damage)"
        )
    if id_only:
        strong += 1
        findings.append(f"ID-only sectors (no data field) on logical {fmt_numbers(set(id_only[:12]))}")

    weak = 0
    if disk.cylinders > 80:
        weak += 1
        findings.append(f"{disk.cylinders} cylinders - more than a standard 80-track drive provides")

    formatted = [
        c * disk.heads + h for c, h in iter_selected_tracks(disk, None, None) if disk.get_track(c, h) and disk.get_track(c, h).sectors
    ]
    if formatted:
        inside_holes = [t for t in range(1, max(formatted)) if t not in set(formatted)]
        if inside_holes:
            weak += 1
            findings.append(f"unformatted tracks inside the used area: logical {fmt_numbers(set(inside_holes[:12]))}")

    if not findings:
        return [], "no protection signals - plain geometry, IDs match position, CRCs valid"
    if strong:
        return findings, f"likely copy-protected ({strong} strong signal type(s))"
    if weak:
        return findings, f"nothing conclusive ({weak} minor observation(s)) - could be protection or damage"
    return findings, "no protection signals - informational geometry notes only"


# --------------------------------------------------------------- brief mode

def print_brief(path: str, module, disk: DiskImage, with_tracks: bool) -> None:
    name = module.__name__.rsplit(".", 1)[-1].upper()
    total = disk.cylinders * disk.heads
    print(f"file:                 {path}")
    print(f"format:               {name} ({Path(path).stat().st_size} bytes)")
    print(f"geometry:             {disk.cylinders} cylinders x {disk.heads} heads = {total} tracks")
    print(f"write-protected:      {'yes' if disk.write_protected else 'no'}")
    print(f"standard TR-DOS geometry: {'yes (16x256, R=1-16)' if disk.is_standard_trdos_geometry() else 'no'}")

    groups: dict[str, list[tuple[int, int]]] = {}
    for cyl in range(disk.cylinders):
        for head in range(disk.heads):
            groups.setdefault(describe_track(disk.get_track(cyl, head), cyl, head), []).append((cyl, head))
    print("tracks:")
    for desc, tracks in groups.items():
        if disk.heads > 1:
            by_side = []
            for head in range(disk.heads):
                cyls = {cyl for cyl, h in tracks if h == head}
                if cyls:
                    by_side.append(f"side {head} cyl {fmt_numbers(cyls)}")
            print(f"  {'; '.join(by_side)}: {desc}")
        else:
            print(f"  cyl {fmt_numbers({cyl for cyl, _ in tracks})}: {desc}")

    if with_tracks:
        log_col = " log" if disk.heads > 1 else ""
        print(f"per-track table (logical = cylinder*heads + head):")
        print(f"  {'cyl':>3} {'side':>4}{log_col}  description")
        for cyl, head in iter_selected_tracks(disk, None, None):
            log = f" {cyl * disk.heads + head:>3}" if disk.heads > 1 else ""
            print(f"  {cyl:>3} {head:>4}{log}  {describe_track(disk.get_track(cyl, head), cyl, head)}")

    filesystems = detect_filesystems(disk)
    print("filesystems:")
    if filesystems:
        for name, evidence in filesystems:
            print(f"  {name}: {evidence}")
    else:
        print("  none recognized")

    findings, verdict = analyze_protection(disk, [name for name, _ in filesystems])
    print("protection analysis:")
    for finding in findings:
        print(f"  - {finding}")
    print(f"  verdict: {verdict}")


# --------------------------------------------------------------- sector dumps

def write_payload(out: str, payload: bytes) -> None:
    if out == "-":
        sys.stdout.buffer.write(payload)
        sys.stdout.buffer.flush()
    else:
        Path(out).write_bytes(payload)
        print(f"wrote {len(payload)} bytes to {out}", file=sys.stderr)


def dump_sectors(
    disk: DiskImage,
    cyl_sel: set[int] | None,
    head_sel: set[int] | None,
    sec_sel: set[int] | None,
    out_binary: str | None,
) -> None:
    """Sector data only: no ID fields, no flags - just what a plain sector read
    returns, hexdumped (or concatenated to a binary file with --out)."""
    payload = bytearray() if out_binary else None
    for cyl, head in iter_selected_tracks(disk, cyl_sel, head_sel):
        trk = disk.get_track(cyl, head)
        if trk is None or not trk.sectors:
            if not out_binary:
                print(f"--- track {cyl}/{head}: unformatted")
            continue
        for index, sec in enumerate(trk.sectors, 1):
            if sec_sel is not None and sec.number not in sec_sel:
                continue
            if out_binary is not None:
                if sec.no_data:
                    print(
                        f"note: track {cyl}/{head} sector R={sec.number} has no data field; skipped in binary output",
                        file=sys.stderr,
                    )
                    continue
                payload.extend(sec.data)
            else:
                print(f"--- track {cyl}/{head} sector {index}/{len(trk.sectors)} (R={sec.number}), {sec.data_size} bytes")
                if sec.no_data:
                    print("    (no data field)")
                else:
                    print(hexdump(sec.data))
    if out_binary is not None:
        write_payload(out_binary, payload)


def idam_line(sec: Sector) -> str:
    """The ID field as a WD1793 sees it: CHRN plus the CRC-16 bytes the field
    carries. crc_wd1793 returns the byte-swapped register, and the on-disk order
    is swapped-low then swapped-high (see the long note in mfm.crc_wd1793), so
    the two display bytes are value&0xFF then value>>8."""
    fields = bytes([mfm.IDAM, sec.cylinder, sec.head, sec.number, sec.size_code & 3])
    crc = mfm.crc_wd1793(fields)
    return (
        f"C={sec.cylinder} H={sec.head} R={sec.number} N={sec.size_code & 3} "
        f"({sec.data_size} B), id-crc {crc & 0xFF:02X}{(crc >> 8) & 0xFF:02X}"
    )


def dump_idam(
    disk: DiskImage,
    cyl_sel: set[int] | None,
    head_sel: set[int] | None,
    sec_sel: set[int] | None,
) -> None:
    """Sectors + IDAM: every sector's decoded ID field and data-address-mark
    state, followed by its data hexdump."""
    for cyl, head in iter_selected_tracks(disk, cyl_sel, head_sel):
        trk = disk.get_track(cyl, head)
        if trk is None or not trk.sectors:
            print(f"--- track {cyl}/{head}: unformatted")
            continue
        print(f"--- track {cyl}/{head}: {len(trk.sectors)} sector(s)")
        for index, sec in enumerate(trk.sectors, 1):
            if sec_sel is not None and sec.number not in sec_sel:
                continue
            dam = mfm.DAM_DELETED if sec.deleted else mfm.DAM_NORMAL
            if sec.no_data:
                data_crc = "no data field"
            elif sec.crc_valid:
                crc = mfm.crc_wd1793(bytes([dam]) + sec.data)
                data_crc = f"data-crc OK ({crc & 0xFF:02X}{(crc >> 8) & 0xFF:02X})"
            else:
                data_crc = "data-crc ERROR (stored CRC does not match the data)"
            dam_kind = "F8 (deleted)" if sec.deleted else "FB (normal)"
            print(f"  sector {index}/{len(trk.sectors)}  IDAM {idam_line(sec)}")
            print(f"    DAM {dam_kind}, {data_crc}")
            if not sec.no_data:
                print(hexdump(sec.data, "    "))


# --------------------------------------------------------------- raw mode

def extract_udi_raw_tracks(path: str) -> dict[tuple[int, int], bytes] | None:
    """Original per-track MFM byte streams straight out of a UDI file - the only
    input format that carries them. Mirrors udi.read's walk over the track table,
    minus sector decoding. Returns None for any non-UDI file (including the
    compressed 'udi!' variant, which module.read refuses before we get here)."""
    with open(path, "rb") as f:
        data = f.read()
    if not udi_format.detect(data[:16]):
        return None
    cylinders = data[9] + 1
    heads = data[10] + 1
    offset = 0x10 + struct.unpack_from("<I", data, 0x0C)[0]
    tracks: dict[tuple[int, int], bytes] = {}
    for cyl in range(cylinders):
        for head in range(heads):
            track_type = data[offset]
            tlen = struct.unpack_from("<H", data, offset + 1)[0]
            offset += 3
            if track_type in (udi_format.TRACK_TYPE_MFM, udi_format.TRACK_TYPE_FM):
                tracks[(cyl, head)] = data[offset : offset + tlen]
            offset += tlen + (tlen + 7) // 8  # stream + clock bitmap
    return tracks


def marker_lines(raw: bytes) -> list[str]:
    """Offsets of every A1 A1 A1 <mark> in a raw stream with the ID/data fields
    they introduce - enough to spot interleave, duplicated IDs and protection
    tricks at a glance."""
    lines: list[str] = []
    pending_size_code = 0
    for i in range(len(raw) - 3):
        if raw[i : i + 3] != b"\xA1\xA1\xA1":
            continue
        mark = raw[i + 3]
        if mark == mfm.IDAM and i + 8 <= len(raw):
            cyl, head, number, size_code = raw[i + 4 : i + 8]
            size_code &= 3
            pending_size_code = size_code
            lines.append(f"    offset {i:4d}: IDAM C={cyl} H={head} R={number} N={size_code} ({128 << size_code} B)")
        elif mark in (mfm.DAM_NORMAL, mfm.DAM_DELETED):
            kind = "DAM (deleted)" if mark == mfm.DAM_DELETED else "DAM"
            lines.append(f"    offset {i:4d}: {kind}, data {128 << pending_size_code} B")
    return lines


def dump_raw(
    path: str,
    module,
    disk: DiskImage,
    cyl_sel: set[int] | None,
    head_sel: set[int] | None,
    out_binary: str | None,
) -> None:
    """Raw tracks. UDI inputs get their original byte streams (sector gaps,
    interleave and all); every other format only has structured sectors, so the
    stream is synthesized by mfm.encode_track - same encoder UDI writing uses."""
    udi_raw = extract_udi_raw_tracks(path) if module is udi_format else None
    payload = bytearray() if out_binary else None
    for cyl, head in iter_selected_tracks(disk, cyl_sel, head_sel):
        if udi_raw is not None and (cyl, head) in udi_raw:
            raw = udi_raw[(cyl, head)]
            source = "original stream from UDI"
        else:
            trk = disk.get_track(cyl, head)
            raw = mfm.encode_track(trk.sectors if trk else []).raw
            source = "synthesized (gaps rebuilt, nominal 6250-byte revolution)"
        if out_binary is not None:
            payload.extend(raw)
            continue
        print(f"--- track {cyl}/{head}: {len(raw)} bytes, {source}")
        for line in marker_lines(raw):
            print(line)
        print(hexdump(raw))
    if out_binary is not None:
        write_payload(out_binary, payload)


# --------------------------------------------------------------- entry point

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", help="disk image to inspect (scl|trd|fdi|udi|td0)")
    parser.add_argument("--from", dest="from_format", metavar="FORMAT", help="force input format (scl|trd|fdi|udi|td0)")
    parser.add_argument("--dump", choices=("brief", "sectors", "idam", "raw"), default="brief", help="what to print (default: brief)")
    parser.add_argument("--tracks", action="store_true", help="brief mode: one line per physical track")
    parser.add_argument("--track", metavar="SPEC", help="select cylinders: N, N-M, comma lists (e.g. 0,2,5-7)")
    parser.add_argument("--side", metavar="SPEC", help="select heads/sides: N, N-M, comma lists")
    parser.add_argument("--sector", metavar="SPEC", help="select sector ID numbers R: N, N-M, comma lists")
    parser.add_argument("--out", metavar="FILE", help="write the payload as binary instead of hexdumping ('-' = stdout)")
    args = parser.parse_args(argv)

    try:
        module = formats.resolve_input(args.image, args.from_format)
        cyl_sel = parse_spec(args.track, "track")
        head_sel = parse_spec(args.side, "side")
        sec_sel = parse_spec(args.sector, "sector")
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if args.dump == "raw" and sec_sel is not None:
        print("error: --sector does not apply to --dump raw (a raw stream is whole-track); use --dump idam", file=sys.stderr)
        return 2
    if args.out and args.dump in ("brief", "idam"):
        print(f"error: --out needs a binary payload; --dump {args.dump} is text-only", file=sys.stderr)
        return 2

    try:
        disk = module.read(args.image)
    except (DiskConversionError, OSError) as e:
        print(f"error: reading {args.image}: {e}", file=sys.stderr)
        return 1

    warn_out_of_range(cyl_sel, range(disk.cylinders), "track")
    warn_out_of_range(head_sel, range(disk.heads), "side")
    if sec_sel is not None:
        known = {sec.number for trk in disk.tracks.values() for sec in trk.sectors}
        warn_out_of_range(sec_sel, known, "sector")

    if args.dump == "brief":
        print_brief(args.image, module, disk, args.tracks)
    elif args.dump == "sectors":
        dump_sectors(disk, cyl_sel, head_sel, sec_sel, args.out)
    elif args.dump == "idam":
        dump_idam(disk, cyl_sel, head_sel, sec_sel)
    else:
        dump_raw(args.image, module, disk, cyl_sel, head_sel, args.out)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        # `diskinfo.py ... | head` closing the pipe is not an error. Point stdout at
        # devnull so the interpreter's exit-time flush does not raise a second
        # BrokenPipeError, then report the conventional SIGPIPE status.
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        sys.exit(141)
