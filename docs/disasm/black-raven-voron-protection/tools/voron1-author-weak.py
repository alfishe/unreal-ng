#!/usr/bin/env python3
"""Author UDIW weak-bit chunks in UDI images (VORON1 protection authoring).

Writes the "UDIW" weak-map chunk designed in
docs/inprogress/2026-09-22-udi-weak-bit-storage/design.md into the UDI trailer
(comment area, CRC-covered), so the stock emulator's WD1793 FlakySectorEmulator
sees the marked bytes as physically weak - no re-dump of the original media
needed. The chunk layout matches core/src/loaders/disk/loader_udi.cpp exactly:

    "UDIW" | u16 version = 1 | u16 recordCount |
    record: u8 cyl | u8 side | u8 flags | u16 streamOffset | u16 length

The CRC-32 here is a byte-exact port of CRCHelper::crcUDI (signed int
accumulator, arithmetic shift). Before modifying anything the tool verifies its
CRC against the input file's stored CRC - if that check fails, the port or the
file is wrong and nothing is written.

Outputs are restricted to the scratch/ directory (test-artifact rule).
Live verification of authored images runs on the Pentagon 128 machine config
only - see the tools README.

Usage:
  voron1-author-weak.py IMAGE.udi --list                 # sector census incl. CRC/lying-C anomalies
  voron1-author-weak.py IN.udi OUT.udi                   # default mark: M1 (cyl 59/h1 R=192 data)
  voron1-author-weak.py IN.udi OUT.udi --mark 59:1:192:data --mark 0:0:9:idam
  voron1-author-weak.py IMAGE.udi --verify               # dump the UDIW chunk records
"""

import argparse
import struct
import sys
from pathlib import Path

SIG = b"UDIW"
VERSION = 1
RECORD = struct.Struct("<BBBHH")  # cylinder, side, flags, streamOffset, length


def crc_udi(data):
    """CRC-32 with the UDI signed-accumulator quirk (CRCHelper::crcUDI port)."""
    def s32(value):
        value &= 0xFFFFFFFF
        return value - 0x100000000 if value & 0x80000000 else value

    crc = -1
    for byte in data:
        crc = s32(crc ^ -1 ^ byte)
        for _ in range(8):
            temp = -(crc & 1)  # 0 or -1 (all ones), like the C int mask
            crc >>= 1  # Python arithmetic shift matches C on the signed value
            crc = s32(crc ^ (0xEDB88320 & temp))
        crc = s32(crc ^ -1)
    return crc & 0xFFFFFFFF


def crc16(data):
    """MFM field CRC as the WD1793 sees it (CRCHelper::crcWD1793 port): crc16-CCITT
    (poly 0x1021) seeded 0xCDB4 - the accumulator value after the three A1 sync bytes,
    which the controller folds into every field CRC. The body is mark + field bytes;
    the on-disk pair is big-endian (true CRC high byte first)."""
    crc = 0xCDB4
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


class Udi:
    def __init__(self, data):
        if data[:4] != b"UDI!":
            raise SystemExit("not an uncompressed UDI file (signature 'UDI!' missing)")
        self.data = data
        self.size = struct.unpack_from("<I", data, 4)[0]  # offset of the trailing CRC
        if self.size + 4 != len(data):
            raise SystemExit(f"UDI size field {self.size} != file size - 4 ({len(data) - 4})")
        stored = struct.unpack_from("<I", data, self.size)[0]
        computed = crc_udi(data[:self.size])
        if stored != computed:
            raise SystemExit(f"CRC mismatch (stored 0x{stored:08X}, computed 0x{computed:08X}) - "
                             "the CRC port or the file is wrong, refusing to touch it")
        self.cylinders = data[9] + 1
        self.sides = data[10] + 1
        ext = struct.unpack_from("<I", data, 12)[0]
        offset = 16 + ext
        self.tracks = {}  # (cyl, side) -> (rawOffset, rawLen, clockOffset)
        for cyl in range(self.cylinders):
            for side in range(self.sides):
                track_type = data[offset]
                if track_type & 0x80:
                    raise SystemExit(f"track ({cyl},{side}) uses the multi-revolution extension, unsupported")
                raw_len = struct.unpack_from("<H", data, offset + 1)[0]
                bitmap_len = (raw_len + 7) // 8
                self.tracks[(cyl, side)] = (offset + 3, raw_len, offset + 3 + raw_len)
                bitmaps = 2 if track_type == 2 else 1
                offset += 3 + raw_len + bitmap_len * bitmaps
        self.trailer_start = offset
        self.trailer = bytearray(data[offset:self.size])


