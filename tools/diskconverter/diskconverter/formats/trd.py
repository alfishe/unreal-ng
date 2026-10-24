"""TRD - raw TR-DOS sector dump. See docs/file-formats/disk-images/trd.md.

Sequential dump, cylinder-major: for each cylinder, for each head, 16 sectors of 256
bytes each (numbers 1..16 in that order). No CHRN header, no CRCs, no gaps - the format
*is* the standard TR-DOS geometry, so anything non-standard simply cannot be written here.
"""

from __future__ import annotations

import sys

from ..disk_image import DiskImage, DiskConversionError, Sector
from .. import trdos

SECTOR_SIZE = 256
SECTORS_PER_TRACK = 16
TRACK_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK

# Track 0/head 0 is always the first TRACK_SIZE bytes of the file (cylinder-major layout),
# regardless of the disk's overall geometry - so the disk-info sector's disk-type byte
# (sector 9 = index 8, offset 0xE3 within it) sits at this fixed absolute offset.
_DISK_TYPE_OFFSET = 8 * SECTOR_SIZE + 0xE3


def _guess_geometry(data: bytes) -> tuple[int, int, list[str]]:
    warnings: list[str] = []
    size = len(data)

    disk_type = data[_DISK_TYPE_OFFSET] if size > _DISK_TYPE_OFFSET else None
    geometry = trdos._GEOMETRY_BY_DISK_TYPE.get(disk_type) if disk_type is not None else None
    if geometry is not None:
        cylinders, heads = geometry
        if cylinders * heads * TRACK_SIZE > size:
            warnings.append(
                f"disk-info geometry ({cylinders}x{heads}) needs {cylinders * heads * TRACK_SIZE} bytes, "
                f"file has {size} - file is truncated after the last used cylinder (per spec, this is "
                f"normal); missing tracks are read as blank"
            )
        return cylinders, heads, warnings

    warnings.append("no valid TR-DOS disk-info sector found - guessing geometry from file size alone")
    if size <= 163840:
        return 40, 1, warnings
    if size <= 327680:
        return 80, 1, warnings
    return 80, 2, warnings


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()

    cylinders, heads, warnings = _guess_geometry(data)
    for w in warnings:
        print(f"warning: {path}: {w}", file=sys.stderr)

    disk = DiskImage(cylinders=cylinders, heads=heads)
    offset = 0
    for cyl in range(cylinders):
        for head in range(heads):
            trk = disk.track(cyl, head)
            for r in range(1, 17):
                chunk = data[offset : offset + SECTOR_SIZE]
                if len(chunk) < SECTOR_SIZE:
                    chunk = chunk + bytes(SECTOR_SIZE - len(chunk))
                trk.sectors.append(Sector(cylinder=cyl, head=head, number=r, size_code=1, data=bytes(chunk)))
                offset += SECTOR_SIZE
    return disk


def write(disk: DiskImage, path: str) -> None:
    if not disk.is_standard_trdos_geometry():
        raise DiskConversionError(
            "TRD can only store standard 16x256-byte TR-DOS tracks (sectors 1..16, valid CRC, "
            "not deleted, no ID-only sectors). This disk has a non-conforming track - "
            "TRD would silently corrupt it, so refusing to write. Use UDI instead (it can store "
            "anything short of weak bits) or FDI (preserves CHRN/flags but not gaps/clock marks)."
        )
    if (disk.cylinders, disk.heads) not in trdos._DISK_TYPE_BY_GEOMETRY:
        raise DiskConversionError(
            f"TRD only defines geometries {sorted(trdos._DISK_TYPE_BY_GEOMETRY)}, disk is "
            f"({disk.cylinders}, {disk.heads})"
        )

    with open(path, "wb") as f:
        for cyl in range(disk.cylinders):
            for head in range(disk.heads):
                trk = disk.track(cyl, head)
                for r in range(1, 17):
                    sector = trk.find(r)
                    f.write(sector.data)
