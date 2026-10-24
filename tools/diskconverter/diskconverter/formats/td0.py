"""TD0 - Sydex Teledisk image. See docs/file-formats/disk-images/td0.md and
core/src/loaders/disk/loader_td0.{h,cpp} (the authoritative byte layout - the flag bit
values below match the C++ source, not the doc's summary table, where they differ).

Read-only, and only for the uncompressed variant (signature "TD"). The per-sector data
encoding (raw / 2-byte-pattern-repeat / chunked RLE) used by *both* TD0 variants is
implemented; the advanced "td" variant's whole-file LZSS+adaptive-Huffman compression
(Teledisk's own LZHUF scheme, ~300 lines of custom decoder in loader_td0.cpp) is not -
`read()` raises a clear error for it rather than guessing. Writing TD0 is not implemented
at all (no title in this project's fixtures needs it as an output format); use FDI or UDI
as the lossless target instead.
"""

from __future__ import annotations

from ..disk_image import DiskImage, DiskConversionError, Sector

HEADER_SIZE = 12
COMMENT_HEADER_SIZE = 10
TRACK_HEADER_SIZE = 4
SECTOR_HEADER_SIZE = 6
END_OF_IMAGE = 0xFF

FLAG_DUPLICATE = 0x01
FLAG_DATA_CRC_ERROR = 0x02
FLAG_DELETED = 0x04
FLAG_DOS_UNALLOCATED = 0x10
FLAG_NO_DATA = 0x20
FLAG_NO_ID = 0x40
FLAGS_NO_DATA_BLOCK = FLAG_DOS_UNALLOCATED | FLAG_NO_DATA

STEPPING_COMMENT = 0x80
RATE_FM = 0x80

ENCODING_RAW = 0
ENCODING_PATTERN = 1
ENCODING_RLE = 2


def detect(data: bytes) -> bool:
    return len(data) >= 2 and data[0:2] in (b"TD", b"td")


