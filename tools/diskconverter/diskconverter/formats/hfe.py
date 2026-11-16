"""HFE - HxC Floppy Emulator bit-cell image, v1 ("HXCPICFE") and v3 ("HXCHFEV3").

A port of the C++ LoaderHFE (core/src/3rdparty/unreal-disk/src/loaders/loader_hfe.cpp): `parse` gives the
tracks C++ puts into its DiskImage, `serialize` (v3, what `zxdisk convert x.hfe` writes) and `serialize_v1`
give the bytes C++ writes for the same tracks.

Layout: a 512-byte header, a track LUT at block `lut_offset` (u16 block offset + u16 byte length per
cylinder, the length twice the longer side), then per cylinder 512-byte blocks holding 256 bytes of side 0 and
256 of side 1. Cells are LSB first in each byte. v3 adds opcodes (bytes F0..F4 after bit reversal), with the
Greaseweazle semantics: NOP / SETINDEX alone, SETBITRATE + 1 byte, SKIPBITS n + x (the low 8 - n bits of x,
weak when x is RAND), RAND (8 weak cells).

The tracks are written at 250 kbit/s, FM doubled (each FM cell as "0c"), so one file holds FM and MFM tracks;
the reader tries MFM and both FM phases on each side and keeps the decode with the most sectors (the header
encoding first on a tie).
"""

from __future__ import annotations

import re

from .. import flux
from ..disk_image import Track
from ..flux import FluxDisk, RawTrackData

SIGNATURE_V1 = b"HXCPICFE"
SIGNATURE_V3 = b"HXCHFEV3"
HEADER_SIZE = 512
BLOCK_SIZE = 512
INTERLEAVE_SIZE = 256
MAX_CYLINDERS = 86

ENCODING_ISOIBM_MFM = 0
ENCODING_AMIGA_MFM = 1
ENCODING_ISOIBM_FM = 2
ENCODING_EMU_FM = 3
ENCODING_UNKNOWN = 0xFF

OP_NOP = 0xF0
OP_SETINDEX = 0xF1
OP_SETBITRATE = 0xF2
OP_SKIPBITS = 0xF3
OP_RAND = 0xF4

WRITE_BIT_RATE = 250
WRITE_RATE_OPCODE_VALUE = 72

_OPCODE = re.compile(rb"[\xf0-\xf4]")
_HIGH_NIBBLE_F = re.compile(rb"[\xf0-\xff]")
_WEAK_RUN = re.compile("1+")


def detect(data: bytes) -> int:
    """3 for v3, 1 for v1, 0 when not HFE"""
    if data[:8] == SIGNATURE_V3:
        return 3
    if data[:8] == SIGNATURE_V1:
        return 1
    return 0


def _u16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def _deinterleave(data: bytes, track_start: int, track_length: int) -> tuple[bytes, bytes]:
    """LoaderHFE::deinterleave: track_length // 2 bytes a side, reads bounded by the file"""
    sides = (bytearray(), bytearray())
    per_side = track_length // 2
    block = 0
    while block * INTERLEAVE_SIZE < per_side:
        count = min(INTERLEAVE_SIZE, per_side - block * INTERLEAVE_SIZE)
        for side in (0, 1):
            start = track_start + block * BLOCK_SIZE + side * INTERLEAVE_SIZE
            sides[side].extend(data[start:start + count])
        block += 1
    return bytes(sides[0]), bytes(sides[1])


def _process_v3(raw: bytes) -> tuple[str, str]:
    """LoaderHFE::processV3Opcodes on bit-reversed bytes: (cells, weak flag per cell)"""
    cells: list[str] = []
    weak: list[str] = []
    pos = 0
    end = len(raw)
    while pos < end:
        match = _OPCODE.search(raw, pos)
        stop = match.start() if match else end
        if stop > pos:
            chunk = flux.unpack_msb_first(raw[pos:stop])
            cells.append(chunk)
            weak.append("0" * len(chunk))
        if not match:
            break
        op = raw[stop]
        pos = stop + 1
        if op == OP_SETBITRATE:
            pos += 1
        elif op == OP_SKIPBITS:
            if pos + 2 > end:
                break
            count = 8 - (raw[pos] & 7)
            value = raw[pos + 1]
            pos += 2
            if value == OP_RAND:
                cells.append("0" * count)
                weak.append("1" * count)
            else:
                cells.append(format(value & ((1 << count) - 1), f"0{count}b"))
                weak.append("0" * count)
        elif op == OP_RAND:
            cells.append("0" * 8)
            weak.append("1" * 8)
    return "".join(cells), "".join(weak)


