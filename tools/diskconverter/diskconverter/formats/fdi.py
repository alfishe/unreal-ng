"""FDI - Full Disk Image (port of unreal-disk src/loaders/loader_fdi.cpp).

Reading lays each track out with the TR-DOS layout for its sector list (raw N in the ID field, data length
128 << (N & 3); gap 3 shrunk to 16, then the track grown, when the sectors do not fit 6250 bytes), copies the
data, writes F8 for a deleted sector and inverts both data CRC bytes when the "CRC ok" bit of N & 3 is clear.
A sector without data (flag #80) or whose data lies outside the file is an ID-only sector. Writing stores the
sector list of every track in stream order; the header is that of a fresh loader (not write protected, no
description, no extra header), as `zxdisk convert` writes it.
"""

from __future__ import annotations

import struct

from ..disk_image import (DEFAULT_TRACK_SIZE_MFM, FM, MAX_CYLINDERS, DAM_DELETED, DiskConversionError, DiskImage,
                          TrackFormatSpec, fit_track_spec)

SIGNATURE = b"FDI"
HEADER_SIZE = 14
TRACK_HEADER_SIZE = 7
SECTOR_HEADER_SIZE = 7

FLAG_DELETED = 0x40
FLAG_NO_DATA = 0x80
MIN_GAP_POST_DATA = 16


def detect(data: bytes) -> bool:
    return data[:3] == SIGNATURE


