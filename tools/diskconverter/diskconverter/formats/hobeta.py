"""Hobeta ($-file: .$b, .$c, ...) - port of unreal-disk src/loaders/loader_hobeta.cpp (the loading half).

Not a disk image: one TR-DOS file with a 17-byte header. Loading formats a blank 80 x 2 TR-DOS disk (as the TRD
loader does) and adds the file the way LoaderHobeta::addFile does: catalogue entry 0, the body from the first free
track / sector of the volume sector (track 1 sector 0), the volume sector updated.

Header (little-endian): name (8, space padded), type, start (2), length (2), reserved (1, not stored on the disk),
sectors (1), checksum (2) = sum(i = 0..14) header[i] * 257 + i (16-bit); then sectors x 256 bytes.

A disk is never saved as Hobeta: FloppyFormats::Save refuses a $-extension (the exporter writes one catalogue file,
which is not a disk conversion).
"""

from __future__ import annotations

from dataclasses import dataclass

from ..disk_image import DiskConversionError, DiskImage
from . import trd

HEADER_SIZE = 17
CHECKSUM_OFFSET = 15
NAME_SIZE = 8
SECTOR_SIZE = 256
SECTORS_PER_TRACK = 16
MAX_FILES = 128
SAVE_REFUSAL = "a Hobeta file holds one TR-DOS file, not a disk: save the disk as .trd, .scl or .udi"


@dataclass
class Header:
    name: bytes
    type: int
    start: int
    length: int
    reserved: int
    sectors: int
    checksum: int

    def descriptor(self) -> bytes:
        """TRDOSDirectoryEntryBase: name, type, start, length, size in sectors (14 bytes)"""
        return (self.name + bytes([self.type, self.start & 0xFF, self.start >> 8, self.length & 0xFF,
                                   self.length >> 8, self.sectors]))


def checksum(header: bytes) -> int:
    """LoaderHobeta::computeChecksum over the first 15 header bytes"""
    total = 0
    for i in range(CHECKSUM_OFFSET):
        total = (total + header[i] * 257 + i) & 0xFFFF
    return total


def parse_header(data: bytes, warnings: list[str]) -> Header | None:
    """LoaderHobeta::parseHeader: the header when its checksum and its sector count (against len(data)) hold"""
    if len(data) < HEADER_SIZE:
        warnings.append(f"Hobeta: file is {len(data)} bytes, shorter than the 17-byte header")
        return None
    header = Header(bytes(data[:NAME_SIZE]), data[8], data[9] | (data[10] << 8), data[11] | (data[12] << 8), data[13],
                    data[14], data[15] | (data[16] << 8))
    expected = checksum(data)
    if header.checksum != expected:
        warnings.append(f"Hobeta: header checksum mismatch: stored 0x{header.checksum:04X}, computed 0x{expected:04X}")
        return None
    expected_size = HEADER_SIZE + header.sectors * SECTOR_SIZE
    if len(data) != expected_size:
        warnings.append(f"Hobeta: header declares {header.sectors} sectors ({expected_size} bytes with header), file "
                        f"is {len(data)} bytes")
        return None
    return header


def detect(data: bytes) -> bool:
    """LoaderHobeta::detect on the whole file"""
    return parse_header(data, []) is not None


def _display_name(name: bytes) -> str:
    return name[:NAME_SIZE].decode("latin-1").rstrip(" ")


def _system_track_problem(disk: DiskImage) -> str:
    """LoaderHobeta::hasTrdosSystemTrack: "" when track 0 sectors 1..9 are 256-byte sectors and the volume sector
    carries the TR-DOS id"""
    track0 = disk.get_track(0, 0)
    if track0 is None:
        return "track 0 is missing"
    for sector_no in range(trd.VOLUME_SECTOR + 1):
        sector = track0.get_sector(sector_no)
        if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
            return f"track 0 sector {sector_no + 1} is not a 256-byte TR-DOS sector"
    if track0.get_sector(trd.VOLUME_SECTOR).data_bytes()[0xE7] != trd.TRD_SIGNATURE:
        return "no TR-DOS signature in the volume sector"
    return ""