def clock_bit(clock, index):
    """Bit i (LSB first) of the clock bitmap: 1 = byte i carries a missing-clock sync mark."""
    return clock is not None and (clock[index >> 3] >> (index & 7)) & 1


def next_sync(raw, clock, start):
    """Offset of the next clocked A1 A1 A1 triplet at/after start (-1 when none).

    Only triplets whose A1s carry missing-clock marks are real sync fields - data bytes can
    coincidentally contain the same pattern (the strict-mode rule of the C++ track scanner)."""
    pos = start
    while True:
        pos = raw.find(b"\xA1\xA1\xA1", pos)
        if pos < 0:
            return -1
        if clock_bit(clock, pos) and clock_bit(clock, pos + 1) and clock_bit(clock, pos + 2):
            return pos
        pos += 1


def find_sectors(raw, clock=None):
    """Scan an MFM raw stream for ID/data fields: sync A1 A1 A1 FE C H R N ... sync A1 A1 A1 FB/F8 data."""
    sectors = []
    pos = 0
    while True:
        idam = next_sync(raw, clock, pos)
        if idam < 0 or idam + 8 > len(raw):
            break
        if raw[idam + 3] != 0xFE:  # not an ID field - skip this sync (e.g. a data mark)
            pos = idam + 3
            continue
        c, h, r, n = raw[idam + 4], raw[idam + 5], raw[idam + 6], raw[idam + 7]
        dam = next_sync(raw, clock, idam + 4)
        mark = raw[dam + 3] if dam >= 0 else None
        has_data = mark in (0xFB, 0xF8)
        entry = {
            "idam": idam, "c": c, "h": h, "r": r, "n": n,
            "dam": dam + 3 if has_data else None,
            "deleted": mark == 0xF8,
            "data": dam + 4 if has_data else None,
            "dlen": 128 << (n & 3),
        }
        if has_data:
            body = bytes([mark]) + raw[entry["data"]:entry["data"] + entry["dlen"]]
            stored = struct.unpack_from(">H", raw, entry["data"] + entry["dlen"])[0]  # on-disk: high byte first
            entry["data_crc_ok"] = crc16(body) == stored
        sectors.append(entry)
        pos = idam + 4
    return sectors


def strip_chunk(trailer):
    """Remove an existing UDIW chunk from the trailer so it cannot duplicate."""
    pos = bytes(trailer).find(SIG)
    if pos < 0:
        return
    if pos + 8 > len(trailer):
        raise SystemExit("truncated UDIW chunk in input")
    count = struct.unpack_from("<H", trailer, pos + 6)[0]
    end = pos + 8 + count * RECORD.size
    if end > len(trailer):
        raise SystemExit("truncated UDIW chunk in input")
    del trailer[pos:end]


def build_chunk(records):
    out = bytearray(SIG)
    out += struct.pack("<HH", VERSION, len(records))
    for cyl, side, offset, length in records:
        out += RECORD.pack(cyl, side, 0, offset, length)
    return bytes(out)


