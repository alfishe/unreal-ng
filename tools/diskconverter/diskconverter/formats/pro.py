"""PRO - Profi CP/M floppy sector dump (port of unreal-disk src/loaders/loader_pro.cpp).

80..83 cylinders x 2 sides x 5 sectors of 1024 bytes, cylinder-major with the sides interleaved, recognized by the
.pro extension and the size. Sector IDs 1..5, except cylinder 0 side 0 (the system track): 1, 2, 3, 4, 9. Every
track is an IBM MFM track of 6250 bytes (index mark, GAP#3 54, filler 0xE5). Saving needs that layout still in place
(9 standing for 5 on the system track); otherwise it is refused with the reason.
"""

from __future__ import annotations

from ..disk_image import DEFAULT_TRACK_SIZE_MFM, DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 1024
SECTORS = 5
SIDES = 2
MIN_CYLINDERS = 80
MAX_CYLINDERS = 83
TRACK_SIZE = SECTOR_SIZE * SECTORS  # 5120
GAP3 = 54
SYSTEM_SECTOR = 9


def cylinders_for_size(size: int) -> int:
    if size % (TRACK_SIZE * SIDES) != 0:
        return 0
    cylinders = size // (TRACK_SIZE * SIDES)
    return cylinders if MIN_CYLINDERS <= cylinders <= MAX_CYLINDERS else 0


def track_spec(system_track: bool = False) -> TrackFormatSpec:
    spec = TrackFormatSpec.ibm(SECTORS, 3, 1, GAP3, 0xE5)
    if system_track:
        spec.sector_numbers[-1] = SYSTEM_SECTOR
    spec.track_length = DEFAULT_TRACK_SIZE_MFM
    spec.gap_index = 80
    spec.gap_post_index = 50
    spec.gap_post_id = 22
    return spec


def find_sector(track, cylinder: int, side: int, number: int):
    """Logical sector 1..5: the sector of that ID, on the system track 9 for a missing 5"""
    sector = track.find_index(number)
    if sector is None and cylinder == 0 and side == 0 and number == SECTORS:
        sector = track.find_index(SYSTEM_SECTOR)
    return sector


def regular_geometry(disk: DiskImage) -> str:
    """isRegularGeometry: "" when the disk qualifies, else the reason"""
    if disk.heads != SIDES:
        return f"Image has {disk.heads} side(s), a .pro image requires 2"
    cylinders = disk.cylinders
    if cylinders < MIN_CYLINDERS or cylinders > MAX_CYLINDERS:
        return f"Image has {cylinders} cylinders, a .pro image takes 80 to 83"
    for cylinder in range(cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            if track is None:
                return f"Track cylinder {cylinder} side {side} is missing"
            if track.sector_count() != SECTORS:
                return (f"Track cylinder {cylinder} side {side} has {track.sector_count()} sectors, a .pro image "
                        f"requires 5")
            for number in range(1, SECTORS + 1):
                sector = find_sector(track, cylinder, side, number)
                if sector is None or not sector.has_data:
                    return f"Track cylinder {cylinder} side {side}: sector {number} is missing or has no data field"
                if sector.size != SECTOR_SIZE:
                    return (f"Track cylinder {cylinder} side {side}: sector {number} is {sector.size} bytes, a .pro "
                            f"image requires 1024")
    return ""


def parse(data: bytes) -> DiskImage:
    cylinders = cylinders_for_size(len(data))
    if cylinders == 0:
        message = f"PRO: file size {len(data)} is not 80 to 83 cylinders of 2 x 5 x 1024 bytes"
        raise DiskConversionError(message, [message])
    spec = track_spec()
    system = track_spec(True)
    disk = DiskImage(cylinders, SIDES)
    src = 0
    for cylinder in range(cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            track.format_track(cylinder, side, system if cylinder == 0 and side == 0 else spec)
            for index in range(SECTORS):
                sector = find_sector(track, cylinder, side, index + 1)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    message = f"PRO: formatter did not produce sector {index + 1} on cylinder {cylinder} side {side}"
                    raise DiskConversionError(message, [message])
                sector.write_data(data[src:src + SECTOR_SIZE], recalculate_crc=True)
                src += SECTOR_SIZE
    disk.format = "pro"
    disk.warnings = []
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if cylinders_for_size(len(data)) == 0:
        message = f"PRO: {path} is {len(data)} bytes, expected 80 to 83 cylinders of 10240"
        raise DiskConversionError(message, [message])
    return parse(data)


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    reason = regular_geometry(disk)
    if reason:
        message = "PRO save refused: " + reason + " (export the disk as UDI to keep it)"
        raise DiskConversionError(message, [message])
    out = bytearray()
    for cylinder in range(disk.cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            for number in range(1, SECTORS + 1):
                out += find_sector(track, cylinder, side, number).data_bytes()
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
