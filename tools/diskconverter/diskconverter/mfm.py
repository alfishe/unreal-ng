"""Compatibility layer over the stream model in disk_image (kept for older callers).

The raw track stream, its clock-mark bitmap and the sector index now live in `disk_image.Track`, which mirrors
unreal-disk's DiskImage::Track: `Track.set_sectors` lays sectors out the way the C++ FDI loader does and
`Track.reindex` finds them the way Track::reindex does. This module keeps the old helpers on top of it:

- `crc_wd1793`, `crc_bytes` and the address-mark constants;
- `encode_track(sectors)`: the stream (and the offsets of its clock-marked bytes) a track laid out from sectors has;
- `decode_track(raw, clock_bitmap)`: the sectors Track::reindex finds in a stream.
"""

from __future__ import annotations

from dataclasses import dataclass

from .disk_image import (DAM_DELETED, DAM_NORMAL, DEFAULT_TRACK_SIZE_MFM, IDAM, MFM, Sector, Track, crc_bytes,
                         crc_wd1793)

__all__ = ["IDAM", "DAM_NORMAL", "DAM_DELETED", "SYNC_BYTE", "NOMINAL_TRACK_LEN", "crc_wd1793", "crc_bytes",
           "EncodedTrack", "encode_track", "decode_track"]

SYNC_BYTE = 0xA1
NOMINAL_TRACK_LEN = DEFAULT_TRACK_SIZE_MFM


@dataclass
class EncodedTrack:
    raw: bytes
    clock_missing: list[int]  # byte offsets whose clock bit is set (the sync 0xA1 bytes and marks)
    clock: bytes = b""  # the same as a bitmap (bit i of byte i / 8)


def encode_track(sectors: list[Sector], cylinder: int = 0, head: int = 0) -> EncodedTrack:
    """The MFM stream of a track laid out from sectors (TR-DOS gaps, as the C++ FDI loader lays it out)"""
    track = Track(cylinder, head)
    track.set_sectors(list(sectors), MFM)
    clock = bytes(track.clock)
    marks = [i for i in range(track.length) if clock[i >> 3] & (1 << (i & 7))]
    return EncodedTrack(bytes(track.raw), marks, clock)


def decode_track(raw: bytes, clock_bitmap: bytes | None = None) -> list[Sector]:
    """The sectors Track::reindex finds in an MFM stream. clock_bitmap (UDI: bit i = byte i carries a missing-clock
    mark): with any mark set an A1 counts as a sync byte only when it carries the mark; without marks every literal
    A1 A1 A1 counts."""
    track = Track()
    track.set_stream(raw, clock_bitmap, None, MFM)
    return track.sectors