def _sector_for_locator(disk: DiskImage, locator: int):
    """LoaderHobeta::sectorForLocator"""
    track_no = locator // SECTORS_PER_TRACK
    if track_no >= disk.cylinders * disk.heads:
        return None
    track = disk.track_at(track_no)
    sector = track.get_sector(locator % SECTORS_PER_TRACK) if track else None
    if sector is None or not sector.has_data or sector.size != SECTOR_SIZE:
        return None
    return sector


def add_file(disk: DiskImage, descriptor: bytes, body: bytes, warnings: list[str]) -> bool:
    """LoaderHobeta::addFile: validate everything first, then the catalogue entry, the volume sector and the body"""
    problem = _system_track_problem(disk)
    if problem:
        warnings.append("Hobeta inject refused: " + problem)
        return False
    track0 = disk.get_track(0, 0)
    volume = track0.get_sector(trd.VOLUME_SECTOR)
    info = bytearray(volume.data_bytes())
    file_count = info[0xE4]
    free = info[0xE5] | (info[0xE6] << 8)
    first_sector, first_track = info[0xE1], info[0xE2]
    sectors = descriptor[13]
    if file_count >= MAX_FILES:
        warnings.append(f"Hobeta inject refused: TR-DOS catalog is full ({MAX_FILES} files)")
        return False
    if free < sectors:
        warnings.append(f"Hobeta inject refused: '{_display_name(descriptor)}' needs {sectors} sectors, only {free} "
                        f"free")
        return False
    first_locator = (first_track * SECTORS_PER_TRACK + first_sector) & 0xFFFF
    end_locator = first_locator + sectors
    if end_locator > 0xFFFF:
        warnings.append("Hobeta inject refused: first free sector in the volume sector is out of range")
        return False
    for n in range(sectors):
        locator = first_locator + n
        if _sector_for_locator(disk, locator) is None:
            warnings.append(f"Hobeta inject refused: track {locator // SECTORS_PER_TRACK} sector "
                            f"{locator % SECTORS_PER_TRACK + 1} is not a 256-byte TR-DOS sector")
            return False

    dir_sector = track0.get_sector(file_count // 16)
    catalog = bytearray(dir_sector.data_bytes())
    at = (file_count % 16) * 16
    catalog[at:at + 14] = descriptor[:14]
    catalog[at + 14] = first_sector
    catalog[at + 15] = first_track
    dir_sector.write_data(catalog, recalculate_crc=True)

    info[0xE1] = end_locator % SECTORS_PER_TRACK
    info[0xE2] = (end_locator // SECTORS_PER_TRACK) & 0xFF
    info[0xE4] = (file_count + 1) & 0xFF
    free = (free - sectors) & 0xFFFF
    info[0xE5] = free & 0xFF
    info[0xE6] = free >> 8
    volume.write_data(info, recalculate_crc=True)

    for n in range(sectors):
        _sector_for_locator(disk, first_locator + n).write_data(body[n * SECTOR_SIZE:(n + 1) * SECTOR_SIZE],
                                                                recalculate_crc=True)
    return True


def parse(data: bytes) -> DiskImage:
    """LoaderHobeta::loadData: a fresh 80 x 2 TR-DOS disk holding the file"""
    warnings: list[str] = []
    header = parse_header(data, warnings)
    if header is None:
        raise DiskConversionError(warnings[-1], warnings)
    disk = DiskImage(80, 2)
    if not trd.format_disk(disk):
        warnings.append("Hobeta: unable to format a blank TR-DOS disk")
        raise DiskConversionError(warnings[-1], warnings)
    if not add_file(disk, header.descriptor(), data[HEADER_SIZE:], warnings):
        raise DiskConversionError(warnings[-1], warnings)
    disk.format = "hobeta"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        return parse(f.read())


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """FloppyFormats::Save on a $-extension"""
    raise DiskConversionError(SAVE_REFUSAL, [])
