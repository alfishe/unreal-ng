"""TR-DOS catalog: shared by the SCL reader/writer and anything that needs to walk a
standard 16x256 TR-DOS-formatted disk's file table.

Layout mirrors core/src/emulator/io/fdc/trdoscatalog.{h,cpp} exactly (16-byte catalog
entries in track 0 sectors 1-8, disk-info fields in track 0 sector 9 at 0xE1-0xE7) so a
disk this tool builds is byte-identical to what the C++ loader would produce, and a disk
the C++ loader/emulator produces parses correctly here.
"""

from __future__ import annotations

from dataclasses import dataclass

from .disk_image import DiskImage, DiskConversionError

MAX_FILES = 128
SECTOR_SIZE = 256

DISK_TYPE_DS80 = 0x16
DISK_TYPE_DS40 = 0x17
DISK_TYPE_SS80 = 0x18
DISK_TYPE_SS40 = 0x19

_DISK_TYPE_BY_GEOMETRY = {
    (80, 2): DISK_TYPE_DS80,
    (40, 2): DISK_TYPE_DS40,
    (80, 1): DISK_TYPE_SS80,
    (40, 1): DISK_TYPE_SS40,
}
_GEOMETRY_BY_DISK_TYPE = {v: k for k, v in _DISK_TYPE_BY_GEOMETRY.items()}


@dataclass
class TrdosFileEntry:
    name: str  # 8 chars, space-padded, as stored
    type: str  # 'B' BASIC, 'C' code, 'D' data, '#' screen, ...
    start: int
    length: int
    sectors: int
    first_sector: int  # 0..15
    first_track: int  # logical: cylinder * heads + head
    deleted: bool = False

    @property
    def trimmed_name(self) -> str:
        return self.name.rstrip(" ")


FIRST_DATA_TRACK = 1  # logical: the first track after the catalog (cylinder 0 head 1 on a double-sided disk)


def logical_track(cylinder: int, head: int, heads: int) -> int:
    return cylinder * heads + head


def physical_track(logical: int, heads: int) -> tuple[int, int]:
    return divmod(logical, heads)


def _put_sector(disk: DiskImage, cylinder: int, head: int, number: int, data: bytes) -> None:
    """Overwrite a formatted sector's data field (CRC recalculated), as the C++ loaders' writeData"""
    sector = disk.track(cylinder, head).find_index(number)
    if sector is None or not sector.has_data:
        raise DiskConversionError(f"cylinder {cylinder} head {head}: sector {number} is not formatted")
    sector.write_data(data.ljust(SECTOR_SIZE, b"\0")[:SECTOR_SIZE], recalculate_crc=True)


def blank_trdos_disk(cylinders: int = 80, heads: int = 2, label: str = "") -> DiskImage:
    """An empty, formatted TR-DOS disk, as LoaderTRD::format builds it (every track laid out with 16 x 256-byte
    sectors, the catalogue empty, the volume sector filled in), with an optional disk label."""
    from .formats import trd

    if (cylinders, heads) not in _DISK_TYPE_BY_GEOMETRY:
        raise DiskConversionError(
            f"TR-DOS only defines geometries {sorted(_DISK_TYPE_BY_GEOMETRY)}, got "
            f"({cylinders}, {heads})"
        )
    disk = DiskImage(cylinders=cylinders, heads=heads)
    trd.format_disk(disk)
    if label:
        info = bytearray(disk.sector_bytes(0, 0, 9))
        info[0xF5:0xFD] = label.encode("ascii", errors="replace")[:8].ljust(8)
        _put_sector(disk, 0, 0, 9, bytes(info))
    return disk


def is_trdos(disk: DiskImage) -> bool:
    track0 = disk.get_track(0, 0)
    if track0 is None:
        return False
    info = disk.sector_bytes(0, 0, 9)
    if info is None or len(info) != SECTOR_SIZE:
        return False
    if info[0] != 0x00 or info[0xE7] != 0x10:
        return False
    if info[0xE3] not in _GEOMETRY_BY_DISK_TYPE:
        return False
    for s in range(1, 9):
        if disk.sector_bytes(0, 0, s) is None:
            return False
    return True


