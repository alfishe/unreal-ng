"""SCP - SuperCard Pro flux image. See docs/file-formats/disk-images/scp/.

A port of the C++ LoaderSCP (core/src/3rdparty/unreal-disk/src/loaders/loader_scp.cpp): `parse` gives the
tracks C++ puts into its DiskImage (PLL + MFM/FM decoder per revolution, up to 5 revolutions merged by a
per-byte majority vote, disagreements marked weak, each clock mark from the first revolution that decoded the
voted byte), `serialize` gives the bytes C++ writes (one revolution of ideal flux per track at 25 ns a tick, no
weak information; at most 84 cylinders, the size of the track table).
"""

from __future__ import annotations

import struct

from .. import flux
from ..flux import FluxDisk, FluxPll, RawTrackData

SIGNATURE = b"SCP"
HEADER_SIZE = 16
TRACK_TABLE_OFFSET = 0x10
TRACK_TABLE_ENTRIES = 168
TRACK_HEADER_SIZE = 4
REV_HEADER_SIZE = 12

FLAG_INDEX = 0x01
FLAG_96TPI = 0x02
FLAG_NORMALISED = 0x08

HEADS_BOTH = 0
HEADS_SIDE0 = 1
NS_PER_TICK = 25
MAX_CYLINDERS = 86
MAX_REVOLUTIONS = 5


def detect(data: bytes) -> bool:
    return data[:3] == SIGNATURE


def _u32(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)


def _flux_intervals(data: bytes, offset: int, count: int, ns_per_tick: int = NS_PER_TICK) -> list[int]:
    """LoaderSCP::parseFluxData: big-endian u16 ticks, 0 adds 65536 to the next one, x ns_per_tick (uint32)"""
    ticks = struct.unpack_from(f">{count}H", data, offset)
    if 0 not in ticks:
        return [(t * ns_per_tick) & 0xFFFFFFFF for t in ticks]
    out = []
    accumulated = 0
    for t in ticks:
        if t == 0:
            accumulated = (accumulated + 65536) & 0xFFFFFFFF
        else:
            out.append((((accumulated + t) & 0xFFFFFFFF) * ns_per_tick) & 0xFFFFFFFF)
            accumulated = 0
    return out


def _decode_revolution(intervals: list[int], encoding: str, index_ns: int) -> tuple[bytes, bytes]:
    """LoaderSCP::decodeTrack: the index time past the flux gives the trailing zero cells"""
    if not intervals:
        return b"", b""
    pll = FluxPll(encoding)
    pll.add_intervals(intervals)
    flux_ns = sum(intervals)
    if index_ns > flux_ns:
        pll.add_trailing_time(min(index_ns - flux_ns, 0xFFFFFFFF))
    return flux.decode(encoding, pll.bit_cells())


def _merge(revolutions: list[bytes]) -> tuple[bytes, list[int]]:
    """LoaderSCP::mergeRevolutions: per byte the most frequent value (ties: the lowest value), weak where any
    revolution that reaches the byte disagrees"""
    max_len = max(len(r) for r in revolutions)
    merged = bytearray(max_len)
    weak: list[int] = []
    for i in range(max_len):
        values = [r[i] for r in revolutions if i < len(r)]
        first = values[0]
        if all(v == first for v in values):
            merged[i] = first
            continue
        counts: dict[int, int] = {}
        for v in values:
            counts[v] = counts.get(v, 0) + 1
        best = max(counts.values())
        merged[i] = min(v for v, n in counts.items() if n == best)
        weak.append(i)
    return bytes(merged), weak


