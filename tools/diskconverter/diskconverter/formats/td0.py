"""TD0 - Sydex Teledisk image (port of unreal-disk src/loaders/loader_td0.cpp).

Reading: the 12-byte header (its CRC must match), the advanced ("td") body decompressed by `..td0lzh` (Teledisk
LZHUF, as LoaderTD0 decodes it), the optional comment block, then tracks of sector headers with raw / pattern /
RLE data blocks. Each track is laid out with the TR-DOS layout (MFM) or the IBM 3740 layout (FM: the header's
data rate bit 7 or the track header's head bit 7), the size code masked to N & 3 in the ID field, gap 3 shrunk to
16 (MFM) / 10 (FM) and then the track grown when the sectors do not fit; sectors flagged #10 / #20 are ID-only,
#40 (data without an ID) are skipped, #04 writes F8, #02 inverts both data CRC bytes. Data rates 300 / 500 kbps
scale the track length. Tracks absent from the file stay erased.

Writing: uncompressed "TD" with a fresh loader's header (version #15, rate 0 plus bit 7 when every track is FM,
drive type 3, no comment), every sector of every track in stream order, data stored as a 2-byte pattern when the
whole sector repeats one, else raw.
"""

from __future__ import annotations

from ..disk_image import (DEFAULT_TRACK_SIZE_FM, DEFAULT_TRACK_SIZE_MFM, FM, MAX_CYLINDERS, MAX_TRACK_SIZE, MFM,
                          DAM_DELETED, DiskConversionError, DiskImage, TrackFormatSpec, fit_track_spec)

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

RATE_MASK = 0x03
RATE_FM = 0x80
HEAD_FM = 0x80
STEPPING_COMMENT = 0x80

ENCODING_RAW = 0
ENCODING_PATTERN = 1
ENCODING_RLE = 2

DEFAULT_VERSION = 0x15
DEFAULT_DRIVE_TYPE = 3
MIN_GAP_POST_DATA_MFM = 16
MIN_GAP_POST_DATA_FM = 10


def detect(data: bytes) -> bool:
    return len(data) >= 2 and data[0:2] in (b"TD", b"td")


def _crc16(data) -> int:
    """Teledisk's CRC-16 (poly 0xA097, init 0)"""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0xA097) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _crc16_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0xA097) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        table.append(crc)
    return table


_CRC16_TABLE = _crc16_table()


def crc16(data) -> int:
    """LoaderTD0::crc16, table driven"""
    crc = 0
    table = _CRC16_TABLE
    for byte in data:
        crc = ((crc << 8) & 0xFFFF) ^ table[(crc >> 8) ^ byte]
    return crc


def decode_data_block(encoding: int, payload: bytes, sector_size: int) -> tuple[bytes, bool]:
    """LoaderTD0::decodeDataBlock: (sector_size bytes, well formed)"""
    out = bytearray()
    ok = True
    if encoding == ENCODING_RAW:
        out += payload[:sector_size]
        ok = len(payload) >= sector_size
    elif encoding == ENCODING_PATTERN:
        if len(payload) < 4:
            ok = False
        else:
            count = payload[0] | (payload[1] << 8)
            out += (payload[2:4] * count)[:sector_size]
    elif encoding == ENCODING_RLE:
        p = 0
        while p + 2 <= len(payload) and len(out) < sector_size:
            length_code, count = payload[p], payload[p + 1]
            p += 2
            if length_code == 0:
                if p + count > len(payload):
                    ok = False
                    break
                out += payload[p:p + min(count, sector_size - len(out))]
                p += count
            else:
                pattern_len = length_code * 2
                if p + pattern_len > len(payload):
                    ok = False
                    break
                pattern = payload[p:p + pattern_len]
                for _ in range(count):
                    if len(out) >= sector_size:
                        break
                    out += pattern[:sector_size - len(out)]
                p += pattern_len
    else:
        ok = False
    if len(out) < sector_size:
        ok = False
        out += bytes(sector_size - len(out))
    return bytes(out), ok