def _sector_count(data: RawTrackData) -> int:
    track = Track()
    track.set_stream(data.raw, data.clock, data.weak, data.encoding)
    return track.sector_count()


def _decode_side(cells: str, weak: str, header_fm: bool, single_rate_fm: bool) -> RawTrackData:
    """LoaderHFE::decodeSide"""
    if not cells:
        return flux.blank_track("fm" if header_fm else "mfm")

    def decode_mfm() -> RawTrackData:
        raw, clock, weak_bytes = flux.mfm_decode_weak(cells, weak)
        return flux.normalize_track("mfm", raw, clock, weak_bytes)

    def decode_fm(step: int, phase: int) -> RawTrackData:
        if step == 1:
            fm_cells, fm_weak = cells, weak
        else:
            fm_cells = cells[phase::2]
            if phase == 1:
                fm_weak = flux.or_cells(weak[1::2], weak[0::2][:len(fm_cells)])
            else:
                fm_weak = flux.or_cells(weak[0::2], ("0" + weak[1::2])[:len(fm_cells)])
        raw, clock, weak_bytes = flux.fm_decode_weak(fm_cells, fm_weak)
        return flux.normalize_track("fm", raw, clock, weak_bytes)

    if single_rate_fm:
        candidates = (lambda: decode_fm(1, 0), decode_mfm)
    elif header_fm:
        candidates = (lambda: decode_fm(2, 1), lambda: decode_fm(2, 0), decode_mfm)
    else:
        candidates = (decode_mfm, lambda: decode_fm(2, 1), lambda: decode_fm(2, 0))

    best = None
    best_sectors = 0
    for make in candidates:
        track = make()
        sectors = _sector_count(track)
        if best is None or sectors > best_sectors:
            best, best_sectors = track, sectors
    return best


def parse(data: bytes) -> FluxDisk:
    """LoaderHFE::parse. Raises ValueError with the C++ warning when C++ refuses the file."""
    version = detect(data)
    if version == 0:
        raise ValueError("Not an HFE file: signature 'HXCPICFE' or 'HXCHFEV3' missing")
    if len(data) < HEADER_SIZE:
        raise ValueError("HFE file is truncated (shorter than the header)")

    warnings: list[str] = []
    tracks_count = data[0x09]
    sides = data[0x0A]
    encoding_byte = data[0x0B]
    bit_rate = _u16(data, 0x0C)
    lut_offset = _u16(data, 0x12)
    meta = {
        "version": version,
        "format_revision": data[0x08],
        "encoding_byte": encoding_byte,
        "bit_rate": bit_rate,
        "rpm": _u16(data, 0x0E),
        "interface_mode": data[0x10],
        "write_allowed": data[0x14] != 0,
        "single_step": data[0x15] != 0,
    }

    if tracks_count == 0 or tracks_count > MAX_CYLINDERS:
        raise ValueError(f"HFE declares {tracks_count} tracks, supported range is 1..{MAX_CYLINDERS}")
    if sides == 0 or sides > 2:
        raise ValueError(f"HFE declares {sides} sides, only 1 or 2 are supported")
    if lut_offset == 0:
        raise ValueError("HFE track LUT offset is zero")
    lut_start = lut_offset * BLOCK_SIZE
    if lut_start + tracks_count * 4 > len(data):
        raise ValueError("HFE track LUT runs past the end of the file")

    header_fm = False
    if encoding_byte in (ENCODING_ISOIBM_FM, ENCODING_EMU_FM):
        header_fm = True
    elif encoding_byte not in (ENCODING_ISOIBM_MFM, ENCODING_AMIGA_MFM, ENCODING_UNKNOWN):
        warnings.append(f"HFE encoding {encoding_byte} is not supported, treated as MFM")
    # FM recorded at its own cell rate (125 kbit/s and below) rather than doubled at 250
    single_rate_fm = header_fm and bit_rate != 0 and bit_rate <= 150

    tracks: dict = {}
    for cylinder in range(tracks_count):
        entry = lut_start + cylinder * 4
        track_start = _u16(data, entry) * BLOCK_SIZE
        track_length = _u16(data, entry + 2)
        if track_start + track_length > len(data):
            raise ValueError(f"HFE track {cylinder} data runs past the end of the file")
        side_data = _deinterleave(data, track_start, track_length)

        for side in range(sides):
            if version == 3:
                cells, weak = _process_v3(flux.bit_reverse_bytes(side_data[side]))
            else:
                cells = flux.unpack_lsb_first(side_data[side])
                weak = "0" * len(cells)
            tracks[(cylinder, side)] = _decode_side(cells, weak, header_fm, single_rate_fm)

    return FluxDisk(tracks_count, sides, tracks, warnings, meta)