def _merge_clock_marks(rev_bytes: list[bytes], rev_clocks: list[bytes], merged: bytes) -> bytes:
    """LoaderSCP::mergeClockMarks: byte i's mark from the first revolution that decoded the voted byte"""
    clock = bytearray((len(merged) + 7) // 8)
    for i, value in enumerate(merged):
        for rev, marks in zip(rev_bytes, rev_clocks):
            if i >= len(rev) or rev[i] != value:
                continue
            if (i >> 3) < len(marks) and (marks[i >> 3] >> (i & 7)) & 1:
                clock[i >> 3] |= 1 << (i & 7)
            break
    return bytes(clock)


def parse(data: bytes) -> FluxDisk:
    """LoaderSCP::parse. Raises ValueError with the C++ warning when C++ refuses the file."""
    if not detect(data):
        raise ValueError("Not an SCP file: signature 'SCP' missing")
    if len(data) < HEADER_SIZE + TRACK_TABLE_ENTRIES * 4:
        raise ValueError("SCP file is truncated (shorter than header + track table)")

    warnings: list[str] = []
    revolutions = data[5]
    start_track = data[6]
    end_track = data[7]
    meta = {
        "version": data[3],
        "disk_type": data[4],
        "revolutions": revolutions,
        "flags": data[8],
        "bit_cell_width": data[9],
        "heads": data[10],
        "resolution": data[11],
    }
    if revolutions == 0:
        raise ValueError("SCP declares 0 revolutions per track")
    if revolutions > MAX_REVOLUTIONS:
        warnings.append(f"SCP declares {revolutions} revolutions, only the first {MAX_REVOLUTIONS} will be used")
    if end_track < start_track:
        raise ValueError("SCP end track is before start track")

    ns_per_tick = NS_PER_TICK * (meta["resolution"] + 1)
    # The track table has 168 entries (84 cylinders x 2 sides): numbers past it have no entry
    last_track = end_track
    if last_track >= TRACK_TABLE_ENTRIES:
        warnings.append(f"SCP end track {end_track} is past the {TRACK_TABLE_ENTRIES}-entry track table, "
                        f"read up to {TRACK_TABLE_ENTRIES - 1}")
        last_track = TRACK_TABLE_ENTRIES - 1

    length = len(data)
    max_cylinder = 0
    max_side = 0
    parsed: list[tuple[int, list[tuple[list[int], int]]]] = []
    for t in range(start_track, last_track + 1):
        entry = TRACK_TABLE_OFFSET + t * 4
        track_offset = _u32(data, entry) if entry + 4 <= length else 0
        if track_offset == 0:
            continue
        if track_offset + TRACK_HEADER_SIZE > length:
            warnings.append(f"SCP track {t} offset is outside the file")
            continue
        if data[track_offset:track_offset + 3] != b"TRK":
            warnings.append(f"SCP track {t} missing 'TRK' signature")
            continue
        track_number = data[track_offset + 3]
        max_cylinder = max(max_cylinder, track_number // 2)
        max_side = max(max_side, track_number % 2)

        revs: list[tuple[list[int], int]] = []
        for r in range(min(revolutions, MAX_REVOLUTIONS)):
            rev_header = track_offset + TRACK_HEADER_SIZE + r * REV_HEADER_SIZE
            if rev_header + REV_HEADER_SIZE > length:
                warnings.append(f"SCP track {t} revolution {r} header is outside the file")
                break
            index_ns = _u32(data, rev_header) * ns_per_tick
            flux_count = _u32(data, rev_header + 4)
            data_offset = _u32(data, rev_header + 8)
            flux_start = track_offset + data_offset
            if flux_start + flux_count * 2 > length:
                warnings.append(f"SCP track {t} revolution {r} flux data is outside the file")
                break
            revs.append((_flux_intervals(data, flux_start, flux_count, ns_per_tick), index_ns))
        parsed.append((track_number, revs))

    if not parsed:
        raise ValueError("SCP file contains no valid tracks")

    cylinders = max_cylinder + 1
    sides = max_side + 1
    if cylinders > MAX_CYLINDERS:
        raise ValueError(f"SCP image has {cylinders} cylinders, maximum is {MAX_CYLINDERS}")

    tracks: dict = {}
    for track_number, revs in parsed:
        key = (track_number // 2, track_number % 2)
        if not revs:
            tracks[key] = flux.blank_track("mfm")
            continue
        encoding = FluxPll.detect_mode(revs[0][0]) if revs[0][0] else "mfm"
        decoded = [_decode_revolution(intervals, encoding, index_ns) for intervals, index_ns in revs]
        rev_bytes = [d[0] for d in decoded]
        weak: list[int] = []
        if len(rev_bytes) == 1:
            merged = rev_bytes[0]
        else:
            merged, weak = _merge(rev_bytes)
        if not merged:
            tracks[key] = flux.blank_track(encoding)
            continue
        rev_clocks = [d[1] for d in decoded]
        clock = rev_clocks[0] if len(rev_bytes) == 1 else _merge_clock_marks(rev_bytes, rev_clocks, merged)
        tracks[key] = flux.normalize_track(encoding, merged, clock, weak)

    if (meta["flags"] & FLAG_96TPI) and cylinders <= 42:
        warnings.append("SCP image has 96 TPI flag set with <= 42 cylinders: may need double-stepping")
    return FluxDisk(cylinders, sides, tracks, warnings, meta)


def read(path: str) -> FluxDisk:
    with open(path, "rb") as handle:
        return parse(handle.read())


def _flux_ticks(cells: str, ticks_per_cell: int) -> tuple[list[int], int]:
    """LoaderSCP::encodeTrack after the encoder: one value per '1' cell (cells since the last one x ticks), values
    >= 65536 split into 0 entries plus the remainder; the zeros after the last '1' are only time to the index
    (returned as trailing ticks)"""
    runs = cells.split("1")
    # runs[k] (k < last) are the zeros before the k-th '1'; the last run is the trailing zeros
    trailing = len(runs.pop()) * ticks_per_cell
    lengths = list(map((1).__add__, map(len, runs)))
    if not lengths:
        return [], trailing
    if max(lengths) * ticks_per_cell < 65536:
        return list(map(ticks_per_cell.__mul__, lengths)), trailing
    out: list[int] = []
    for cell_count in lengths:
        total = cell_count * ticks_per_cell
        while total >= 65536:
            out.append(0)
            total -= 65536
        if total > 0:
            out.append(total)
    return out, trailing


def serialize(tracks: dict, cylinders: int, sides: int, *, version: int = 0x20, disk_type: int = 0,
              flags: int = FLAG_INDEX | FLAG_NORMALISED, bit_cell_width: int = 0) -> bytes:
    """LoaderSCP::serialize. The keyword defaults are a fresh C++ loader's, which is what a conversion uses.
    Tracks are numbered cylinder * 2 + side (single-sided too), the resolution byte is 0 (25 ns ticks), the
    index time is the sum of the track's flux."""
    if cylinders == 0 or cylinders > MAX_CYLINDERS or sides == 0 or sides > 2:
        raise ValueError("SCP save refused: invalid disk geometry")
    if cylinders > TRACK_TABLE_ENTRIES // 2:
        raise ValueError(f"SCP save refused: {cylinders} cylinders, the SCP track table holds "
                         f"{TRACK_TABLE_ENTRIES // 2}")
    end_track = (cylinders - 1) * 2 + (sides - 1)
    is_80_track = cylinders >= 70

    out = bytearray(b"SCP")
    out += bytes([version & 0xFF, disk_type & 0xFF, 1, 0, end_track,
                  (flags | (FLAG_96TPI if is_80_track else 0)) & 0xFF, bit_cell_width & 0xFF,
                  HEADS_SIDE0 if sides == 1 else HEADS_BOTH, 0])
    out += bytes(4)
    table_start = len(out)
    out += bytes(TRACK_TABLE_ENTRIES * 4)

    for cylinder in range(cylinders):
        for side in range(sides):
            track: RawTrackData | None = tracks.get((cylinder, side))
            if track is None:
                raise ValueError(f"SCP save refused: track cylinder {cylinder} side {side} is missing")
            track_number = (cylinder * 2 + side) & 0xFF
            track_offset = len(out)
            entry = table_start + track_number * 4
            out[entry:entry + 4] = struct.pack("<I", track_offset & 0xFFFFFFFF)
            out += b"TRK" + bytes([track_number])

            fm = track.encoding == "fm"
            cells = flux.encode(track.encoding, track.raw, track.clock)
            nominal_cell_ns = flux.FM_CELL_NS if fm else flux.MFM_CELL_NS
            ticks, trailing = _flux_ticks(cells, nominal_cell_ns // NS_PER_TICK)
            index_ticks = (trailing + sum(ticks) + 65536 * ticks.count(0)) & 0xFFFFFFFF
            out += struct.pack("<III", index_ticks, len(ticks), TRACK_HEADER_SIZE + REV_HEADER_SIZE)
            out += struct.pack(f">{len(ticks)}H", *ticks)

    checksum = sum(memoryview(out)[HEADER_SIZE:]) & 0xFFFFFFFF
    out[12:16] = struct.pack("<I", checksum)
    return bytes(out)


def write(path: str, tracks: dict, cylinders: int, sides: int, **header) -> None:
    """LoaderSCP::writeImage"""
    data = serialize(tracks, cylinders, sides, **header)
    with open(path, "wb") as handle:
        handle.write(data)