def locate(mark, udi, sectors_cache):
    """Resolve a cyl:side:R:field mark to (offset, length)."""
    cyl_s, side_s, r_s, field = mark.split(":")
    key = (int(cyl_s), int(side_s))
    if key not in udi.tracks:
        raise SystemExit(f"mark {mark}: track {key} not in image")
    if key not in sectors_cache:
        raw_off, raw_len, clock_off = udi.tracks[key]
        sectors_cache[key] = find_sectors(udi.data[raw_off:raw_off + raw_len],
                                         udi.data[clock_off:clock_off + (raw_len + 7) // 8])
    candidates = [s for s in sectors_cache[key] if s["r"] == int(r_s)]
    if not candidates:
        raise SystemExit(f"mark {mark}: no sector R={r_s} on track {key}")
    sector = candidates[0]
    if field == "data":
        if sector["data"] is None:
            raise SystemExit(f"mark {mark}: sector has no data field")
        return sector["data"], sector["dlen"]
    if field == "idam":
        return sector["idam"], 6
    raise SystemExit(f"mark {mark}: unknown field '{field}' (use data or idam)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", help="stock UDI image to read (also used for --list/--verify)")
    parser.add_argument("output", nargs="?", help="authored copy to write (must be under scratch/)")
    parser.add_argument("--mark", action="append", default=[], metavar="CYL:SIDE:R:FIELD",
                        help="weak mark; FIELD is data|idam (repeatable; default: 59:1:192:data = M1)")
    parser.add_argument("--list", action="store_true", help="print the sector census of the input")
    parser.add_argument("--verify", action="store_true", help="print the UDIW records carried by the input")
    args = parser.parse_args()

    data = Path(args.input).read_bytes()
    udi = Udi(data)

    if args.list:
        for (cyl, side), (raw_off, raw_len, clock_off) in sorted(udi.tracks.items()):
            clock = data[clock_off:clock_off + (raw_len + 7) // 8]
            for s in find_sectors(data[raw_off:raw_off + raw_len], clock):
                if s["c"] != cyl or not s.get("data_crc_ok", True) or s["deleted"]:
                    flag = " ".join(filter(None, [
                        f"ID-CLAIMS-C={s['c']}" if s["c"] != cyl else "",
                        "BAD-DATA-CRC" if not s.get("data_crc_ok", True) else "",
                        "DELETED" if s["deleted"] else "",
                    ]))
                    print(f"cyl {cyl:3d} h{side} R={s['r']:3d} N={s['n']} C={s['c']:3d}: {flag or 'anomaly'}")
        return

    if args.verify:
        pos = bytes(udi.trailer).find(SIG)
        if pos < 0:
            print("no UDIW chunk")
            return
        count = struct.unpack_from("<H", udi.trailer, pos + 6)[0]
        print(f"UDIW chunk at trailer+{pos}: {count} record(s)")
        for i in range(count):
            cyl, side, flags, offset, length = RECORD.unpack_from(udi.trailer, pos + 8 + i * RECORD.size)
            print(f"  cyl {cyl:3d} h{side} flags=0x{flags:02X} offset={offset:5d} length={length}")
        return

    if not args.output:
        parser.error("an output path is required for authoring")
    out_path = Path(args.output).resolve()
    if "scratch" not in out_path.parts:
        raise SystemExit("refusing to write outside scratch/ (test-artifact rule)")

    marks = args.mark or ["59:1:192:data"]  # M1: the acetone spot, design section 6.1
    sectors_cache = {}
    records = []
    for mark in marks:
        offset, length = locate(mark, udi, sectors_cache)
        cyl, side = int(mark.split(":")[0]), int(mark.split(":")[1])
        records.append((cyl, side, offset, length))
        print(f"mark {mark}: stream [{offset}, {offset + length}) weak ({length} bytes)")

    strip_chunk(udi.trailer)
    out = bytearray(data[:udi.trailer_start])
    out += udi.trailer
    out += build_chunk(records)
    struct.pack_into("<I", out, 4, len(out))  # size field = everything before the CRC
    out += struct.pack("<I", crc_udi(bytes(out)))
    out_path.write_bytes(out)
    print(f"wrote {out_path} ({len(out)} bytes, {len(records)} weak record(s))")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:  # e.g. `--list | head`
        sys.exit(141)
