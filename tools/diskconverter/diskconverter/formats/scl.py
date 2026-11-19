"""SCL - TR-DOS file archive (port of unreal-disk src/loaders/loader_scl.cpp).

Reading formats an empty 80 x 2 TR-DOS disk (as the TRD loader does) and adds the archive's files one after
another from logical track 1 sector 0, updating the catalogue and the volume sector. Writing walks the catalogue
up to the first entry starting with 0x00, skips deleted entries (0x01) and files whose sector chain leaves the
disk, and stores the rest: "SINCLAIR", count, 14-byte descriptors, the data, a 32-bit byte sum.
"""

from __future__ import annotations

import struct

from ..disk_image import DiskConversionError, DiskImage
from . import trd

SIGNATURE = b"SINCLAIR"
DIR_ENTRY_SIZE = 14
MAX_FILES = 128
SECTOR_SIZE = 256
FREE_SECTORS_DS80 = (80 * 2 - 1) * 16


def detect(data: bytes) -> bool:
    return data[:8] == SIGNATURE


def checksum(data) -> int:
    return sum(data) & 0xFFFFFFFF


def _add_file(disk: DiskImage, descriptor: bytes, data: bytes) -> bool:
    """LoaderSCL::addFile"""
    track = disk.track_at(0)
    system = track.get_sector(trd.VOLUME_SECTOR)
    info = bytearray(system.data_bytes())
    file_count = info[0xE4]
    if file_count >= MAX_FILES:
        return False
    length = descriptor[13]
    catalog_offset = file_count * 16
    free = info[0xE5] | (info[0xE6] << 8)
    if free < length:
        return False
    dir_sector = track.get_sector((catalog_offset // SECTOR_SIZE) & 0x0F)
    entry_at = catalog_offset & 0xFF
    first_sector, first_track = info[0xE1], info[0xE2]
    entry = bytearray(dir_sector.data_bytes())
    entry[entry_at:entry_at + 14] = descriptor[:14]
    entry[entry_at + 14] = first_sector
    entry[entry_at + 15] = first_track
    dir_sector.write_data(entry, recalculate_crc=True)

    locator = (first_track * 16 + first_sector) & 0xFFFF
    new_locator = (locator + length) & 0xFFFF
    info[0xE1] = new_locator & 0x0F
    info[0xE2] = (new_locator >> 4) & 0xFF
    info[0xE4] = (info[0xE4] + 1) & 0xFF
    free = (free - length) & 0xFFFF
    info[0xE5] = free & 0xFF
    info[0xE6] = free >> 8
    system.write_data(info, recalculate_crc=True)

    for i in range(length):
        loc = (locator + i) & 0xFFFF
        file_track = disk.track_at((loc // 16) & 0xFF)
        file_sector = file_track.get_sector(loc % 16)
        file_sector.write_data(data[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE], recalculate_crc=True)
    return True


def parse(data: bytes) -> DiskImage:
    """LoaderSCL::loadImage / loadSCLData"""
    disk = DiskImage(80, 2)
    trd.format_disk(disk)
    warnings: list[str] = []

    def fail(message: str = "not a valid SCL image") -> DiskConversionError:
        return DiskConversionError(message, warnings)

    if len(data) < 9:
        raise fail()
    if len(data) <= 4 or struct.unpack_from("<I", data, len(data) - 4)[0] != checksum(data[:-4]):
        raise fail()
    if data[:8] != SIGNATURE:
        raise fail()
    count = data[8]
    data_start = 9 + count * DIR_ENTRY_SIZE
    if data_start > len(data):
        # C++ reads the descriptors past the buffer here (it sums their sizes before the length check); a
        # descriptor table longer than the file can only fail that check
        warnings.append("SCL archive is shorter than its catalog says")
        raise fail(warnings[-1])
    total = sum(data[9 + i * DIR_ENTRY_SIZE + 13] for i in range(count))
    if data_start + total * SECTOR_SIZE > len(data):
        warnings.append("SCL archive is shorter than its catalog says")
        raise fail(warnings[-1])
    if total > FREE_SECTORS_DS80:
        raise fail()
    offset = data_start
    for i in range(count):
        descriptor = data[9 + i * DIR_ENTRY_SIZE:9 + (i + 1) * DIR_ENTRY_SIZE]
        _add_file(disk, descriptor, data[offset:offset + descriptor[13] * SECTOR_SIZE])
        offset += descriptor[13] * SECTOR_SIZE
    disk.format = "scl"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        return parse(f.read())


def _c_string(raw: bytes) -> str:
    """%s of a std::string: the text up to the first NUL"""
    return raw.split(b"\0", 1)[0].decode("latin-1")


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderSCL::writeImage"""
    warnings: list[str] = []

    def refuse(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    track0 = disk.get_track(0, 0)
    if track0 is None:
        raise refuse("SCL save refused: track 0 is missing")
    catalog = []
    for sector_no in range(trd.VOLUME_SECTOR + 1):
        sector = track0.get_sector(sector_no)
        if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
            raise refuse(f"SCL save refused: track 0 sector {sector_no + 1} is not a 256-byte TR-DOS sector")
        catalog.append(sector.data_bytes())
    info = catalog[trd.VOLUME_SECTOR]
    if info[0xE7] != trd.TRD_SIGNATURE:
        raise refuse("SCL save refused: no TR-DOS signature in the volume sector")
    if info[0xE4] > MAX_FILES:
        raise refuse("SCL save refused: invalid file count in the volume sector")

    def file_sector(locator: int):
        track = disk.track_at((locator // 16) & 0xFF)
        return track.get_sector(locator % 16) if track else None

    files = []
    for i in range(MAX_FILES):
        entry = catalog[i // 16][(i % 16) * 16:(i % 16) * 16 + 16]
        if entry[0] == 0x00:
            break
        if entry[0] == 0x01:
            continue
        locator = (entry[15] * 16 + entry[14]) & 0xFFFF
        valid = True
        for n in range(entry[13]):
            sector = file_sector((locator + n) & 0xFFFF)
            if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
                valid = False
                break
        if not valid:
            warnings.append(f"SCL save: file '{_c_string(entry[:8])}' skipped - its sector chain points outside "
                            f"the TR-DOS geometry")
            continue
        files.append(entry)

    out = bytearray(SIGNATURE)
    out.append(len(files))
    for entry in files:
        out += entry[:DIR_ENTRY_SIZE]
    for entry in files:
        locator = (entry[15] * 16 + entry[14]) & 0xFFFF
        for n in range(entry[13]):
            out += file_sector((locator + n) & 0xFFFF).data_bytes()
    out += struct.pack("<I", checksum(out))
    return bytes(out), warnings


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
