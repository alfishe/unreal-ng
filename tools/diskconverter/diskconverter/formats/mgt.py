"""MGT / IMG - DISCiPLE, +D, SAM Coupe sector dumps (port of unreal-disk src/loaders/loader_mgt.cpp).

80 (or 40) cylinders x 2 sides x 10 sectors 1..10 of 512 bytes, no low-level information. The extension sets the
track order: .mgt cylinder-major with the sides interleaved, .img side-major (all of side 0, then side 1). Every
track is laid out with TrackFormatSpec plusD (IBM, GAP#3 0x18, filler 0). Saving needs sectors 1..10 of 512 bytes
on every track of a 2-sided 40 / 80 cylinder disk; otherwise it is refused with the reason.
"""

from __future__ import annotations

from ..disk_image import DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 512
SECTORS_PER_TRACK = 10
SIDES = 2
TRACK_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK  # 5120
IMAGE_SIZE_80 = TRACK_SIZE * SIDES * 80  # 819 200
IMAGE_SIZE_40 = TRACK_SIZE * SIDES * 40  # 409 600

CYLINDER_MAJOR = "cylinder-major"  # .mgt
SIDE_MAJOR = "side-major"  # .img


def cylinders_for_size(size: int) -> int:
    if size == IMAGE_SIZE_80:
        return 80
    if size == IMAGE_SIZE_40:
        return 40
    return 0


def _normalize_extension(extension_or_path: str) -> str:
    """normalizeExtension: the text after the last dot of the last path component (the whole name without one)"""
    sep = max(extension_or_path.rfind("/"), extension_or_path.rfind("\\"))
    name = extension_or_path[sep + 1:]
    dot = name.rfind(".")
    return (name if dot < 0 else name[dot + 1:]).lower()


def detect(size: int, extension: str) -> bool:
    return cylinders_for_size(size) != 0 and _normalize_extension(extension) in ("mgt", "img")


def order_for_extension(extension_or_path: str) -> str:
    return SIDE_MAJOR if _normalize_extension(extension_or_path) == "img" else CYLINDER_MAJOR


def plus_d_spec() -> TrackFormatSpec:
    """TrackFormatSpec::plusD: 10 x 512, sectors 1..10, GAP#3 0x18, filler 0"""
    return TrackFormatSpec.ibm(10, 2, 1, 0x18, 0x00)


def plus_d_geometry(disk: DiskImage) -> str:
    """isPlusDGeometry: "" when the disk qualifies, else the reason"""
    if disk.heads != SIDES:
        return f"Image has {disk.heads} side(s), MGT requires exactly 2"
    cylinders = disk.cylinders
    if cylinders not in (40, 80):
        return f"Image has {cylinders} cylinders, MGT requires 40 or 80"
    for cylinder in range(cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            if track is None:
                return f"Track cylinder {cylinder} side {side} is missing"
            if track.sector_count() != SECTORS_PER_TRACK:
                return (f"Track cylinder {cylinder} side {side} has {track.sector_count()} sectors, MGT requires "
                        f"exactly {SECTORS_PER_TRACK}")
            for number in range(1, SECTORS_PER_TRACK + 1):
                sector = track.find_index(number)
                if sector is None or not sector.has_data:
                    return f"Track cylinder {cylinder} side {side}: sector {number} is missing or has no data field"
                if sector.size != SECTOR_SIZE:
                    return (f"Track cylinder {cylinder} side {side}: sector {number} is {sector.size} bytes, MGT "
                            f"requires {SECTOR_SIZE}")
    return ""


def is_plus_d_geometry(disk: DiskImage) -> bool:
    return plus_d_geometry(disk) == ""


def _track_offset(cylinders: int, cylinder: int, side: int, order: str) -> int:
    index = side * cylinders + cylinder if order == SIDE_MAJOR else cylinder * SIDES + side
    return index * TRACK_SIZE


def parse(data: bytes, order: str = CYLINDER_MAJOR) -> DiskImage:
    cylinders = cylinders_for_size(len(data))
    if cylinders == 0:
        message = f"MGT: file size {len(data)} is neither 819200 (80 cylinders) nor 409600 (40 cylinders)"
        raise DiskConversionError(message, [message])
    spec = plus_d_spec()
    disk = DiskImage(cylinders, SIDES)
    for cylinder in range(cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            track.format_track(cylinder, side, spec)
            src = _track_offset(cylinders, cylinder, side, order)
            for number in range(1, SECTORS_PER_TRACK + 1):
                sector = track.find_index(number)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    message = f"MGT: formatter did not produce sector {number} on cylinder {cylinder} side {side}"
                    raise DiskConversionError(message, [message])
                sector.write_data(data[src:src + SECTOR_SIZE], recalculate_crc=True)
                src += SECTOR_SIZE
    disk.format = "mgt"
    disk.warnings = []
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if cylinders_for_size(len(data)) == 0:
        message = f"MGT: {path} is {len(data)} bytes, expected 819200 or 409600"
        raise DiskConversionError(message, [message])
    return parse(data, order_for_extension(path))


def serialize(disk: DiskImage, order: str = CYLINDER_MAJOR) -> tuple[bytes, list[str]]:
    reason = plus_d_geometry(disk)
    if reason:
        message = "MGT save refused: " + reason
        raise DiskConversionError(message, [message])
    cylinders = disk.cylinders
    out = bytearray(cylinders * SIDES * TRACK_SIZE)
    for cylinder in range(cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            dst = _track_offset(cylinders, cylinder, side, order)
            for number in range(1, SECTORS_PER_TRACK + 1):
                out[dst:dst + SECTOR_SIZE] = track.find_index(number).data_bytes()
                dst += SECTOR_SIZE
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk, order_for_extension(path))
    with open(path, "wb") as f:
        f.write(data)
    return warnings
