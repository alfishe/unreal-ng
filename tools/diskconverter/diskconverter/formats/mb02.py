"""MB-02 raw image (.mb2, RealSpectrum .mbd) - port of unreal-disk src/loaders/loader_mb02.cpp.

The 1024-byte sectors of an MB-02 disk in logical order, no header: LSN = 2 * SPT * track + SPT * side +
(sector - 1), sector ids 1..SPT. Recognized by its boot sector (LSN 0): JR (#18) at #00, #02 at #03, #00 at #20 and
#25, 2 surfaces at #08, 1 sector a cluster at #0A, and the geometry it gives (tracks at #04, sectors a track at #06)
matching the file size.

Every track is an IBM MFM track of 1024-byte sectors 1..SPT (filler E5, the widest gap 3 from 40 down to 8 that
fits): 12 500 bytes (500 kbit/s) past 5 sectors, else 6 250. Writing stores the sectors back while every track of
both sides still carries the same sectors 1..N of 1024 bytes.
"""

from __future__ import annotations

from ..disk_image import DEFAULT_TRACK_SIZE_MFM, MAX_CYLINDERS, DiskConversionError, DiskImage, TrackFormatSpec

SECTOR_SIZE = 1024
SIZE_CODE = 3
SIDES = 2
MAX_SECTORS = 22


def boot_geometry(boot: bytes) -> tuple[int, int] | None:
    """LoaderMB02::bootGeometry: (tracks, sectors a track) when the boot sector is an MB-02 one"""
    if len(boot) < 0x26:
        return None
    if boot[0x00] != 0x18 or boot[0x03] != 0x02 or boot[0x20] != 0x00 or boot[0x25] != 0x00:
        return None
    tracks = boot[0x04] | (boot[0x05] << 8)
    sectors = boot[0x06] | (boot[0x07] << 8)
    sides = boot[0x08] | (boot[0x09] << 8)
    cluster = boot[0x0A] | (boot[0x0B] << 8)
    if sides != SIDES or cluster != 1 or tracks == 0 or tracks > MAX_CYLINDERS or sectors == 0 or \
            sectors > MAX_SECTORS:
        return None
    return tracks, sectors


def detect(data: bytes, size: int | None = None) -> bool:
    """LoaderMB02::detect: data holds at least the boot sector, size is the file size (default len(data))"""
    size = len(data) if size is None else size
    geometry = boot_geometry(data) if len(data) >= SECTOR_SIZE else None
    return geometry is not None and size == geometry[0] * SIDES * geometry[1] * SECTOR_SIZE


def track_spec(sectors: int) -> TrackFormatSpec:
    """LoaderMB02::trackSpec"""
    hd = sectors > 5
    spec = None
    for gap3 in range(40, 7, -4):
        spec = TrackFormatSpec.ibm(sectors, SIZE_CODE, 1, gap3, 0xE5)
        spec.track_length = DEFAULT_TRACK_SIZE_MFM * 2 if hd else DEFAULT_TRACK_SIZE_MFM
        if spec.fits():
            break
    return spec


def parse(data: bytes) -> DiskImage:
    """LoaderMB02::parse"""
    warnings: list[str] = []

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    geometry = boot_geometry(data) if len(data) >= SECTOR_SIZE else None
    if geometry is None:
        raise fail("MB-02: no MB-02 boot sector")
    tracks, sectors = geometry
    expected = tracks * SIDES * sectors * SECTOR_SIZE
    if len(data) != expected:
        raise fail(f"MB-02: the boot sector gives {tracks} x 2 x {sectors} sectors ({expected} bytes), the file has "
                   f"{len(data)}")
    spec = track_spec(sectors)
    if not spec.fits():
        raise fail(f"MB-02: {sectors} sectors of 1024 bytes do not fit into a track")

    disk = DiskImage(tracks, SIDES)
    src = 0
    for cylinder in range(tracks):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            track.format_track(cylinder, side, spec)
            for index in range(sectors):
                sector = track.get_sector(index)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    raise fail(f"MB-02: formatter did not produce sector {index + 1} on cylinder {cylinder} side "
                               f"{side}")
                sector.write_data(data[src:src + SECTOR_SIZE], recalculate_crc=True)
                src += SECTOR_SIZE
    disk.format = "mb02"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        return parse(f.read())


def regular_geometry_problem(disk: DiskImage) -> tuple[int, str]:
    """LoaderMB02::isRegularGeometry: (sectors a track, "") when both sides of every cylinder carry the same
    sectors 1..N of 1024 bytes, else (0, why not)"""
    if disk.heads != SIDES:
        return 0, f"Image has {disk.heads} side(s), an MB-02 image has 2"
    first = disk.get_track(0, 0)
    expected = first.sector_count() if first is not None else 0
    if expected == 0 or expected > MAX_SECTORS:
        return 0, f"Track cylinder 0 side 0 has {expected} sectors"
    for cylinder in range(disk.cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            if track is None or track.sector_count() != expected:
                return 0, f"Track cylinder {cylinder} side {side} differs from the rest of the disk"
            for number in range(1, expected + 1):
                sector = track.find_index(number)
                if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                    return 0, (f"Track cylinder {cylinder} side {side}: sector {number} is missing or not 1024 "
                               f"bytes")
    return expected, ""


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderMB02::serialize"""
    sectors, problem = regular_geometry_problem(disk)
    if problem:
        message = "MB-02 save refused: " + problem + " (export the disk as UDI to keep it)"
        raise DiskConversionError(message, [message])
    out = bytearray()
    for cylinder in range(disk.cylinders):
        for side in range(SIDES):
            track = disk.get_track(cylinder, side)
            for number in range(1, sectors + 1):
                out += track.find_index(number).data_bytes()
    return bytes(out), []


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
