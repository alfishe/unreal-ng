"""Didaktik D40 / D80 raw image (MDOS, .d40 / .d80 as Fuse keeps them) - port of unreal-disk
src/loaders/loader_mdos.cpp.

The 512-byte sectors of an MDOS disk in logical order, no header: LSN = (cylinder * sides + side) * SPT +
(sector - 1), sector ids 1..SPT. Recognized by its boot sector (LSN 0): "SDOS" at 204 and the geometry it gives
(2 sides when bit 4 of #B1 is set, cylinders at #B2, sectors a track at #B3) matching the file size.

Every track is an IBM MFM track of 512-byte sectors 1..SPT (filler E5, the widest gap 3 from 84 down to 8 that
fits): 6 250 bytes, 12 500 (HD) past 10 sectors. Writing stores the sectors back while every track still carries the
same sectors 1..N of 512 bytes.
"""

from __future__ import annotations

from ..disk_image import DEFAULT_TRACK_SIZE_MFM, MAX_CYLINDERS, DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 512
SIZE_CODE = 2
MARK_OFFSET = 204
MARK = b"SDOS"
MAX_SECTORS = 18


def boot_geometry(boot: bytes, max_cylinders: int = MAX_CYLINDERS) -> tuple[int, int, int] | None:
    """LoaderMDOS::bootGeometry: (cylinders, sides, sectors a track) when the boot sector carries the MDOS mark"""
    if boot[MARK_OFFSET:MARK_OFFSET + 4] != MARK:
        return None
    cylinders, sectors = boot[0xB2], boot[0xB3]
    if cylinders == 0 or cylinders > max_cylinders or sectors == 0 or sectors > MAX_SECTORS:
        return None
    sides = 2 if boot[0xB1] & 0x10 else 1
    return cylinders, sides, sectors


def detect(data: bytes, size: int | None = None) -> bool:
    """LoaderMDOS::detect: data holds at least the boot sector, size is the file size (default len(data))"""
    size = len(data) if size is None else size
    geometry = boot_geometry(data) if len(data) >= SECTOR_SIZE else None
    return geometry is not None and size == geometry[0] * geometry[1] * geometry[2] * SECTOR_SIZE


def track_spec(sectors: int) -> TrackFormatSpec:
    """LoaderMDOS::trackSpec"""
    hd = sectors > 10
    spec = None
    for gap3 in range(84, 7, -4):
        spec = TrackFormatSpec.ibm(sectors, SIZE_CODE, 1, gap3, 0xE5)
        spec.track_length = DEFAULT_TRACK_SIZE_MFM * 2 if hd else DEFAULT_TRACK_SIZE_MFM
        if spec.fits():
            break
    return spec


def parse(data: bytes) -> DiskImage:
    """LoaderMDOS::parse"""
    warnings: list[str] = []

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    geometry = boot_geometry(data) if len(data) >= SECTOR_SIZE else None
    if geometry is None:
        raise fail('MDOS: no MDOS boot sector ("SDOS" at 204 and a geometry)')
    cylinders, sides, sectors = geometry
    expected = cylinders * sides * sectors * SECTOR_SIZE
    if len(data) != expected:
        raise fail(f"MDOS: the boot sector gives {cylinders} x {sides} x {sectors} sectors ({expected} bytes), the "
                   f"file has {len(data)}")
    spec = track_spec(sectors)
    if not spec.fits():
        raise fail(f"MDOS: {sectors} sectors of 512 bytes do not fit into a track")

    disk = DiskImage(cylinders, sides)
    src = 0
    for cylinder in range(cylinders):
        for side in range(sides):
            track = disk.get_track(cylinder, side)
            track.format_track(cylinder, side, spec)
            for index in range(sectors):
                sector = track.get_sector(index)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    raise fail(f"MDOS: formatter did not produce sector {index + 1} on cylinder {cylinder} side "
                               f"{side}")
                sector.write_data(data[src:src + SECTOR_SIZE], recalculate_crc=True)
                src += SECTOR_SIZE
    disk.format = "mdos"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        return parse(f.read())


def regular_geometry_problem(disk: DiskImage) -> tuple[int, str]:
    """LoaderMDOS::isRegularGeometry: (sectors a track, "") when every track carries the same sectors 1..N of 512
    bytes, else (0, why not)"""
    first = disk.get_track(0, 0)
    expected = first.sector_count() if first is not None else 0
    if expected == 0 or expected > MAX_SECTORS:
        return 0, f"Track cylinder 0 side 0 has {expected} sectors"
    for cylinder in range(disk.cylinders):
        for side in range(disk.heads):
            track = disk.get_track(cylinder, side)
            if track is None or track.sector_count() != expected:
                return 0, f"Track cylinder {cylinder} side {side} differs from the rest of the disk"
            for number in range(1, expected + 1):
                sector = track.find_index(number)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    return 0, f"Track cylinder {cylinder} side {side}: sector {number} is missing or not 512 bytes"
    return expected, ""


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderMDOS::serialize"""
    sectors, problem = regular_geometry_problem(disk)
    if problem:
        message = "MDOS save refused: " + problem + " (export the disk as UDI to keep it)"
        raise DiskConversionError(message, [message])
    out = bytearray()
    for cylinder in range(disk.cylinders):
        for side in range(disk.heads):
            track = disk.get_track(cylinder, side)
            for number in range(1, sectors + 1):
                out += track.find_index(number).data_bytes()
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
