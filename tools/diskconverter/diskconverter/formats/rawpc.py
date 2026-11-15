"""Raw PC floppy (.img / .ima): a bare sector dump of a 720 KB or 1.44 MB disk (port of unreal-disk
src/loaders/loader_rawpc.cpp).

80 cylinders x 2 sides x 9 (DD) or 18 (HD) sectors of 512 bytes, cylinder-major with the sides interleaved,
recognized by the file size alone. Every track is an IBM System 34 MFM track (index mark, GAP#3 84 / 108, filler
0xF6, 6250 / 12500 bytes). Saving needs that layout still in place (sectors 1..N of 512 bytes on every track);
otherwise it is refused with the reason.
"""

from __future__ import annotations

from ..disk_image import DEFAULT_TRACK_SIZE_MFM, DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 512
CYLINDERS = 80
SIDES = 2
SECTORS_DD = 9
SECTORS_HD = 18
IMAGE_SIZE_DD = SECTOR_SIZE * SECTORS_DD * SIDES * CYLINDERS  # 737 280
IMAGE_SIZE_HD = SECTOR_SIZE * SECTORS_HD * SIDES * CYLINDERS  # 1 474 560
GAP3_DD = 84
GAP3_HD = 108


def sectors_for_size(size: int) -> int:
    if size == IMAGE_SIZE_DD:
        return SECTORS_DD
    if size == IMAGE_SIZE_HD:
        return SECTORS_HD
    return 0


def detect(size: int) -> bool:
    return sectors_for_size(size) != 0


def track_spec(sectors: int) -> TrackFormatSpec:
    hd = sectors == SECTORS_HD
    spec = TrackFormatSpec.ibm(sectors, 2, 1, GAP3_HD if hd else GAP3_DD, 0xF6)
    spec.track_length = DEFAULT_TRACK_SIZE_MFM * 2 if hd else DEFAULT_TRACK_SIZE_MFM
    spec.gap_index = 80
    spec.gap_post_index = 50
    spec.gap_post_id = 22
    return spec


def regular_geometry(disk: DiskImage) -> tuple[int, str]:
    """isRegularGeometry: (sectors per track, "") or (0, reason)"""
    if disk.heads != SIDES:
        return 0, f"Image has {disk.heads} side(s), a raw PC floppy requires exactly 2"
    if disk.cylinders != CYLINDERS:
        return 0, f"Image has {disk.cylinders} cylinders, a raw PC floppy requires 80"
    first = disk.get_track(0, 0)
    expected = first.sector_count() if first is not None else 0
    if expected not in (SECTORS_DD, SECTORS_HD):
        return 0, (f"Track cylinder 0 side 0 has {expected} sectors, a raw PC floppy requires 9 (720 KB) or 18 "
                   f"(1.44 MB)")
    for cylinder in range(CYLINDERS):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            if track is None:
                return 0, f"Track cylinder {cylinder} side {side} is missing"
            if track.sector_count() != expected:
                return 0, (f"Track cylinder {cylinder} side {side} has {track.sector_count()} sectors, the rest of "
                           f"the disk has {expected}")
            for number in range(1, expected + 1):
                sector = track.find_index(number)
                if sector is None or not sector.has_data:
                    return 0, (f"Track cylinder {cylinder} side {side}: sector {number} is missing or has no data "
                               f"field")
                if sector.size != SECTOR_SIZE:
                    return 0, (f"Track cylinder {cylinder} side {side}: sector {number} is {sector.size} bytes, a "
                               f"raw PC floppy requires 512")
    return expected, ""


def _track_offset(sectors: int, cylinder: int, side: int) -> int:
    return (cylinder * SIDES + side) * sectors * SECTOR_SIZE


def parse(data: bytes) -> DiskImage:
    warnings: list[str] = []
    sectors = sectors_for_size(len(data))
    if sectors == 0:
        message = f"Raw PC floppy: file size {len(data)} is neither 737280 (720 KB) nor 1474560 (1.44 MB)"
        raise DiskConversionError(message, [message])
    spec = track_spec(sectors)
    disk = DiskImage(CYLINDERS, SIDES)
    for cylinder in range(CYLINDERS):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            track.format_track(cylinder, side, spec)
            src = _track_offset(sectors, cylinder, side)
            for index in range(sectors):
                sector = track.find_index(index + 1)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    message = (f"Raw PC floppy: formatter did not produce sector {index + 1} on cylinder {cylinder} "
                               f"side {side}")
                    raise DiskConversionError(message, [message])
                sector.write_data(data[src:src + SECTOR_SIZE], recalculate_crc=True)
                src += SECTOR_SIZE
    disk.format = "rawpc"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if sectors_for_size(len(data)) == 0:
        message = f"Raw PC floppy: {path} is {len(data)} bytes, expected 737280 or 1474560"
        raise DiskConversionError(message, [message])
    return parse(data)


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    sectors, reason = regular_geometry(disk)
    if not sectors:
        message = "Raw PC floppy save refused: " + reason + " (export the disk as UDI to keep it)"
        raise DiskConversionError(message, [message])
    out = bytearray(CYLINDERS * SIDES * sectors * SECTOR_SIZE)
    for cylinder in range(CYLINDERS):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            dst = _track_offset(sectors, cylinder, side)
            for number in range(1, sectors + 1):
                out[dst:dst + SECTOR_SIZE] = track.find_index(number).data_bytes()
                dst += SECTOR_SIZE
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