def read(path: str) -> FluxDisk:
    with open(path, "rb") as handle:
        return parse(handle.read())


def _check_geometry(tracks: dict, cylinders: int, sides: int) -> None:
    if cylinders == 0 or cylinders > MAX_CYLINDERS or sides == 0 or sides > 2:
        raise ValueError("HFE save refused: invalid disk geometry")
    for cylinder in range(cylinders):
        for side in range(sides):
            if (cylinder, side) not in tracks:
                raise ValueError(f"HFE save refused: track cylinder {cylinder} side {side} is missing")


def _encode_side(track: RawTrackData) -> tuple[str, str]:
    """LoaderHFE::encodeSide: cells at the write rate (FM doubled, c -> "0c") and a weak flag per cell"""
    cells = flux.encode(track.encoding, track.raw, track.clock)
    factor = 1
    if track.encoding == "fm":
        cells = cells.replace("1", "x").replace("0", "00").replace("x", "01")
        factor = 2
    if not track.weak or not any(track.weak):
        return cells, ""
    span = 16 * factor
    weak = bytearray(b"0" * len(cells))
    for offset in range(len(track.raw)):
        slot = offset >> 3
        if slot < len(track.weak) and (track.weak[slot] >> (offset & 7)) & 1:
            start = offset * span
            stop = min(len(cells), start + span)
            weak[start:stop] = b"1" * (stop - start)
    return cells, weak.decode()


def _emit_plain(cells: str, start: int, stop: int, out: bytearray) -> None:
    pos = start
    while stop - pos >= 8:
        count = (stop - pos) // 8
        packed = flux.pack_msb_first(cells[pos:pos + 8 * count])
        match = _HIGH_NIBBLE_F.search(packed)
        taken = match.start() if match else count
        out += packed[:taken]
        pos += 8 * taken
        if match:
            # would read as an opcode: send seven cells
            out += bytes((OP_SKIPBITS, 1, packed[taken] >> 1))
            pos += 7
    if stop > pos:
        out += bytes((OP_SKIPBITS, 8 - (stop - pos), int(cells[pos:stop], 2)))