def parse(data: bytes) -> DiskImage:
    """LoaderFDI::parse"""
    warnings: list[str] = []

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    if not detect(data):
        raise fail("Not an FDI file: signature 'FDI' missing")
    size = len(data)
    if size < HEADER_SIZE:
        raise fail("FDI file is truncated (shorter than the header)")
    cylinders, heads, _description_offset, data_offset, extra_length = struct.unpack_from("<HHHHH", data, 4)
    if cylinders == 0 or cylinders > MAX_CYLINDERS:
        raise fail(f"FDI declares {cylinders} cylinders, supported range is 1..{MAX_CYLINDERS}")
    if heads == 0 or heads > 2:
        raise fail(f"FDI declares {heads} heads, supported range is 1..2")
    if HEADER_SIZE + extra_length > size:
        raise fail("FDI extra header runs past the end of the file")
    if data_offset > size:
        raise fail("FDI data offset is outside the file")

    disk = DiskImage(cylinders, heads, write_protected=data[3] != 0)
    offset = HEADER_SIZE + extra_length
    for cylinder in range(cylinders):
        for head in range(heads):
            if offset + TRACK_HEADER_SIZE > size:
                raise fail(f"FDI truncated at the track header of cylinder {cylinder} head {head}")
            track_data_offset = struct.unpack_from("<I", data, offset)[0]
            count = data[offset + 6]
            offset += TRACK_HEADER_SIZE
            if offset + count * SECTOR_HEADER_SIZE > size:
                raise fail(f"FDI truncated inside the sector list of cylinder {cylinder} head {head}")

            entries = []
            id_only = []
            for s in range(count):
                c, h, r, n, flags, doff = struct.unpack_from("<BBBBBH", data, offset + s * SECTOR_HEADER_SIZE)
                if n > 3:
                    warnings.append(f"FDI cylinder {cylinder} head {head} sector {r}: N={n} kept in ID, data length "
                                    f"{128 << (n & 3)} (N & 3)")
                length = 128 << (n & 3)
                start = data_offset + track_data_offset + doff
                has_data = not flags & FLAG_NO_DATA
                if has_data and start + length > size:
                    warnings.append(f"FDI cylinder {cylinder} head {head} sector {r}: data lies outside the file, "
                                    f"stored as ID-only")
                    id_only.append(1)
                else:
                    id_only.append(0 if has_data else 1)
                entries.append((c, h, r, n, flags, start))
            offset += count * SECTOR_HEADER_SIZE

            track = disk.get_track(cylinder, head)
            if count == 0:
                track.resize_raw(DEFAULT_TRACK_SIZE_MFM)
                continue

            spec = TrackFormatSpec.trdos([e[2] for e in entries], count)
            spec.sector_size_codes = [e[3] for e in entries]
            spec.sector_cylinders = [e[0] for e in entries]
            spec.sector_heads = [e[1] for e in entries]
            spec.id_only = id_only
            ok, note = fit_track_spec(spec, MIN_GAP_POST_DATA)
            if not ok:
                raise fail(f"FDI cylinder {cylinder} head {head}: {note}")
            if note:
                warnings.append(f"FDI cylinder {cylinder} head {head}: {note}")

            track.format_track(cylinder, head, spec)
            views = track.index
            for s, (_c, _h, _r, n, flags, start) in enumerate(entries):
                if s >= len(views):
                    break
                sector = views[s]
                if not sector.has_data:
                    continue
                sector.write_data(data[start:start + sector.size])
                if flags & FLAG_DELETED:
                    sector.set_data_address_mark(DAM_DELETED)
                sector.recalculate_data_crc()
                if not flags & (1 << (n & 3)):
                    sector.set_data_crc(sector.data_crc() ^ 0xFFFF)
            track.reindex()

    disk.format = "fdi"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        message = "File is empty: " + path
        raise DiskConversionError(message, [message])
    return parse(data)


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderFDI::serialize with a fresh loader's header fields"""
    warnings: list[str] = []
    cylinders, heads = disk.cylinders & 0xFF, disk.heads & 0xFF
    out = bytearray(SIGNATURE)
    out.append(0)
    out += struct.pack("<HHHHH", cylinders, heads, 0, 0, 0)
    data_area = bytearray()
    lossy = False
    for cylinder in range(cylinders):
        for head in range(heads):
            track = disk.get_track(cylinder, head)
            if track is None:
                message = f"FDI save refused: track cylinder {cylinder} head {head} is missing"
                warnings.append(message)
                raise DiskConversionError(message, warnings)
            total = track.sector_count()
            count = min(total, 255)
            if total > 255:
                warnings.append(f"FDI cylinder {cylinder} head {head}: only the first 255 of {total} sectors stored")
            if track.encoding == FM:
                warnings.append(f"FDI cylinder {cylinder} head {head}: FM track stored as a plain sector list "
                                f"(density lost)")
                lossy = True
            if track.length != DEFAULT_TRACK_SIZE_MFM:
                lossy = True
            out += struct.pack("<IHB", len(data_area) & 0xFFFFFFFF, 0, count)
            sector_data_offset = 0
            for sector in track.index[:count]:
                size_code = sector.size_code
                flags = 0
                if not sector.has_data:
                    flags |= FLAG_NO_DATA
                else:
                    if sector.data_crc_valid:
                        flags |= 1 << (size_code & 3)
                    if sector.deleted:
                        flags |= FLAG_DELETED
                if not sector.id_crc_valid:
                    lossy = True
                    warnings.append(f"FDI cylinder {cylinder} head {head} sector {sector.number}: ID CRC error cannot "
                                    f"be stored, the sector will read with a valid ID (use UDI)")
                out += struct.pack("<BBBBBH", sector.cylinder, sector.head, sector.number, size_code, flags,
                                   sector_data_offset)
                if sector.has_data:
                    data_area += sector.data_bytes()
                    sector_data_offset = (sector_data_offset + sector.size) & 0xFFFF
    if len(out) > 0xFFFF:
        message = "FDI save refused: header area exceeds 64 KB (too many tracks / sectors)"
        warnings.append(message)
        raise DiskConversionError(message, warnings)
    struct.pack_into("<H", out, 10, len(out))
    out += data_area
    if lossy:
        warnings.append("FDI stores sector lists only: gaps, clock marks, exact CRC values, track length and density "
                        "are not preserved (use UDI for a lossless copy)")
    return bytes(out), warnings


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
