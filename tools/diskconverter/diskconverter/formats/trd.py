"""TRD - raw TR-DOS sector dump (port of unreal-disk src/loaders/loader_trd.cpp).

Logical tracks in order (cylinder-major, side-minor), 16 sectors of 256 bytes each, sector numbers 1..16. No gaps,
no CRCs: reading lays every track out with the TR-DOS layout (TrackFormatSpec.trdos, sectors 1..16 in physical
order - LoaderOptions.trdosInterleave 0), writes the empty volume sector, then copies the file's sectors over it.
Writing requires every track to hold exactly 16 sectors numbered 1..16 with 256-byte data fields.
"""

from __future__ import annotations

from ..disk_image import MAX_CYLINDERS, DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 256
SECTORS_PER_TRACK = 16
TRACK_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK
FULL_TRACK_SIZE = TRACK_SIZE * 2

VOLUME_SECTOR = 8  # sector 9, 0-based
DISK_TYPE_OFFSET = VOLUME_SECTOR * SECTOR_SIZE + 0xE3

DS_80, DS_40, SS_80, SS_40 = 0x16, 0x17, 0x18, 0x19
TRD_SIGNATURE = 0x10

INTERLEAVE_PATTERNS = (
    tuple(range(1, 17)),  # TR-DOS 5.03 (LoaderOptions default 0)
    (1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16),  # TR-DOS 5.04T
    (1, 12, 7, 2, 13, 8, 3, 14, 9, 4, 15, 10, 5, 16, 11, 6),
)
INTERLEAVE = 0  # LoaderOptions::trdosInterleave as zxdisk runs it


def detect(data: bytes) -> bool:
    """TRD has no signature; the TR-DOS id #10 at track 0 sector 9 byte #E7 is the probe's hint"""
    return len(data) > 8 * 256 + 0xE7 and data[8 * 256 + 0xE7] == TRD_SIGNATURE


def geometry_for_file(data: bytes) -> tuple[int, int]:
    """LoaderTRD::geometryForFile: the disk type byte when the file fits it, else double-sided, 40 cylinders for
    exactly 320 KiB, else 80 (or as many as the size needs)"""
    size = len(data)
    if size > DISK_TYPE_OFFSET:
        cylinders, sides = {DS_80: (80, 2), DS_40: (40, 2), SS_80: (80, 1), SS_40: (40, 1)}.get(
            data[DISK_TYPE_OFFSET], (0, 0))
        if cylinders and size <= cylinders * sides * TRACK_SIZE:
            return cylinders, sides
    full = (size + FULL_TRACK_SIZE - 1) // FULL_TRACK_SIZE
    cylinders = min(full, 255)
    if size != 40 * FULL_TRACK_SIZE and cylinders < 80:
        cylinders = 80
    return cylinders, 2


def format_disk(disk: DiskImage) -> bool:
    """LoaderTRD::format: every track laid out with the TR-DOS layout, then the empty volume sector"""
    cylinders, sides = disk.cylinders, disk.heads
    if cylinders == 0 or cylinders > MAX_CYLINDERS or sides not in (1, 2):
        return False
    forty = cylinders <= 42
    disk_type = (DS_40 if forty else DS_80) if sides == 2 else (SS_40 if forty else SS_80)
    order = list(INTERLEAVE_PATTERNS[INTERLEAVE if INTERLEAVE < 3 else 1])
    for cyl in range(cylinders):
        for side in range(sides):
            disk.get_track(cyl, side).format_track(cyl, side, TrackFormatSpec.trdos(order, SECTORS_PER_TRACK))
    populate_empty_volume_info(disk, disk_type)
    return True


def populate_empty_volume_info(disk: DiskImage, disk_type: int) -> None:
    sector = disk.track_at(0).get_sector(VOLUME_SECTOR)
    if sector is None or not sector.has_data:
        return
    free = ((disk.cylinders * disk.heads - 1) * SECTORS_PER_TRACK) & 0xFFFF
    info = bytearray(sector.data_bytes())
    info[0xE7] = TRD_SIGNATURE
    info[0xE3] = disk_type
    info[0xE5] = free & 0xFF
    info[0xE6] = free >> 8
    info[0xE2] = 1
    info[0xE1] = 0
    info[0xF4] = 0
    info[0xF5:0xFD] = b" " * 8
    info[0xEA:0xF3] = b" " * 9
    sector.write_data(info, recalculate_crc=True)


def parse(data: bytes) -> DiskImage:
    """LoaderTRD::parse"""
    if not data:
        raise DiskConversionError("TRD image is empty", ["TRD image is empty"])
    cylinders, sides = geometry_for_file(data)
    if cylinders == 0 or cylinders > MAX_CYLINDERS:
        message = f"TRD image of {len(data)} bytes needs {cylinders} cylinders, more than the supported {MAX_CYLINDERS}"
        raise DiskConversionError(message, [message])
    disk = DiskImage(cylinders, sides)
    format_disk(disk)
    file_tracks = (len(data) + TRACK_SIZE - 1) // TRACK_SIZE
    tracks = min(file_tracks, cylinders * sides) & 0xFF  # uint8_t in C++
    for track_no in range(tracks):
        track = disk.track_at(track_no)
        for sector_no in range(SECTORS_PER_TRACK):
            offset = track_no * TRACK_SIZE + sector_no * SECTOR_SIZE
            if offset + SECTOR_SIZE > len(data):
                break
            sector = track.get_sector(sector_no)
            if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                continue
            sector.write_data(data[offset:offset + SECTOR_SIZE], recalculate_crc=True)
    disk.format = "trd"
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        return parse(f.read())


def trdos_geometry_problem(disk: DiskImage) -> str:
    """LoaderTRD::isTrdosGeometry: "" when every track holds exactly sectors 1..16 of 256 bytes, else why not"""
    for track_no in range(disk.cylinders * disk.heads):
        track = disk.track_at(track_no)
        if track is None:
            return f"Track {track_no} is missing"
        if track.sector_count() != SECTORS_PER_TRACK:
            return (f"Track {track_no} (cylinder {track.cylinder} side {track.side}) has {track.sector_count()} "
                    f"sectors, TRD requires exactly {SECTORS_PER_TRACK}")
        for number in range(1, SECTORS_PER_TRACK + 1):
            sector = track.find_index(number)
            if sector is None or not sector.has_data:
                return (f"Track {track_no} (cylinder {track.cylinder} side {track.side}): sector {number} is missing "
                        f"or has no data field")
            if sector.size != SECTOR_SIZE:
                return (f"Track {track_no} (cylinder {track.cylinder} side {track.side}): sector {number} is "
                        f"{sector.size} bytes, TRD holds only 256-byte sectors")
    return ""


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderTRD::writeImage"""
    problem = trdos_geometry_problem(disk)
    if problem:
        message = "TRD save refused: " + problem
        raise DiskConversionError(message, [message])
    out = bytearray()
    for track_no in range(disk.cylinders * disk.heads):
        track = disk.track_at(track_no)
        for sector_no in range(SECTORS_PER_TRACK):
            out += track.data_for_sector(sector_no)
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