def _emit_weak(count: int, out: bytearray) -> None:
    out += bytes((OP_RAND,)) * (count // 8)
    if count % 8:
        out += bytes((OP_SKIPBITS, 8 - count % 8, OP_RAND))


def _emit_v3(cells: str, weak: str) -> bytes:
    """LoaderHFE::emitV3: SETBITRATE first, then 8 cells a byte (first in bit 7), weak runs as RAND"""
    out = bytearray((OP_SETBITRATE, WRITE_RATE_OPCODE_VALUE))
    pos = 0
    for run in _WEAK_RUN.finditer(weak):
        _emit_plain(cells, pos, run.start(), out)
        _emit_weak(run.end() - run.start(), out)
        pos = run.end()
    _emit_plain(cells, pos, len(cells), out)
    return bytes(out)


def _serialize(tracks: dict, cylinders: int, sides: int, version: int, rpm: int, interface_mode: int,
               write_allowed: bool, single_step: bool) -> bytes:
    _check_geometry(tracks, cylinders, sides)
    all_fm = all(tracks[(c, s)].encoding == "fm" for c in range(cylinders) for s in range(sides))

    out = bytearray(b"\xff" * HEADER_SIZE)
    out[0:8] = SIGNATURE_V3 if version == 3 else SIGNATURE_V1
    out[0x08] = 0
    out[0x09] = cylinders
    out[0x0A] = sides
    out[0x0B] = ENCODING_ISOIBM_FM if all_fm else ENCODING_ISOIBM_MFM
    out[0x0C] = WRITE_BIT_RATE & 0xFF
    out[0x0D] = WRITE_BIT_RATE >> 8
    out[0x0E] = rpm & 0xFF
    out[0x0F] = (rpm >> 8) & 0xFF
    out[0x10] = interface_mode & 0xFF
    out[0x11] = 1
    out[0x12] = 1
    out[0x13] = 0
    out[0x14] = 0xFF if write_allowed else 0
    out[0x15] = 0xFF if single_step else 0

    lut_start = HEADER_SIZE
    lut_size = cylinders * 4
    out += b"\xff" * (((lut_size + BLOCK_SIZE - 1) // BLOCK_SIZE) * BLOCK_SIZE)
    pad = flux.bit_reverse_bytes(bytes((OP_NOP,))) if version == 3 else b"\0"

    for cylinder in range(cylinders):
        side_bytes = [b"", b""]
        for side in range(sides):
            cells, weak = _encode_side(tracks[(cylinder, side)])
            if version == 3:
                side_bytes[side] = flux.bit_reverse_bytes(_emit_v3(cells, weak))
            else:
                side_bytes[side] = flux.pack_lsb_first(cells)
        longest = max(len(side_bytes[0]), len(side_bytes[1]))
        if longest * 2 > 0xFFFF:
            raise ValueError(f"HFE save refused: cylinder {cylinder} needs {longest * 2} bytes, the LUT holds 65535")
        blocks = (longest + INTERLEAVE_SIZE - 1) // INTERLEAVE_SIZE
        side0 = side_bytes[0].ljust(blocks * INTERLEAVE_SIZE, pad)
        side1 = side_bytes[1].ljust(blocks * INTERLEAVE_SIZE, pad)
        interleaved = bytearray()
        for block in range(blocks):
            lo = block * INTERLEAVE_SIZE
            interleaved += side0[lo:lo + INTERLEAVE_SIZE]
            interleaved += side1[lo:lo + INTERLEAVE_SIZE]

        track_offset = len(out) // BLOCK_SIZE
        track_length = longest * 2
        entry = lut_start + cylinder * 4
        out[entry] = track_offset & 0xFF
        out[entry + 1] = (track_offset >> 8) & 0xFF
        out[entry + 2] = track_length & 0xFF
        out[entry + 3] = (track_length >> 8) & 0xFF
        out += interleaved
    return bytes(out)


def serialize(tracks: dict, cylinders: int, sides: int, *, rpm: int = 300, interface_mode: int = 7,
              write_allowed: bool = True, single_step: bool = True) -> bytes:
    """LoaderHFE::serialize (v3, weak bytes as RAND). The keyword defaults are a fresh C++ loader's, which is what
    a conversion uses."""
    return _serialize(tracks, cylinders, sides, 3, rpm, interface_mode, write_allowed, single_step)


def serialize_v1(tracks: dict, cylinders: int, sides: int, *, rpm: int = 300, interface_mode: int = 7,
                 write_allowed: bool = True, single_step: bool = True) -> bytes:
    """LoaderHFE::serializeV1 (weak bits dropped, as in C++)"""
    return _serialize(tracks, cylinders, sides, 1, rpm, interface_mode, write_allowed, single_step)


def write(path: str, tracks: dict, cylinders: int, sides: int, **header) -> None:
    """LoaderHFE::writeImage: v3"""
    data = serialize(tracks, cylinders, sides, **header)
    with open(path, "wb") as handle:
        handle.write(data)
