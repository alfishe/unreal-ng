"""Minimal MFM byte-stream codec: synthesizes a track's raw byte stream from structured
sectors (for writing UDI), and decodes a raw byte stream back into sectors (for reading
UDI, including files this tool did not itself produce).

This works at the *byte* level the same way UDI's own model does (see
docs/file-formats/disk-images/udi.md and core/src/emulator/io/fdc/diskimage.h's
`RawTrack`): sync marks are literal 0xA1 bytes in the stream, not flux-level clock
manipulation. That is sufficient for every format this tool talks to - none of them are
flux captures.

CRC is CRC-16/CCITT exactly as the WD1793 computes it (core/src/emulator/io/fdc/fdc.h
`crcWD1793`): poly 0x1021, preset 0xCDB4 (already "primed" for the 3 leading 0xA1 sync
bytes, which are therefore excluded from the explicit CRC input), result byte-swapped
before being stored on disk.
"""

from __future__ import annotations

from dataclasses import dataclass

from .disk_image import Sector

GAP_PRE_ID = 12
SYNC_LEN = 12
GAP_POST_ID = 22
GAP_POST_DATA_DEFAULT = 60
GAP_POST_DATA_MIN = 16
GAP_LEADING = 16
NOMINAL_TRACK_LEN = 6250

IDAM = 0xFE
DAM_NORMAL = 0xFB
DAM_DELETED = 0xF8
SYNC_BYTE = 0xA1


def crc_wd1793(data: bytes) -> int:
    """Matches core/src/emulator/io/fdc/fdc.h's crcWD1793 exactly: computes CRC-16/CCITT
    (poly 0x1021, preset 0xCDB4) then byte-swaps the internal register before returning.

    Byte order note (verified against a real captured sector, see git history of this
    file): the parser (mfm_parser.h) undoes this same swap to get back the raw register
    value, then reads the two on-disk CRC bytes as `(data[hi] << 8) | data[lo]` - i.e. the
    on-disk bytes are the RAW (pre-swap) register's high byte then low byte. Since this
    function already returns the swapped value, on-disk bytes are the SWAPPED value's low
    byte then high byte - write it little-endian, not big-endian. (An earlier version of
    this function wrote big-endian, which produced byte-swapped CRCs that the WD1793 ID
    search rejects outright - every sector on such a disk fails, seen as "Disk error" when
    TR-DOS tries to read anything at all.)
    """
    crc = 0xCDB4
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return ((crc & 0xFF) << 8) | (crc >> 8)


def _put_crc(buf: bytearray, value: int) -> None:
    buf.append(value & 0xFF)
    buf.append((value >> 8) & 0xFF)


@dataclass
class EncodedTrack:
    raw: bytes
    clock_missing: list[int]  # byte offsets whose clock bit should be set (the sync 0xA1 bytes)


def encode_track(sectors: list[Sector]) -> EncodedTrack:
    raw = bytearray([0x4E] * GAP_LEADING)
    clock_missing: list[int] = []

    # Shrink the post-data gap until the track fits the nominal 6250-byte revolution,
    # same policy as loader_fdi.cpp's buildTrackSpec (shrink to 16, then just let the
    # track grow past nominal if it still doesn't fit - UDI's TLEN is stored explicitly
    # per track, so an oversized track is still valid, just non-nominal).
    gap_post_data = GAP_POST_DATA_DEFAULT
    while gap_post_data > GAP_POST_DATA_MIN:
        projected = len(raw)
        for sector in sectors:
            projected += GAP_PRE_ID + SYNC_LEN + 3 + 5 + 2 + GAP_POST_ID
            if not sector.no_data:
                projected += SYNC_LEN + 3 + 1 + sector.data_size + 2 + gap_post_data
        if projected <= NOMINAL_TRACK_LEN:
            break
        gap_post_data -= 1

    for sector in sectors:
        raw += bytes([0x4E] * GAP_PRE_ID)
        raw += bytes([0x00] * SYNC_LEN)
        sync_start = len(raw)
        raw += bytes([SYNC_BYTE] * 3)
        clock_missing.extend(range(sync_start, sync_start + 3))

        idam_fields = bytes([IDAM, sector.cylinder, sector.head, sector.number, sector.size_code])
        raw += idam_fields
        _put_crc(raw, crc_wd1793(idam_fields))

        raw += bytes([0x4E] * GAP_POST_ID)

        if sector.no_data:
            continue

        raw += bytes([0x00] * SYNC_LEN)
        sync_start = len(raw)
        raw += bytes([SYNC_BYTE] * 3)
        clock_missing.extend(range(sync_start, sync_start + 3))

        dam = DAM_DELETED if sector.deleted else DAM_NORMAL
        raw.append(dam)
        raw += sector.data
        crc = crc_wd1793(bytes([dam]) + sector.data)
        if not sector.crc_valid:
            crc ^= 0x0001  # deliberately wrong, mirrors loader-fdi.md's "flip one CRC bit" convention
        _put_crc(raw, crc)

        raw += bytes([0x4E] * gap_post_data)

    if len(raw) < NOMINAL_TRACK_LEN:
        raw += bytes([0x4E] * (NOMINAL_TRACK_LEN - len(raw)))

    return EncodedTrack(raw=bytes(raw), clock_missing=clock_missing)


def decode_track(raw: bytes) -> list[Sector]:
    """Scans for `A1 A1 A1 FE` IDAM markers, reads C/H/R/N + CRC, then looks for the next
    `A1 A1 A1 <DAM>` within the WD1793 datasheet's search window (43 bytes) for the data
    field. A sector whose data field never turns up (or whose bytes run past the end of the
    stream) is recorded as ID-only (`no_data=True`), matching FDI's own convention for that
    case."""
    sectors: list[Sector] = []
    n = len(raw)
    DAM_SEARCH_WINDOW = 43

    i = 0
    while i < n - 6:
        if raw[i] == SYNC_BYTE and raw[i + 1] == SYNC_BYTE and raw[i + 2] == SYNC_BYTE and raw[i + 3] == IDAM:
            fields = raw[i + 3 : i + 8]
            if len(fields) < 5:
                break
            _, cyl, head, number, size_code = fields
            size_code &= 3
            idam_crc_stored = raw[i + 8 : i + 10]
            idam_end = i + 10

            data = b""
            crc_valid = True
            deleted = False
            no_data = True

            search_end = min(idam_end + DAM_SEARCH_WINDOW, n)
            j = idam_end
            while j < search_end - 4:
                if raw[j] == SYNC_BYTE and raw[j + 1] == SYNC_BYTE and raw[j + 2] == SYNC_BYTE and raw[j + 3] in (
                    DAM_NORMAL,
                    DAM_DELETED,
                ):
                    dam = raw[j + 3]
                    size = 128 << size_code
                    data_start = j + 4
                    data = raw[data_start : data_start + size]
                    if len(data) == size:
                        no_data = False
                        deleted = dam == DAM_DELETED
                        stored_crc = raw[data_start + size : data_start + size + 2]
                        computed = crc_wd1793(bytes([dam]) + data)
                        crc_valid = bytes([computed & 0xFF, (computed >> 8) & 0xFF]) == bytes(stored_crc)
                        i = data_start + size + 2
                    break
                j += 1

            if no_data:
                data = b""
                i = idam_end

            sectors.append(
                Sector(
                    cylinder=cyl,
                    head=head,
                    number=number,
                    size_code=size_code,
                    data=data,
                    crc_valid=crc_valid,
                    deleted=deleted,
                    no_data=no_data,
                )
            )
            continue
        i += 1

    return sectors
