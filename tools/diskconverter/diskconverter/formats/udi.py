"""UDI - Ultra Disk Image v1.0 (port of unreal-disk src/loaders/loader_udi.cpp).

Each track is stored as it is held: type (0 MFM, 1 FM; 2 mixed loads as MFM), length, raw bytes, clock-mark
bitmap (and a second bitmap for mixed tracks, skipped). Weak bytes ride in an optional "UDIW" chunk after the
tracks (records of cylinder, side, flags, offset, length), which reading applies and strips; writing regenerates
it from the tracks. A wrong file CRC refuses the load (LoaderUDI default). Writing uses a fresh loader's header
(reserved byte 0, no extended header, no trailer), as `zxdisk convert` writes it.
"""

from __future__ import annotations

import struct

from ..disk_image import FM, MAX_CYLINDERS, MAX_TRACK_SIZE, MIN_TRACK_SIZE, MFM, DiskConversionError, DiskImage

SIGNATURE = b"UDI!"
SIGNATURE_COMPRESSED = b"udi!"
HEADER_SIZE = 16
VERSION = 0
TRACK_TYPE_MFM = 0
TRACK_TYPE_FM = 1
TRACK_TYPE_MIXED = 2
TRACK_TYPE_MULTIREV_FLAG = 0x80
WEAK_CHUNK_SIGNATURE = b"UDIW"
WEAK_CHUNK_VERSION = 1
WEAK_CHUNK_HEADER_SIZE = 8
WEAK_CHUNK_RECORD_SIZE = 7


def detect(data: bytes) -> bool:
    return data[:4] in (SIGNATURE, SIGNATURE_COMPRESSED)


def _udi_table() -> list[int]:
    """One CRCHelper::crcUDI byte step on a low byte with the high bits clear: the arithmetic shift only
    sign-extends the high bits, so a step is sar(crc, 8) ^ table[low byte] (see crc_udi)"""
    table = []
    for low in range(256):
        crc = low
        for _ in range(8):
            temp = 0xFFFFFFFF if crc & 1 else 0
            crc = (crc >> 1) | (crc & 0x80000000)
            crc ^= 0xEDB88320 & temp
        table.append(crc)
    return table


_UDI_TABLE = _udi_table()


def crc_udi(data) -> int:
    """CRCHelper::crcUDI from crc = -1 (CRC-32, poly 0xEDB88320, with a signed accumulator: `crc >>= 1` shifts
    arithmetically). Per byte: crc ^= ~b; 8 rounds; crc ^= -1, which folds to crc = sar(crc, 8) ^ T[(crc ^ b ^ 0xFF)
    & 0xFF] with T the 8 rounds on a single low byte."""
    table = _UDI_TABLE
    crc = 0xFFFFFFFF
    for byte in data:
        crc = ((crc >> 8) | (0xFF000000 if crc & 0x80000000 else 0)) ^ table[(crc ^ byte ^ 0xFF) & 0xFF]
    return crc


_crc_udi = crc_udi  # older name, used by the tests' builder


def parse(data: bytes, ignore_crc: bool = False) -> DiskImage:
    """LoaderUDI::parse"""
    warnings: list[str] = []

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    size = len(data)
    if size < 4 or not detect(data):
        raise fail("Not a UDI file: signature 'UDI!' missing")
    if data[:4] == SIGNATURE_COMPRESSED:
        raise fail("Compressed UDI ('udi!') is not supported")
    if size < HEADER_SIZE + 4:
        raise fail("UDI file is truncated (shorter than header + CRC)")
    declared = struct.unpack_from("<I", data, 4)[0]
    version, max_cylinder, max_head = data[8], data[9], data[10]
    extended = struct.unpack_from("<I", data, 12)[0]
    if version != VERSION:
        raise fail(f"UDI version {version} is not supported (expected 0)")
    if max_head > 1:
        raise fail(f"UDI declares {max_head + 1} heads, only 1 or 2 are supported")
    if max_cylinder + 1 > MAX_CYLINDERS:
        raise fail(f"UDI declares {max_cylinder + 1} cylinders, the emulator supports at most {MAX_CYLINDERS}")
    if (declared + 4) & 0xFFFFFFFF != size:
        warnings.append(f"UDI size field ({declared}) does not match the file size minus CRC ({size - 4})")
    if HEADER_SIZE + extended > size - 4:
        raise fail("UDI extended header runs past the end of the file")
    stored = struct.unpack_from("<I", data, size - 4)[0]
    actual = crc_udi(memoryview(data)[:size - 4])
    if stored != actual:
        message = f"UDI CRC mismatch: stored 0x{stored:08X}, computed 0x{actual:08X}"
        if not ignore_crc:
            raise fail(message + " (load refused; use setIgnoreCrc(true) to force)")
        warnings.append(message + " (ignored)")

    cylinders, sides = max_cylinder + 1, max_head + 1
    disk = DiskImage(cylinders, sides)
    offset = HEADER_SIZE + extended
    end = size - 4
    for cylinder in range(cylinders):
        for side in range(sides):
            if offset + 3 > end:
                raise fail(f"UDI truncated at track cylinder {cylinder} side {side}")
            track_type = data[offset]
            length = struct.unpack_from("<H", data, offset + 1)[0]
            offset += 3
            if track_type & TRACK_TYPE_MULTIREV_FLAG:
                raise fail(f"UDI track cylinder {cylinder} side {side} uses the multi-revolution extension, "
                           f"not supported")
            if track_type not in (TRACK_TYPE_MFM, TRACK_TYPE_FM, TRACK_TYPE_MIXED):
                raise fail(f"UDI track cylinder {cylinder} side {side} has unknown type {track_type}")
            bitmap_size = (length + 7) // 8
            track_bytes = length + bitmap_size + (bitmap_size if track_type == TRACK_TYPE_MIXED else 0)
            if offset + track_bytes > end:
                raise fail(f"UDI track data truncated at cylinder {cylinder} side {side}")
            if length > MAX_TRACK_SIZE:
                raise fail(f"UDI track cylinder {cylinder} side {side} is {length} bytes long, above the supported "
                           f"maximum {MAX_TRACK_SIZE}")
            if length < MIN_TRACK_SIZE:
                warnings.append(f"UDI track cylinder {cylinder} side {side} is only {length} bytes long, padded to "
                                f"{MIN_TRACK_SIZE}")
            if track_type == TRACK_TYPE_MIXED:
                warnings.append(f"UDI track cylinder {cylinder} side {side} is a mixed FM/MFM track, loaded as MFM")
            track = disk.get_track(cylinder, side)
            track.set_stream(data[offset:offset + length], data[offset + length:offset + length + bitmap_size],
                             None, FM if track_type == TRACK_TYPE_FM else MFM)
            offset += track_bytes

    _apply_weak_map(disk, bytearray(data[offset:end]), warnings)
    disk.format = "udi"
    disk.warnings = warnings
    return disk