def _decompress(body: bytes) -> bytes:
    try:
        from .. import td0lzh
    except ImportError as error:  # pragma: no cover - the decoder ships with the package
        raise DiskConversionError(f"advanced (LZHUF-compressed, 'td' signature) TD0 needs diskconverter.td0lzh: "
                                  f"{error}") from error
    try:
        return td0lzh.decompress(body)
    except Exception as error:  # LzhufDecoder::decode never fails; a Python-side failure is reported the C++ way
        raise DiskConversionError("TD0 advanced compression stream could not be decoded",
                                  ["TD0 advanced compression stream could not be decoded"]) from error


def _parse_body(data: bytes, header_fm: bool, stepping: int, warnings: list[str]):
    """LoaderTD0::parseBody: [(cylinder, head, fm, [(c, h, r, n, flags, data)])], or None when truncated"""
    offset = 0
    size = len(data)
    if stepping & STEPPING_COMMENT:
        if offset + COMMENT_HEADER_SIZE > size:
            warnings.append("TD0 file is truncated inside the comment block header")
            return None
        stored = data[0] | (data[1] << 8)
        length = data[2] | (data[3] << 8)
        if offset + COMMENT_HEADER_SIZE + length > size:
            warnings.append("TD0 file is truncated inside the comment text")
            return None
        computed = crc16(data[2:COMMENT_HEADER_SIZE + length])
        if computed != stored:
            warnings.append(f"TD0 comment block CRC mismatch (stored {stored:04X}, computed {computed:04X})")
        offset += COMMENT_HEADER_SIZE + length

    tracks = []
    while True:
        if offset >= size:
            warnings.append("TD0 image has no end-of-image marker (0xFF track header)")
            break
        if data[offset] == END_OF_IMAGE:
            break
        if offset + TRACK_HEADER_SIZE > size:
            warnings.append("TD0 file is truncated inside a track header")
            return None
        count = data[offset]
        cylinder = data[offset + 1]
        head = data[offset + 2] & 0x7F
        fm = header_fm or bool(data[offset + 2] & HEAD_FM)
        stored = data[offset + 3]
        computed = crc16(data[offset:offset + 3]) & 0xFF
        if stored != computed:
            warnings.append(f"TD0 cylinder {cylinder} head {head}: track header CRC mismatch (stored {stored:02X}, "
                            f"computed {computed:02X})")
        offset += TRACK_HEADER_SIZE
        sectors = []
        for _ in range(count):
            if offset + SECTOR_HEADER_SIZE > size:
                warnings.append(f"TD0 file is truncated inside the sector list of cylinder {cylinder} head {head}")
                return None
            c, h, r, n, flags, sector_crc = data[offset:offset + SECTOR_HEADER_SIZE]
            offset += SECTOR_HEADER_SIZE
            sector_data = b""
            if not flags & FLAGS_NO_DATA_BLOCK:
                if offset + 2 > size:
                    warnings.append(f"TD0 file is truncated at the data block of cylinder {cylinder} head {head} "
                                    f"sector {r}")
                    return None
                block_length = data[offset] | (data[offset + 1] << 8)
                offset += 2
                if block_length == 0 or offset + block_length > size:
                    warnings.append(f"TD0 file is truncated inside the data block of cylinder {cylinder} head {head} "
                                    f"sector {r}")
                    return None
                if n > 6:
                    warnings.append(f"TD0 cylinder {cylinder} head {head} sector {r}: invalid size code {n}, "
                                    f"sector skipped")
                    offset += block_length
                    continue
                encoding = data[offset]
                sector_data, ok = decode_data_block(encoding, data[offset + 1:offset + block_length], 128 << n)
                if not ok:
                    warnings.append(f"TD0 cylinder {cylinder} head {head} sector {r}: malformed data block "
                                    f"(encoding {encoding})")
                offset += block_length
                computed = crc16(sector_data) & 0xFF
                if computed != sector_crc:
                    warnings.append(f"TD0 cylinder {cylinder} head {head} sector {r}: data CRC mismatch (stored "
                                    f"{sector_crc:02X}, computed {computed:02X})")
            if flags & FLAG_NO_ID:
                warnings.append(f"TD0 cylinder {cylinder} head {head}: sector {r} has data but no ID field, skipped")
                continue
            sectors.append((c, h, r, n, flags, sector_data))
        tracks.append((cylinder, head, fm, sectors))
    return tracks


