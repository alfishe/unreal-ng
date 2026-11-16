"""`zxdisk dump` in Python: a floppy as the model holds it, as the same JSON text.

The output is byte for byte what `zxdisk dump <image>` prints (unreal-disk's tools/zxdisk/zxdisk.cpp Dump): the
format, the geometry, the loader's warnings, then one line per track with its encoding, length, raw stream, clock
bitmap and weak bitmap (hex) and the sectors indexed in it (C, H, R, raw N, IDAM / DAM / data offsets, size, flags,
the stored CRCs).
"""

from __future__ import annotations

from .disk_image import FM, NO_OFFSET, DiskImage


def json_string(text: str) -> str:
    """zxdisk JsonString: '"' and '\\' escaped, control characters as \\u00xx, everything else verbatim"""
    out = ['"']
    for ch in text:
        if ch in ('"', "\\"):
            out.append("\\" + ch)
        elif ord(ch) < 0x20:
            out.append(f"\\u{ord(ch):04x}")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def _offset(value: int) -> int:
    return -1 if value == NO_OFFSET else value


def _flag(value: bool) -> str:
    return "true" if value else "false"


def dump_track(cylinder: int, side: int, track) -> str:
    weak = track.weak_bitmap().hex() if track.has_weak_bits() else ""
    parts = [f'{{"c":{cylinder},"h":{side},"encoding":"{"fm" if track.encoding == FM else "mfm"}",'
             f'"length":{track.length},"raw":"{bytes(track.raw).hex()}","clock":"{bytes(track.clock).hex()}",'
             f'"weak":"{weak}","sectors":[']
    sectors = []
    for sector in track.index:
        data_valid = sector.has_data and sector.is_data_crc_valid()
        sectors.append(
            f'{{"c":{sector.cylinder},"h":{sector.head},"r":{sector.number},"n":{sector.size_code},'
            f'"idam":{_offset(sector.idam)},"dam":{_offset(sector.dam)},"data":{_offset(sector.data)},'
            f'"size":{sector.size},"has_data":{_flag(sector.has_data)},"deleted":{_flag(sector.deleted)},'
            f'"id_crc_valid":{_flag(sector.is_id_crc_valid())},"data_crc_valid":{_flag(data_valid)},'
            f'"id_crc":{sector.id_crc},"data_crc":{sector.data_crc() if sector.has_data else 0}}}')
    parts.append(",".join(sectors))
    parts.append("]}")
    return "".join(parts)


def dump(disk: DiskImage, fmt: str | None = None, warnings: list[str] | None = None) -> str:
    """The JSON text zxdisk dump prints for the disk (with the trailing newline)"""
    fmt = disk.format if fmt is None else fmt
    warnings = disk.warnings if warnings is None else warnings
    cylinders, sides = disk.cylinders & 0xFF, disk.heads & 0xFF
    out = [f'{{"format":{json_string(fmt or "")},"cylinders":{cylinders},"sides":{sides},"warnings":[',
           ",".join(json_string(w) for w in warnings), '],"tracks":[']
    tracks = []
    for cylinder in range(cylinders):
        for side in range(sides):
            track = disk.get_track(cylinder, side)
            if track is not None:
                tracks.append("\n" + dump_track(cylinder, side, track))
    out.append(",".join(tracks))
    out.append("\n]}\n")
    return "".join(out)
