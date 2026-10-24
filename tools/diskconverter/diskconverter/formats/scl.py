"""SCL - TR-DOS file archive. See docs/file-formats/disk-images/scl.md.

File-level, not sector-level: reading builds a blank standard TR-DOS disk and injects each
archived file into it (mirrors loader_scl.cpp); writing walks a disk's TR-DOS catalog and
exports the live (non-deleted) files. Round-tripping *through* a sector-preserving format
(TRD/FDI/UDI) in between reproduces the same files byte-for-byte, since the catalog and
file data are ordinary standard-geometry sectors once written.
"""

from __future__ import annotations

import struct

from ..disk_image import DiskImage, DiskConversionError
from .. import trdos

SIGNATURE = b"SINCLAIR"
DIR_ENTRY_SIZE = 14


def detect(data: bytes) -> bool:
    return data[:8] == SIGNATURE


def _checksum(data: bytes) -> int:
    """SCL's trailing 4-byte "CRC" is actually a plain wrapping sum of every preceding
    byte (see loader_scl.cpp's writeImage/checkSCLFileCRC - the field is misnamed, this is
    what real SCL files and this codebase's own writer both do)."""
    total = 0
    for byte in data:
        total = (total + byte) & 0xFFFFFFFF
    return total


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()

    if not detect(data):
        raise DiskConversionError(f"{path}: not an SCL file (missing 'SINCLAIR' signature)")
    if len(data) > 4:
        stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        computed = _checksum(data[:-4])
        if stored != computed:
            print(f"warning: {path}: trailing checksum mismatch (stored 0x{stored:08X}, computed 0x{computed:08X})")
        data = data[:-4]

    file_count = data[8]
    offset = 9
    entries = []
    for _ in range(file_count):
        raw = data[offset : offset + DIR_ENTRY_SIZE]
        offset += DIR_ENTRY_SIZE
        name = raw[0:8].decode("ascii", errors="replace")
        ftype = chr(raw[8])
        start = raw[9] | (raw[10] << 8)
        length = raw[11] | (raw[12] << 8)
        sectors = raw[13]
        entries.append((name, ftype, start, length, sectors))

    disk = trdos.blank_trdos_disk(cylinders=80, heads=2)
    files: list[tuple[trdos.TrdosFileEntry, bytes]] = []
    for name, ftype, start, length, sectors in entries:
        size = sectors * trdos.SECTOR_SIZE
        chunk = data[offset : offset + size]
        offset += size
        if len(chunk) < size:
            raise DiskConversionError(f"{path}: file '{name.rstrip()}' truncated (expected {size} bytes, got {len(chunk)})")
        entry = trdos.TrdosFileEntry(
            name=name, type=ftype, start=start, length=length, sectors=sectors, first_sector=0, first_track=0
        )
        files.append((entry, chunk))

    trdos.write_catalog_and_files(disk, files)
    return disk


def write(disk: DiskImage, path: str) -> None:
    if not trdos.is_trdos(disk):
        raise DiskConversionError(
            "SCL export requires a TR-DOS-formatted disk (valid catalog + disk-info sector); "
            "this disk has none. If it came from a non-TR-DOS FDI/UDI, SCL cannot represent it."
        )
    if not disk.is_standard_trdos_geometry():
        print(
            f"warning: {path}: disk has non-standard tracks outside the catalog; SCL only stores "
            f"catalogued files, so this is likely fine, but any raw sectors relying on non-standard "
            f"geometry are silently dropped"
        )

    entries = trdos.parse_catalog(disk)
    live = [e for e in entries if not e.deleted]

    out = bytearray()
    out += SIGNATURE
    out.append(len(live) & 0xFF)
    for entry in live:
        out += entry.name.encode("ascii", errors="replace")[:8].ljust(8, b" ")
        out += entry.type.encode("ascii")
        out += struct.pack("<HH", entry.start, entry.length)
        out.append(entry.sectors)

    for entry in live:
        out += trdos.read_file_data(disk, entry, disk.heads)

    out += struct.pack("<I", _checksum(bytes(out)))

    with open(path, "wb") as f:
        f.write(out)