def parse_catalog(disk: DiskImage) -> list[TrdosFileEntry]:
    if not is_trdos(disk):
        raise DiskConversionError("not a TR-DOS-formatted disk (catalog/disk-info sector missing or invalid)")

    files: list[TrdosFileEntry] = []
    for slot in range(MAX_FILES):
        sector_data = disk.sector_bytes(0, 0, 1 + slot // 16)
        entry = sector_data[(slot % 16) * 16 : (slot % 16) * 16 + 16]
        if entry[0] == 0x00:
            break
        files.append(
            TrdosFileEntry(
                name=entry[0:8].decode("ascii", errors="replace"),
                type=chr(entry[8]),
                start=entry[9] | (entry[10] << 8),
                length=entry[11] | (entry[12] << 8),
                sectors=entry[13],
                first_sector=entry[14],
                first_track=entry[15],
                deleted=(entry[0] == 0x01),
            )
        )
    return files


def read_file_data(disk: DiskImage, entry: TrdosFileEntry, heads: int) -> bytes:
    """Walk the sector chain starting at (first_track, first_sector), sequential sector
    numbers 1..16 then next logical track, for `entry.sectors` sectors."""
    out = bytearray()
    logical = entry.first_track
    sector_no = entry.first_sector + 1  # catalog stores 0-based, sector numbers are 1-based on disk
    for _ in range(entry.sectors):
        cyl, head = physical_track(logical, heads)
        data = disk.sector_bytes(cyl, head, sector_no)
        if data is None:
            raise DiskConversionError(
                f"file '{entry.trimmed_name}': missing sector (logical track {logical}, sector {sector_no}) "
                f"while reading {entry.sectors} sectors from track {entry.first_track} sector {entry.first_sector}"
            )
        out.extend(data)
        sector_no += 1
        if sector_no > 16:
            sector_no = 1
            logical += 1
    return bytes(out)  # exactly sectors*256 bytes - real TR-DOS/SCL never truncates to `length`


def write_catalog_and_files(disk: DiskImage, files: list[tuple[TrdosFileEntry, bytes]]) -> None:
    """Injects files sequentially starting at logical track 1, sector 1 (mirrors
    loader_scl.cpp), writing both the catalog (track 0) and each file's data sectors."""
    heads = disk.heads
    logical = FIRST_DATA_TRACK
    sector_no = 1

    catalog_bytes = bytearray(SECTOR_SIZE * 8)
    for slot, (entry, data) in enumerate(files):
        if slot >= MAX_FILES:
            raise DiskConversionError(f"more than {MAX_FILES} files - TR-DOS catalog cannot hold them")

        needed_sectors = entry.sectors
        entry.first_track = logical
        entry.first_sector = sector_no - 1

        # Write file data across consecutive sectors, wrapping to the next logical track.
        pos = 0
        cur_logical, cur_sector = logical, sector_no
        for _ in range(needed_sectors):
            cyl, head = physical_track(cur_logical, heads)
            if cyl >= disk.cylinders:
                raise DiskConversionError(
                    f"file '{entry.trimmed_name}' does not fit: disk has only {disk.cylinders} cylinders"
                )
            chunk = data[pos : pos + SECTOR_SIZE]
            if len(chunk) < SECTOR_SIZE:
                chunk = chunk + bytes(SECTOR_SIZE - len(chunk))
            _put_sector(disk, cyl, head, cur_sector, chunk)
            pos += SECTOR_SIZE
            cur_sector += 1
            if cur_sector > 16:
                cur_sector = 1
                cur_logical += 1

        logical, sector_no = cur_logical, cur_sector

        entry_bytes = (
            entry.name.encode("ascii", errors="replace")[:8].ljust(8, b" ")
            + entry.type.encode("ascii")
            + bytes([entry.start & 0xFF, (entry.start >> 8) & 0xFF])
            + bytes([entry.length & 0xFF, (entry.length >> 8) & 0xFF])
            + bytes([entry.sectors, entry.first_sector, entry.first_track])
        )
        catalog_bytes[slot * 16 : slot * 16 + 16] = entry_bytes

    for s in range(8):
        _put_sector(disk, 0, 0, s + 1, bytes(catalog_bytes[s * 256 : s * 256 + 256]))

    used_sectors = (logical - FIRST_DATA_TRACK) * 16 + (sector_no - 1)
    total_data_sectors = (disk.cylinders * heads - 1) * 16
    free = total_data_sectors - used_sectors
    info = bytearray(disk.sector_bytes(0, 0, 9))
    info[0xE1] = (sector_no - 1) & 0xFF
    info[0xE2] = logical & 0xFF
    info[0xE4] = len(files) & 0xFF
    info[0xE5] = free & 0xFF
    info[0xE6] = (free >> 8) & 0xFF
    _put_sector(disk, 0, 0, 9, bytes(info))
