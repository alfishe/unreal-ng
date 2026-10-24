"""FDI - Full Disk Image. See docs/file-formats/disk-images/fdi.md and
docs/inprogress/2026-09-02-universal-track-model/loader-fdi.md (the exact flags convention
used here matches that spec, verified against the real VORON1/VORON2 fixtures).
"""

from __future__ import annotations

import struct

from ..disk_image import DiskImage, DiskConversionError, Sector

SIGNATURE = b"FDI"
HEADER_SIZE = 14

FLAG_DELETED = 0x40
FLAG_NO_DATA = 0x80


def detect(data: bytes) -> bool:
    return data[:3] == SIGNATURE


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()

    if not detect(data):
        raise DiskConversionError(f"{path}: not an FDI file (missing 'FDI' signature)")
    if len(data) < HEADER_SIZE:
        raise DiskConversionError(f"{path}: truncated FDI header")

    write_protected = data[3] != 0
    cylinders, heads, desc_offset, data_offset, extra_len = struct.unpack_from("<HHHHH", data, 4)

    disk = DiskImage(cylinders=cylinders, heads=heads, write_protected=write_protected)
    offset = HEADER_SIZE + extra_len

    for cyl in range(cylinders):
        for head in range(heads):
            if offset + 7 > len(data):
                raise DiskConversionError(f"{path}: truncated at track header cyl={cyl} head={head}")
            track_data_offset = struct.unpack_from("<I", data, offset)[0]
            sector_count = data[offset + 6]
            offset += 7

            trk = disk.track(cyl, head)
            for _ in range(sector_count):
                if offset + 7 > len(data):
                    raise DiskConversionError(f"{path}: truncated in sector list cyl={cyl} head={head}")
                c, h, r, n, flags, doff = struct.unpack_from("<BBBBBH", data, offset)
                offset += 7

                size_code = n & 3
                if n > 3:
                    print(f"warning: {path}: cyl={cyl} head={head} sector={r}: size code {n} masked to {size_code}")

                no_data = bool(flags & FLAG_NO_DATA)
                size = 128 << size_code
                sector_data = b""
                crc_valid = True
                if not no_data:
                    start = data_offset + track_data_offset + doff
                    sector_data = data[start : start + size]
                    if len(sector_data) < size:
                        print(
                            f"warning: {path}: cyl={cyl} head={head} sector={r}: data runs past end of "
                            f"file, treated as ID-only"
                        )
                        no_data = True
                        sector_data = b""
                    else:
                        crc_valid = bool(flags & (1 << size_code))

                trk.sectors.append(
                    Sector(
                        cylinder=c,
                        head=h,
                        number=r,
                        size_code=size_code,
                        data=sector_data,
                        crc_valid=crc_valid,
                        deleted=bool(flags & FLAG_DELETED),
                        no_data=no_data,
                    )
                )

    return disk


def write(disk: DiskImage, path: str) -> None:
    header = bytearray()
    header += SIGNATURE
    header.append(1 if disk.write_protected else 0)
    header += struct.pack("<HHHH", disk.cylinders, disk.heads, 0, 0)  # desc/data offsets patched below
    header += struct.pack("<H", 0)  # extra header length

    track_headers = bytearray()
    data_area = bytearray()

    for cyl in range(disk.cylinders):
        for head in range(disk.heads):
            trk = disk.get_track(cyl, head)
            sectors = trk.sectors if trk else []

            track_data_offset = len(data_area)
            track_headers += struct.pack("<IHB", track_data_offset, 0, len(sectors))

            for sector in sectors:
                doff = len(data_area) - track_data_offset
                if sector.no_data:
                    flags = FLAG_NO_DATA
                else:
                    flags = (1 << sector.size_code) if sector.crc_valid else 0
                    if sector.deleted:
                        flags |= FLAG_DELETED
                    data_area += sector.data

                track_headers += struct.pack(
                    "<BBBBBH", sector.cylinder, sector.head, sector.number, sector.size_code, flags, doff
                )

    # header layout: [0:3]="FDI" [3]=write-protect [4:6]=cylinders [6:8]=heads
    # [8:10]=desc offset (left 0, no description) [10:12]=data offset [12:14]=extra header length
    data_offset = HEADER_SIZE + len(track_headers)
    struct.pack_into("<H", header, 10, data_offset)

    with open(path, "wb") as f:
        f.write(header)
        f.write(track_headers)
        f.write(data_area)