def _build_image(tracks, sides: int, length_mfm: int, length_fm: int, warnings: list[str]) -> DiskImage:
    """LoaderTD0::buildImage"""

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    cylinders = 1
    for cylinder, head, _fm, _sectors in tracks:
        if head > 1:
            raise fail(f"TD0 cylinder {cylinder}: head {head} is outside the supported range 0..1")
        if cylinder >= MAX_CYLINDERS:
            raise fail(f"TD0 cylinder {cylinder} exceeds the supported maximum of {MAX_CYLINDERS} cylinders")
        cylinders = max(cylinders, cylinder + 1)
        if head == 1 and sides < 2:
            warnings.append("TD0 header declares a single-sided disk but head 1 tracks are present; loaded as "
                            "double-sided")
            sides = 2

    disk = DiskImage(cylinders, sides)  # every track erased: tracks absent from the file stay unformatted
    for cylinder, head, fm, sectors in tracks:
        track = disk.get_track(cylinder, head)
        if track is None:
            continue
        if not sectors:
            if fm:
                track.resize_raw(DEFAULT_TRACK_SIZE_FM, FM, 0xFF)
            else:
                track.resize_raw(DEFAULT_TRACK_SIZE_MFM, MFM, 0x4E)
            continue
        for c, h, r, n, flags, data in sectors:
            if n > 3:
                warnings.append(f"TD0 cylinder {cylinder} head {head} sector {r}: size code {n} masked to {n & 3} "
                                f"(WD1793 uses 2 bits)")
        spec = TrackFormatSpec.ibm3740() if fm else TrackFormatSpec.trdos(None, 0)
        spec.track_length = length_fm if fm else length_mfm
        spec.sector_numbers = [s[2] for s in sectors]
        spec.sector_size_codes = [s[3] & 3 for s in sectors]
        spec.sector_cylinders = [s[0] for s in sectors]
        spec.sector_heads = [s[1] for s in sectors]
        spec.id_only = [1 if s[4] & FLAGS_NO_DATA_BLOCK else 0 for s in sectors]
        ok, note = fit_track_spec(spec, MIN_GAP_POST_DATA_FM if fm else MIN_GAP_POST_DATA_MFM)
        if not ok:
            raise fail(f"TD0 cylinder {cylinder} head {head}: {note}")
        if note:
            warnings.append(f"TD0 cylinder {cylinder} head {head}: {note}")
        track.format_track(cylinder, head, spec)
        views = track.index
        for s, (_c, _h, _r, _n, flags, data) in enumerate(sectors):
            if s >= len(views) or not views[s].has_data:
                continue
            sector = views[s]
            sector.write_data(data)
            if flags & FLAG_DELETED:
                sector.set_data_address_mark(DAM_DELETED)
            sector.recalculate_data_crc()
            if flags & FLAG_DATA_CRC_ERROR:
                sector.set_data_crc(sector.data_crc() ^ 0xFFFF)
        track.reindex()
    return disk


