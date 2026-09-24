"""UDI - Ultra Disk Image v1.0. See docs/file-formats/disk-images/udi.md.

The "save anything" format for this project's C++ core (short of weak bits - see
docs/WD1793/FlakySectorEmulator.md §2). Stores each track as a raw MFM byte stream plus a
clock-mark bitmap, so unlike TRD/SCL it can carry any CHRN/geometry this tool's `DiskImage`
model can express; conversion to/from UDI goes through `..mfm` to synthesize/decode that
byte stream from/to structured sectors.
"""

from __future__ import annotations

import struct

from ..disk_image import DiskImage, DiskConversionError
from .. import mfm

SIGNATURE = b"UDI!"
SIGNATURE_COMPRESSED = b"udi!"
TRACK_TYPE_MFM = 0
TRACK_TYPE_FM = 1


def detect(data: bytes) -> bool:
    return data[:4] in (SIGNATURE, SIGNATURE_COMPRESSED)


def _crc_udi(data: bytes) -> int:
    """Signed-accumulator CRC-32 (poly 0xEDB88320, init -1) exactly as
    core/src/emulator/io/fdc/fdc.h's crcUDI computes it - see that function's comment for
    why the accumulator's sign matters (arithmetic vs logical right shift)."""
    crc = 0xFFFFFFFF
    for byte in data:
        crc = (crc ^ (0xFFFFFFFF ^ byte)) & 0xFFFFFFFF
        for _ in range(8):
            lsb = crc & 1
            temp = 0xFFFFFFFF if lsb else 0
            sign_bit = crc & 0x80000000
            crc = (crc >> 1) | (0x80000000 if sign_bit else 0)
            crc = (crc ^ (0xEDB88320 & temp)) & 0xFFFFFFFF
        crc = (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF
    return crc


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()

    if not detect(data):
        raise DiskConversionError(f"{path}: not a UDI file (missing 'UDI!' signature)")
    if data[:4] == SIGNATURE_COMPRESSED:
        raise DiskConversionError(f"{path}: compressed UDI ('udi!') is not supported")

    size_minus_crc = struct.unpack_from("<I", data, 4)[0]
    # 0x08=version 0x09=max_cyl 0x0A=max_head 0x0B=reserved
    max_cyl = data[9]
    max_head = data[10]
    ext_header_size = struct.unpack_from("<I", data, 0x0C)[0]

    stored_crc = struct.unpack_from("<I", data, size_minus_crc)[0]
    computed_crc = _crc_udi(data[:size_minus_crc])
    if stored_crc != computed_crc:
        print(f"warning: {path}: CRC-32 mismatch (stored 0x{stored_crc:08X}, computed 0x{computed_crc:08X})")

    cylinders = max_cyl + 1
    heads = max_head + 1
    disk = DiskImage(cylinders=cylinders, heads=heads)

    offset = 0x10 + ext_header_size
    for cyl in range(cylinders):
        for head in range(heads):
            track_type = data[offset]
            tlen = struct.unpack_from("<H", data, offset + 1)[0]
            offset += 3
            if track_type not in (TRACK_TYPE_MFM, TRACK_TYPE_FM):
                raise DiskConversionError(
                    f"{path}: cyl={cyl} head={head}: track type 0x{track_type:02X} "
                    f"(multi-revolution or unknown) is not supported"
                )
            raw = data[offset : offset + tlen]
            offset += tlen
            offset += (tlen + 7) // 8  # skip clock bitmap - decode_track re-derives sync positions itself

            sectors = mfm.decode_track(raw)
            disk.track(cyl, head).sectors = sectors

    return disk


def write(disk: DiskImage, path: str) -> None:
    if disk.cylinders > 256 or disk.heads > 2:
        raise DiskConversionError(f"UDI stores cylinders/heads as single bytes (max_cyl, max_head): geometry {disk.cylinders}x{disk.heads} does not fit")

    body = bytearray()
    body += SIGNATURE
    body += struct.pack("<I", 0)  # size field, patched below
    body.append(0)  # version
    body.append(disk.cylinders - 1)
    body.append(disk.heads - 1)
    body.append(0)  # reserved
    body += struct.pack("<I", 0)  # extended header size

    for cyl in range(disk.cylinders):
        for head in range(disk.heads):
            trk = disk.get_track(cyl, head)
            sectors = trk.sectors if trk else []
            encoded = mfm.encode_track(sectors)

            body.append(TRACK_TYPE_MFM)
            body += struct.pack("<H", len(encoded.raw))
            body += encoded.raw

            bitmap_size = (len(encoded.raw) + 7) // 8
            bitmap = bytearray(bitmap_size)
            for bit_offset in encoded.clock_missing:
                bitmap[bit_offset >> 3] |= 1 << (bit_offset & 7)
            body += bitmap

    struct.pack_into("<I", body, 4, len(body))
    crc = _crc_udi(bytes(body))

    with open(path, "wb") as f:
        f.write(body)
        f.write(struct.pack("<I", crc))