def _apply_weak_map(disk: DiskImage, trailer: bytearray, warnings: list[str]) -> None:
    """LoaderUDI::applyWeakMapChunk: the first "UDIW" in the trailer"""
    pos = trailer.find(WEAK_CHUNK_SIGNATURE)
    if pos < 0:
        return
    if pos + WEAK_CHUNK_HEADER_SIZE > len(trailer):
        warnings.append("UDI weak-bit map chunk is truncated, ignored")
        return
    version, count = struct.unpack_from("<HH", trailer, pos + 4)
    if version != WEAK_CHUNK_VERSION:
        warnings.append(f"UDI weak-bit map version {version} is not supported (expected 1), ignored")
        return
    if pos + WEAK_CHUNK_HEADER_SIZE + count * WEAK_CHUNK_RECORD_SIZE > len(trailer):
        warnings.append("UDI weak-bit map chunk is truncated, ignored")
        return
    flagged = False
    skipped = 0
    for i in range(count):
        at = pos + WEAK_CHUNK_HEADER_SIZE + i * WEAK_CHUNK_RECORD_SIZE
        cylinder, side, flags = trailer[at], trailer[at + 1], trailer[at + 2]
        start, length = struct.unpack_from("<HH", trailer, at + 3)
        track = disk.get_track(cylinder, side)
        if track is None or start + length > track.length:
            skipped += 1
            continue
        if flags:
            flagged = True
        for k in range(length):
            track.set_weak_byte(start + k, True)
    if skipped:
        warnings.append(f"UDI weak-bit map: {skipped} of {count} record(s) out of range ignored")
    if flagged:
        warnings.append("UDI weak-bit map: unknown flag bits set, records applied anyway")


def read(path: str, ignore_crc: bool = False) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        message = "File is empty: " + path
        raise DiskConversionError(message, [message])
    return parse(data, ignore_crc)


def _weak_chunk(disk: DiskImage, warnings: list[str]) -> bytes:
    """LoaderUDI::appendWeakMapChunk: one record per run of weak bytes"""
    records = bytearray()
    count = 0
    overflow = False
    for cylinder in range(disk.cylinders):
        if overflow:
            break
        for side in range(disk.heads):
            track = disk.get_track(cylinder, side)
            if track is None or not track.has_weak_bits():
                continue
            weak = track.weak
            size = track.length
            run_start = -1
            for offset in range(size + 1):
                is_weak = offset < size and bool(weak[offset >> 3] & (1 << (offset & 7)))
                if is_weak and run_start < 0:
                    run_start = offset
                elif not is_weak and run_start >= 0:
                    if count >= 0xFFFF:
                        overflow = True
                        break
                    records += bytes((cylinder, side, 0)) + struct.pack("<HH", run_start & 0xFFFF,
                                                                        (offset - run_start) & 0xFFFF)
                    count += 1
                    run_start = -1
            if overflow:
                break
    if overflow:
        warnings.append("UDI weak-bit map: more than 65535 weak ranges, the rest is dropped")
    if not records:
        return b""
    return WEAK_CHUNK_SIGNATURE + struct.pack("<HH", WEAK_CHUNK_VERSION, count) + bytes(records)


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderUDI::serialize"""
    warnings: list[str] = []
    cylinders, sides = disk.cylinders & 0xFF, disk.heads & 0xFF
    if cylinders == 0 or sides == 0 or sides > 2:
        message = "UDI save refused: invalid disk geometry"
        raise DiskConversionError(message, [message])
    out = bytearray(SIGNATURE)
    out += struct.pack("<I", 0)
    out += bytes((VERSION, cylinders - 1, sides - 1, 0))
    out += struct.pack("<I", 0)
    for cylinder in range(cylinders):
        for side in range(sides):
            track = disk.get_track(cylinder, side)
            if track is None:
                message = f"UDI save refused: track cylinder {cylinder} side {side} is missing"
                warnings.append(message)
                raise DiskConversionError(message, warnings)
            length = track.length
            out.append(TRACK_TYPE_FM if track.encoding == FM else TRACK_TYPE_MFM)
            out += struct.pack("<H", length)
            out += track.raw
            bitmap_size = (length + 7) // 8
            out += track.clock[:bitmap_size]
            if len(track.clock) < bitmap_size:
                out += bytes(bitmap_size - len(track.clock))
    out += _weak_chunk(disk, warnings)
    struct.pack_into("<I", out, 4, len(out))
    out += struct.pack("<I", crc_udi(out))
    return bytes(out), warnings


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