def parse(data: bytes) -> DiskImage:
    """LoaderTD0::parse"""
    warnings: list[str] = []

    def fail(message: str) -> DiskConversionError:
        warnings.append(message)
        return DiskConversionError(message, warnings)

    if not detect(data):
        raise fail("Not a TD0 file: signature 'TD' / 'td' missing")
    if len(data) < HEADER_SIZE:
        raise fail("TD0 file is truncated (shorter than the header)")
    stored = data[10] | (data[11] << 8)
    computed = crc16(data[:10])
    if stored != computed:
        raise fail(f"TD0 header CRC mismatch (stored {stored:04X}, computed {computed:04X})")
    advanced = data[0:1] == b"t"
    data_rate, stepping, dos_allocation, sides_byte = data[5], data[7], data[8], data[9]
    sides = 1 if sides_byte == 1 else 2
    header_fm = bool(data_rate & RATE_FM)
    length_mfm, length_fm = DEFAULT_TRACK_SIZE_MFM, DEFAULT_TRACK_SIZE_FM
    rate = data_rate & RATE_MASK
    if rate == 1:
        warnings.append("TD0 data rate 300 kbps is outside the WD1793 / Beta Disk envelope; track length scaled to "
                        "7500 bytes")
        length_mfm, length_fm = length_mfm * 300 // 250, length_fm * 300 // 250
    elif rate == 2:
        warnings.append("TD0 data rate 500 kbps is outside the WD1793 / Beta Disk envelope; track length scaled to "
                        "12500 bytes")
        length_mfm, length_fm = length_mfm * 2, length_fm * 2
    elif rate == 3:
        warnings.append("TD0 data rate code 3 is reserved; treated as 250 kbps")
    length_mfm, length_fm = min(length_mfm, MAX_TRACK_SIZE), min(length_fm, MAX_TRACK_SIZE)
    if dos_allocation:
        warnings.append("TD0 image was saved with DOS allocation: unallocated sectors carry no data (stored as "
                        "ID-only)")
    body = data[HEADER_SIZE:]
    if advanced:
        body = _decompress(body)
    tracks = _parse_body(body, header_fm, stepping, warnings)
    if tracks is None:
        raise DiskConversionError(warnings[-1], warnings)
    disk = _build_image(tracks, sides, length_mfm, length_fm, warnings)
    disk.format = "td0"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        message = "File is empty: " + path
        raise DiskConversionError(message, [message])
    return parse(data)


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderTD0::serialize with a fresh loader's header fields"""
    warnings: list[str] = []
    cylinders, sides = disk.cylinders & 0xFF, disk.heads & 0xFF
    all_fm = True
    for cylinder in range(cylinders):
        for head in range(sides):
            track = disk.get_track(cylinder, head)
            if track is None:
                message = f"TD0 save refused: track cylinder {cylinder} head {head} is missing"
                warnings.append(message)
                raise DiskConversionError(message, warnings)
            if track.encoding != FM:
                all_fm = False
    out = bytearray(b"TD")
    out += bytes((0, 0, DEFAULT_VERSION, RATE_FM if all_fm else 0, DEFAULT_DRIVE_TYPE, 0, 0, 1 if sides == 1 else 2))
    crc = crc16(out)
    out += bytes((crc & 0xFF, crc >> 8))
    lossy = False
    for cylinder in range(cylinders):
        for head in range(sides):
            track = disk.get_track(cylinder, head)
            fm = track.encoding == FM
            total = track.sector_count()
            count = min(total, 254)
            if total > 254:
                warnings.append(f"TD0 cylinder {cylinder} head {head}: only the first 254 of {total} sectors stored")
            if track.length != (DEFAULT_TRACK_SIZE_FM if fm else DEFAULT_TRACK_SIZE_MFM):
                lossy = True
            at = len(out)
            out += bytes((count, cylinder, head | (HEAD_FM if fm else 0)))
            out.append(crc16(out[at:at + 3]) & 0xFF)
            views = track.index[:count]
            for s, sector in enumerate(views):
                flags = 0
                if not sector.has_data:
                    flags |= FLAG_NO_DATA
                else:
                    if not sector.data_crc_valid:
                        flags |= FLAG_DATA_CRC_ERROR
                    if sector.deleted:
                        flags |= FLAG_DELETED
                number = sector.number
                if any(views[p].number == number for p in range(s)):
                    flags |= FLAG_DUPLICATE
                if not sector.id_crc_valid:
                    lossy = True
                out += bytes((sector.cylinder, sector.head, number, sector.size_code, flags))
                if not sector.has_data:
                    out.append(0)
                    continue
                d = sector.data_bytes()
                out.append(crc16(d) & 0xFF)
                size = len(d)
                if size >= 2 and size % 2 == 0 and d == d[:2] * (size // 2):
                    out += bytes((5, 0, ENCODING_PATTERN, (size // 2) & 0xFF, (size // 2) >> 8, d[0], d[1]))
                else:
                    out += bytes(((size + 1) & 0xFF, ((size + 1) >> 8) & 0xFF, ENCODING_RAW))
                    out += d
    out.append(END_OF_IMAGE)
    if lossy:
        warnings.append("TD0 stores sector lists only: gaps, clock marks, exact CRC values and track length are not "
                        "preserved (use UDI for a lossless copy)")
    return bytes(out), warnings


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