def _crc16(data: bytes) -> int:
    """Teledisk's own CRC-16 (poly 0xA097, init 0) - not the WD1793 MFM CRC."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0xA097) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def _decode_data_block(encoding: int, payload: bytes, sector_size: int) -> bytes:
    out = bytearray()
    if encoding == ENCODING_RAW:
        out += payload[:sector_size]
    elif encoding == ENCODING_PATTERN:
        if len(payload) < 4:
            raise DiskConversionError("TD0: truncated pattern-encoded sector data block")
        count = payload[0] | (payload[1] << 8)
        for _ in range(count):
            if len(out) >= sector_size:
                break
            out.append(payload[2])
            if len(out) < sector_size:
                out.append(payload[3])
    elif encoding == ENCODING_RLE:
        p = 0
        while p + 2 <= len(payload) and len(out) < sector_size:
            length_code = payload[p]
            count = payload[p + 1]
            p += 2
            if length_code == 0:
                if p + count > len(payload):
                    raise DiskConversionError("TD0: truncated literal run in RLE-encoded sector data block")
                take = min(count, sector_size - len(out))
                out += payload[p : p + take]
                p += count
            else:
                pattern_len = length_code * 2
                if p + pattern_len > len(payload):
                    raise DiskConversionError("TD0: truncated pattern in RLE-encoded sector data block")
                for _ in range(count):
                    if len(out) >= sector_size:
                        break
                    take = min(pattern_len, sector_size - len(out))
                    out += payload[p : p + take]
                p += pattern_len
    else:
        raise DiskConversionError(f"TD0: unknown sector data encoding {encoding}")

    if len(out) < sector_size:
        out += bytes(sector_size - len(out))
    return bytes(out[:sector_size])


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()

    if not detect(data):
        raise DiskConversionError(f"{path}: not a TD0 file (missing 'TD'/'td' signature)")
    if len(data) < HEADER_SIZE:
        raise DiskConversionError(f"{path}: truncated TD0 header")
    if data[0:1] == b"t":
        raise DiskConversionError(
            f"{path}: advanced (LZSS-compressed, 'td' signature) TD0 is not supported by this tool - "
            f"its whole-file LZHUF decoder was not ported (see core/src/loaders/disk/loader_td0.cpp for "
            f"the reference implementation). Decompress it with the C++ core/unreal-ng first, or convert "
            f"via another tool to FDI/UDI, then use this converter."
        )

    stored_crc = data[10] | (data[11] << 8)
    computed_crc = _crc16(data[:10])
    if stored_crc != computed_crc:
        print(f"warning: {path}: header CRC mismatch (stored 0x{stored_crc:04X}, computed 0x{computed_crc:04X})")

    data_rate = data[5]
    stepping = data[7]
    sides_byte = data[9]
    heads = 1 if sides_byte == 1 else 2

    offset = HEADER_SIZE
    if stepping & STEPPING_COMMENT:
        if offset + COMMENT_HEADER_SIZE > len(data):
            raise DiskConversionError(f"{path}: truncated inside the comment block header")
        comment_length = data[offset + 2] | (data[offset + 3] << 8)
        offset += COMMENT_HEADER_SIZE + comment_length

    disk = DiskImage(cylinders=0, heads=heads)  # cylinder count finalized once every track is seen
    max_cyl = -1

    while True:
        if offset >= len(data):
            print(f"warning: {path}: no end-of-image marker (0xFF track header) - file may be truncated")
            break
        if data[offset] == END_OF_IMAGE:
            break
        if offset + TRACK_HEADER_SIZE > len(data):
            raise DiskConversionError(f"{path}: truncated inside a track header")

        sector_count = data[offset]
        cylinder = data[offset + 1]
        head = data[offset + 2] & 0x7F
        offset += TRACK_HEADER_SIZE
        max_cyl = max(max_cyl, cylinder)

        sectors = []
        for _ in range(sector_count):
            if offset + SECTOR_HEADER_SIZE > len(data):
                raise DiskConversionError(f"{path}: truncated inside the sector list of cyl={cylinder} head={head}")
            s_cyl, s_head, number, size_code, flags, sector_crc = data[offset : offset + SECTOR_HEADER_SIZE]
            offset += SECTOR_HEADER_SIZE

            has_data_block = not (flags & FLAGS_NO_DATA_BLOCK)
            sector_data = b""
            no_data = not has_data_block
            crc_valid = not (flags & FLAG_DATA_CRC_ERROR)

            if has_data_block:
                if offset + 2 > len(data):
                    raise DiskConversionError(f"{path}: truncated at the data block of cyl={cylinder} head={head} sector={number}")
                block_length = data[offset] | (data[offset + 1] << 8)
                offset += 2
                if block_length == 0 or offset + block_length > len(data):
                    raise DiskConversionError(f"{path}: truncated inside the data block of cyl={cylinder} head={head} sector={number}")

                if size_code > 3:
                    print(
                        f"warning: {path}: cyl={cylinder} head={head} sector={number}: size code {size_code} "
                        f"has no WD1793 equivalent (>1024 bytes) - sector skipped"
                    )
                    offset += block_length
                    continue

                encoding = data[offset]
                payload = data[offset + 1 : offset + block_length]
                sector_data = _decode_data_block(encoding, payload, 128 << size_code)
                offset += block_length
            else:
                size_code = min(size_code, 3)

            if flags & FLAG_NO_ID:
                print(f"warning: {path}: cyl={cylinder} head={head}: sector {number} has data but no ID field, skipped")
                continue

            sectors.append(
                Sector(
                    cylinder=s_cyl,
                    head=s_head,
                    number=number,
                    size_code=size_code,
                    data=sector_data,
                    crc_valid=crc_valid,
                    deleted=bool(flags & FLAG_DELETED),
                    no_data=no_data,
                )
            )

        disk.track(cylinder, head).sectors = sectors

    disk.cylinders = max_cyl + 1
    if data_rate & RATE_FM:
        print(f"warning: {path}: single-density (FM) image - this tool's Sector model does not carry encoding, treated as MFM byte content")

    return disk


def write(disk: DiskImage, path: str) -> None:
    raise DiskConversionError(
        "writing TD0 is not implemented by this tool (no fixture in this project needs it as an "
        "output format) - convert to FDI or UDI instead, both of which are lossless for sector "
        "content."
    )
