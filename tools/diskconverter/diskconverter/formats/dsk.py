"""DSK / Extended DSK - Amstrad CPC, ZX Spectrum +3 (port of unreal-disk src/loaders/loader_dsk.cpp).

Reading takes both the standard "MV - CPC" and the "EXTENDED CPC DSK" layouts. Each track's sector list (C H R N
and the uPD765 status bytes ST1 / ST2) becomes a TrackFormatSpec (IBM layout, the track's GAP#3 and filler; FM when
the extended recording mode says so); the track is formatted from it and the data and status bits are applied on
top, as the C++ loader does:

- ST1 0x20 without ST2 0x20: ID CRC broken (first stored byte inverted); ST2 0x20: data CRC broken; ST2 0x40:
  deleted data mark; ST1 0x04 / ST1 0x01 / ST2 0x01 or an extended length 0: ID field only.
- An extended length k * size (k > 1): k copies of a weak sector, differing bytes marked weak.
- N >= 4: the data field holds the stored bytes (a long field without a CRC where the model looks for one); a layout
  longer than 12500 bytes is laid out from overlapping dumps (CPDRead) when they show it, else the longest fields
  are cut. CPDRead images keep a 128-byte sector in a 256-byte slot.

Writing always produces an Extended DSK (creator "unreal-ng"), GAP#3 measured after the first sector, the filler the
most frequent data byte, a weak sector stored as two copies (the second with the weak bytes inverted).
"""

from __future__ import annotations

from ..disk_image import (DEFAULT_TRACK_SIZE_FM, DEFAULT_TRACK_SIZE_MFM, FM, MAX_CYLINDERS, MAX_SIDES, MAX_TRACK_SIZE,
                          MFM, DiskConversionError, DiskImage, TrackFormatSpec, crc_wd1793)

SIGNATURE_STANDARD = b"MV - CPC"
SIGNATURE_EXTENDED = b"EXTENDED"
SIGNATURE_EXTENDED_FULL = b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"
TRACK_SIGNATURE = b"Track-Info"
CREATOR = b"unreal-ng     "
CPDREAD_CREATOR = b"CPDRead"
CPDREAD_SLOTTED_SIZE = 128
CPDREAD_SLOT_SIZE = 256
DISK_INFO_SIZE = 256
TRACK_INFO_SIZE = 256
SECTOR_INFO_SIZE = 8
MAX_SECTORS_PER_TRACK = (TRACK_INFO_SIZE - 0x18) // SECTOR_INFO_SIZE  # 29
MAX_TRACKS = DISK_INFO_SIZE - 0x34  # 204

OFF_CREATOR = 0x22
OFF_TRACKS = 0x30
OFF_SIDES = 0x31
OFF_TRACK_SIZE = 0x32
OFF_TRACK_SIZE_TABLE = 0x34

OFF_TI_CYLINDER = 0x10
OFF_TI_SIDE = 0x11
OFF_TI_RATE = 0x12
OFF_TI_MODE = 0x13
OFF_TI_SIZE_CODE = 0x14
OFF_TI_SECTOR_COUNT = 0x15
OFF_TI_GAP3 = 0x16
OFF_TI_FILLER = 0x17
OFF_TI_SECTOR_INFO = 0x18

MODE_FM = 1
MODE_MFM = 2
RATE_SD_DD = 1

ST1_MISSING_ADDRESS_MARK = 0x01
ST1_NO_DATA = 0x04
ST1_DATA_ERROR = 0x20
ST2_MISSING_DATA_MARK = 0x01
ST2_DATA_ERROR_IN_DATA = 0x20
ST2_CONTROL_MARK = 0x40

NOMINAL_TRACK_LEN = DEFAULT_TRACK_SIZE_MFM
NOMINAL_TRACK_LEN_FM = DEFAULT_TRACK_SIZE_FM


def detect(data: bytes) -> bool:
    return len(data) >= 8 and data[:8] in (SIGNATURE_STANDARD, SIGNATURE_EXTENDED)


def _stored_size_standard(n: int) -> int:
    """128 << N as a standard DSK stores it; N >= 6 is stored as 0x1800 bytes (CPCEMU)"""
    return 0x1800 if n >= 6 else 128 << n


def _model_size(n: int) -> int:
    return 128 << (n & 3)


def _real_size(n: int) -> int:
    return 128 << min(n, 7)


def _long_field_length(n: int, stored: int) -> int:
    if n <= 3 or stored == 0:
        return 0
    real = _real_size(n)
    return real if stored >= real else stored


class _Entry:
    __slots__ = ("st1", "st2", "stored", "src")

    def __init__(self) -> None:
        self.st1 = 0
        self.st2 = 0
        self.stored = 0
        self.src = 0  # offset into the file


def _flip_id_crc(sector) -> None:
    """sector.id->id_crc ^= 0x00FF: the first stored ID CRC byte inverted"""
    sector.track.raw[sector.idam + 5] ^= 0xFF
    sector.is_id_crc_valid()


def _lay_out_overlapping_fields(track, cylinder: int, side: int, spec: TrackFormatSpec, entries: list[_Entry],
                                data: bytes, warnings: list[str]) -> bool:
    """layOutOverlappingFields: a protection track dumped with a growing N, laid out as the dumps show it"""
    count = len(entries)
    if spec.encoding != MFM or count < 2:
        return False
    spec = TrackFormatSpec(**{k: (list(v) if isinstance(v, list) else v) for k, v in vars(spec).items()})
    marks = 3
    id_field = 7

    to_next_id = 0
    witness = count
    for i in range(count - 1):
        se = entries[i]
        pattern = bytes((0xA1, 0xA1, 0xA1, 0xFE, spec.sector_cylinders[i + 1] & 0xFF, spec.sector_heads[i + 1] & 0xFF,
                         spec.sector_numbers[i + 1] & 0xFF, spec.sector_size_codes[i + 1] & 0xFF))
        at = data.find(pattern, se.src, se.src + se.stored)
        if at < 0:
            continue
        distance = at - se.src + marks
        if to_next_id != 0 and distance != to_next_id:
            return False
        to_next_id = distance
        witness = i
    if witness == count or to_next_id < marks + spec.sync_length + 3:
        return False

    w = entries[witness]
    wsrc = w.src
    dam_at = to_next_id + id_field + spec.gap_post_id + spec.sync_length + marks
    if dam_at >= w.stored or data[wsrc + dam_at] not in (0xFB, 0xF8):
        return False
    sync_start = to_next_id - marks - spec.sync_length
    for b in range(sync_start, to_next_id - marks):
        if data[wsrc + b] != 0x00:
            return False
    gap_start = sync_start
    while gap_start > 0 and data[wsrc + gap_start - 1] == spec.gap_fill:
        gap_start -= 1
    if gap_start < 3:
        return False
    field_len = gap_start - 2
    gap3 = sync_start - gap_start
    if gap3 == 0 or gap3 > 255:
        return False
    pitch = dam_at + 1

    source: list[int | None] = [None] * count
    available = [0] * count
    for j in range(count):
        if entries[j].stored != 0:
            source[j] = entries[j].src
            available[j] = entries[j].stored
            continue
        for i in range(j - 1, -1, -1):
            se = entries[i]
            offset = (j - i) * pitch
            if offset + field_len > se.stored or data[se.src + offset - 1] not in (0xFB, 0xF8):
                continue
            source[j] = se.src + offset
            available[j] = se.stored - offset
            break
        missing = (entries[j].st1 & (ST1_NO_DATA | ST1_MISSING_ADDRESS_MARK)) != 0 or \
            (entries[j].st2 & ST2_MISSING_DATA_MARK) != 0
        if source[j] is not None and not missing:
            spec.id_only[j] = 0

    spec.gap_post_data = gap3
    spec.sector_data_lengths = [field_len] * count
    if spec.total_bytes() > NOMINAL_TRACK_LEN:
        spec.index_mark = False
    spec.track_length = max(NOMINAL_TRACK_LEN, spec.total_bytes())
    if spec.track_length > MAX_TRACK_SIZE:
        return False

    track.format_track(cylinder, side, spec)
    sectors = list(track.index)
    if len(sectors) != count:
        return False

    raw = track.raw
    for j in range(count):
        sector = sectors[j]
        se = entries[j]
        if not spec.is_id_only(j):
            start = sector.idam + id_field + spec.gap_post_id + spec.sync_length + marks + 1
            if start + field_len + 2 > len(raw):
                return False
            if se.st2 & ST2_CONTROL_MARK:
                raw[start - 1] = 0xF8
            copied = min(available[j], field_len + 2)
            if copied != 0:
                raw[start:start + copied] = data[source[j]:source[j] + copied]
            if copied < field_len + 2:
                crc = crc_wd1793(raw[start - 1:start + field_len])
                raw[start + field_len] = crc & 0xFF
                raw[start + field_len + 1] = crc >> 8
        if (se.st1 & ST1_DATA_ERROR) and not (se.st2 & ST2_DATA_ERROR_IN_DATA):
            _flip_id_crc(sector)

    track.touch()
    track.reindex()
    warnings.append(f"DSK track cylinder {cylinder} side {side}: overlapping long sector fields laid out as dumped, "
                    f"{count} sectors {pitch} bytes apart, {field_len}-byte fields, GAP#3 {gap3}")
    return True


def _parse_track(disk: DiskImage, cylinder: int, side: int, data: bytes, info: int, data_end: int, extended: bool,
                 creator: bytes, warnings: list[str]) -> bool:
    """LoaderDSK::parseTrack; info and data_end are offsets into data"""
    if data[info:info + len(TRACK_SIGNATURE)] != TRACK_SIGNATURE:
        warnings.append(f"DSK track cylinder {cylinder} side {side}: 'Track-Info' signature missing")
        return False

    mode = data[info + OFF_TI_MODE] if extended else MODE_MFM
    sector_count = data[info + OFF_TI_SECTOR_COUNT]
    gap3 = data[info + OFF_TI_GAP3]
    filler = data[info + OFF_TI_FILLER]

    if sector_count > MAX_SECTORS_PER_TRACK:
        warnings.append(f"DSK track cylinder {cylinder} side {side} declares {sector_count} sectors, at most "
                        f"{MAX_SECTORS_PER_TRACK} fit into the track information block")
        return False

    track = disk.get_track(cylinder, side)
    if track is None:
        warnings.append(f"DSK track cylinder {cylinder} side {side} is outside the image")
        return False

    spec = TrackFormatSpec.ibm(0, 2, 1, gap3, filler)
    if mode == MODE_FM:
        spec.encoding = FM
        spec.track_length = NOMINAL_TRACK_LEN_FM
        spec.gap_fill = 0xFF
        spec.sync_length = 6
        spec.gap_index = 40
        spec.gap_post_index = 26
        spec.gap_post_id = 11

    entries = [_Entry() for _ in range(sector_count)]
    spec.sector_numbers = [0] * sector_count
    spec.sector_size_codes = [0] * sector_count
    spec.sector_cylinders = [0] * sector_count
    spec.sector_heads = [0] * sector_count
    spec.id_only = [0] * sector_count
    spec.sector_data_lengths = [0] * sector_count

    cpdread_slots = extended and creator.startswith(CPDREAD_CREATOR)

    src = info + TRACK_INFO_SIZE
    for i in range(sector_count):
        entry = info + OFF_TI_SECTOR_INFO + i * SECTOR_INFO_SIZE
        n = data[entry + 3]
        spec.sector_cylinders[i] = data[entry]
        spec.sector_heads[i] = data[entry + 1]
        spec.sector_numbers[i] = data[entry + 2]
        spec.sector_size_codes[i] = n

        se = entries[i]
        se.st1 = data[entry + 4]
        se.st2 = data[entry + 5]
        se.stored = (data[entry + 6] | (data[entry + 7] << 8)) if extended else _stored_size_standard(n)
        se.src = src

        no_data_mark = (se.st1 & (ST1_NO_DATA | ST1_MISSING_ADDRESS_MARK)) != 0 or \
            (se.st2 & ST2_MISSING_DATA_MARK) != 0
        if src + se.stored > data_end:
            if cpdread_slots and no_data_mark and src >= data_end:
                se.stored = 0
            else:
                warnings.append(f"DSK track cylinder {cylinder} side {side}: sector data truncated (sector "
                                f"{data[entry + 2]}, {se.stored} bytes expected)")
                return False
        src += CPDREAD_SLOT_SIZE if (cpdread_slots and se.stored == CPDREAD_SLOTTED_SIZE) else se.stored

        id_only = no_data_mark or (extended and se.stored == 0)
        spec.id_only[i] = 1 if id_only else 0
        if not id_only:
            field_len = _long_field_length(n, se.stored)
            if field_len > _model_size(n):
                spec.sector_data_lengths[i] = field_len

    if not spec.fits():
        saved = spec.index_mark
        spec.index_mark = False
        if not spec.fits():
            spec.index_mark = saved

    cut_field = [0] * sector_count
    if not spec.fits():
        needed = spec.total_bytes()
        if needed <= MAX_TRACK_SIZE:
            spec.track_length = needed
            nominal = NOMINAL_TRACK_LEN_FM if spec.encoding == FM else NOMINAL_TRACK_LEN
            warnings.append(f"DSK track cylinder {cylinder} side {side}: {sector_count} sectors with GAP#3 {gap3} "
                            f"need {needed} bytes, track grown from {nominal}")
        else:
            spec.track_length = MAX_TRACK_SIZE
            gap_post_data = spec.gap_post_data
            while not spec.fits() and spec.gap_post_data > 1:
                spec.gap_post_data -= 1
            if not spec.fits():
                spec.gap_post_data = gap_post_data
                if _lay_out_overlapping_fields(track, cylinder, side, spec, entries, data, warnings):
                    return True

                cut = 0
                while not spec.fits():
                    longest = sector_count
                    for i in range(sector_count):
                        if spec.sector_data_lengths[i] > _model_size(spec.sector_size_codes[i]) and (
                                longest == sector_count
                                or spec.sector_data_lengths[i] > spec.sector_data_lengths[longest]):
                            longest = i
                    if longest == sector_count:
                        break
                    over = spec.total_bytes() - spec.track_length
                    room = spec.sector_data_lengths[longest] - _model_size(spec.sector_size_codes[longest])
                    by = min(over, room)
                    spec.sector_data_lengths[longest] -= by
                    cut_field[longest] = 1
                    cut += by
                while not spec.fits() and spec.gap_post_data > 1:
                    spec.gap_post_data -= 1
                if not spec.fits():
                    warnings.append(f"DSK track cylinder {cylinder} side {side}: {sector_count} sectors do not fit "
                                    f"into {MAX_TRACK_SIZE} bytes")
                    return False
                warnings.append(f"DSK track cylinder {cylinder} side {side}: overlapping long sector fields cut by "
                                f"{cut} bytes to fit {MAX_TRACK_SIZE} bytes, GAP#3 {spec.gap_post_data}")
            else:
                warnings.append(f"DSK track cylinder {cylinder} side {side}: layout too large, track grown to "
                                f"{MAX_TRACK_SIZE} bytes and GAP#3 reduced to {spec.gap_post_data}")

    track.format_track(cylinder, side, spec)

    sectors = list(track.index)
    if len(sectors) != sector_count:
        warnings.append(f"DSK track cylinder {cylinder} side {side}: {len(sectors)} of {sector_count} sectors indexed "
                        f"after formatting")
        return False

    raw = track.raw
    for i in range(sector_count):
        sector = sectors[i]
        se = entries[i]
        if sector.has_data:
            long_field = spec.sector_data_lengths[i] != 0
            size = spec.sector_data_lengths[i] if long_field else sector.size
            copies = se.stored // size if (not cut_field[i] and se.stored > size and se.stored % size == 0) else 1

            n = min(se.stored, size, len(raw) - sector.data)
            raw[sector.data:sector.data + n] = data[se.src:se.src + n]

            for k in range(1, copies):
                copy = se.src + k * size
                base = se.src
                for b in range(size):
                    if data[copy + b] != data[base + b]:
                        track.set_weak_byte(sector.data + b, True)
            if copies == 1 and se.stored > size and not cut_field[i]:
                warnings.append(f"DSK track cylinder {cylinder} side {side}: sector {sector.number} stores "
                                f"{se.stored} bytes for a {size}-byte sector, extra bytes dropped")

            if se.st2 & ST2_CONTROL_MARK:
                sector.set_data_address_mark(0xF8)

            if not long_field:
                sector.recalculate_data_crc()
                if se.st2 & ST2_DATA_ERROR_IN_DATA:
                    raw[sector.data + sector.size] ^= 0xFF
                    sector.is_data_crc_valid()

        if (se.st1 & ST1_DATA_ERROR) and not (se.st2 & ST2_DATA_ERROR_IN_DATA):
            _flip_id_crc(sector)

    track.touch()
    track.reindex()
    return True


def parse(data: bytes) -> DiskImage:
    """LoaderDSK::parse. Raises DiskConversionError (the loader's warnings attached) where C++ refuses."""
    warnings: list[str] = []

    def fail(message: str):
        warnings.append(message)
        raise DiskConversionError(message, warnings)

    if not detect(data):
        fail("Not a DSK file: 'MV - CPC' / 'EXTENDED' signature missing")
    extended = data[:8] == SIGNATURE_EXTENDED
    if len(data) < DISK_INFO_SIZE:
        fail("DSK file is truncated (shorter than the disk information block)")

    tracks = data[OFF_TRACKS]
    sides = data[OFF_SIDES]
    standard_track_size = data[OFF_TRACK_SIZE] | (data[OFF_TRACK_SIZE + 1] << 8)
    if tracks == 0:
        fail("DSK declares 0 tracks")
    if tracks > MAX_CYLINDERS:
        fail(f"DSK declares {tracks} tracks, the emulator supports at most {MAX_CYLINDERS}")
    if sides == 0 or sides > MAX_SIDES:
        fail(f"DSK declares {sides} sides, only 1 or 2 are supported")
    track_count = tracks * sides
    if extended and track_count > MAX_TRACKS:
        fail(f"EDSK declares {track_count} tracks, the size table holds at most {MAX_TRACKS}")
    if not extended and standard_track_size < TRACK_INFO_SIZE:
        fail(f"DSK track size {standard_track_size} is smaller than the track information block")

    creator = data[OFF_CREATOR:OFF_CREATOR + 14].rstrip(b" \x00")

    disk = DiskImage(tracks, sides)
    offset = DISK_INFO_SIZE
    length = len(data)
    for cylinder in range(tracks):
        for side in range(sides):
            index = cylinder * sides + side
            track_size = data[OFF_TRACK_SIZE_TABLE + index] * 256 if extended else standard_track_size
            if track_size == 0:
                disk.get_track(cylinder, side).resize_raw(DEFAULT_TRACK_SIZE_MFM)
                continue
            if offset + TRACK_INFO_SIZE > length:
                fail(f"DSK truncated at track cylinder {cylinder} side {side}")
            data_end = min(length, offset + track_size)
            if not _parse_track(disk, cylinder, side, data, offset, data_end, extended, creator, warnings):
                raise DiskConversionError(warnings[-1], warnings)
            offset += track_size

    disk.format = "dsk"
    disk.warnings = warnings
    return disk


def read(path: str) -> DiskImage:
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        message = "File is empty: " + path
        raise DiskConversionError(message, [message])
    return parse(data)


def _measure_gap3(track) -> int:
    sectors = track.index
    if not sectors:
        return 0x4E
    first = sectors[0]
    raw = track.raw
    pos = first.data + first.size + 2 if first.has_data else first.idam + 7
    limit = len(raw)
    if len(sectors) > 1:
        limit = min(limit, sectors[1].idam - (3 if track.encoding == MFM else 0))
    count = 0
    while pos + count < limit and raw[pos + count] != 0x00 and count < 255:
        count += 1
    return count


def _most_frequent_data_byte(track) -> int:
    histogram = [0] * 256
    any_data = False
    raw = track.raw
    for sector in track.index:
        if not sector.has_data:
            continue
        any_data = True
        for value in raw[sector.data:sector.data + sector.size]:
            histogram[value] += 1
    if not any_data:
        return 0xE5
    best = 0
    for b in range(1, 256):
        if histogram[b] > histogram[best]:
            best = b
    return best


def serialize(disk: DiskImage) -> tuple[bytes, list[str]]:
    """LoaderDSK::serialize: always an Extended DSK"""
    warnings: list[str] = []

    def fail(message: str):
        warnings.append(message)
        raise DiskConversionError(message, warnings)

    cylinders = disk.cylinders & 0xFF
    sides = disk.heads & 0xFF
    if cylinders == 0 or sides == 0 or sides > 2:
        fail("DSK save refused: invalid disk geometry")
    track_count = cylinders * sides
    if track_count > MAX_TRACKS:
        fail(f"DSK save refused: {track_count} tracks exceed the EDSK size table ({MAX_TRACKS})")

    out = bytearray(DISK_INFO_SIZE)
    out[0:len(SIGNATURE_EXTENDED_FULL)] = SIGNATURE_EXTENDED_FULL
    out[OFF_CREATOR:OFF_CREATOR + 14] = CREATOR
    out[OFF_TRACKS] = cylinders
    out[OFF_SIDES] = sides

    for cylinder in range(cylinders):
        for side in range(sides):
            index = cylinder * sides + side
            track = disk.get_track(cylinder, side)
            if track is None:
                fail(f"DSK save refused: track cylinder {cylinder} side {side} is missing")
            sectors = track.index
            if not sectors:
                out[OFF_TRACK_SIZE_TABLE + index] = 0
                continue
            if len(sectors) > MAX_SECTORS_PER_TRACK:
                fail(f"DSK save refused: track cylinder {cylinder} side {side} has {len(sectors)} sectors, EDSK holds "
                     f"at most {MAX_SECTORS_PER_TRACK}")

            raw = track.raw
            raw_size = len(raw)
            block_start = len(out)
            info = bytearray(TRACK_INFO_SIZE)
            info[0:12] = b"Track-Info\r\n"
            info[OFF_TI_CYLINDER] = cylinder
            info[OFF_TI_SIDE] = side
            info[OFF_TI_RATE] = RATE_SD_DD
            info[OFF_TI_MODE] = MODE_FM if track.encoding == FM else MODE_MFM
            info[OFF_TI_SIZE_CODE] = sectors[0].size_code
            info[OFF_TI_SECTOR_COUNT] = len(sectors)
            info[OFF_TI_GAP3] = _measure_gap3(track)
            info[OFF_TI_FILLER] = _most_frequent_data_byte(track)
            body = bytearray()

            for i, sector in enumerate(sectors):
                st1 = st2 = 0
                stored = 0
                weak = False
                n = sector.size_code
                long_length = _stored_size_standard(n) if (sector.has_data and n > 3) else 0
                if sector.has_data:
                    stored = long_length if long_length else sector.size
                    if not sector.data_crc_valid:
                        st1 |= ST1_DATA_ERROR
                        st2 |= ST2_DATA_ERROR_IN_DATA
                    if sector.deleted:
                        st2 |= ST2_CONTROL_MARK
                    if long_length == 0:
                        weak = any(track.weak_byte(sector.data + b) for b in range(sector.size))
                    if weak:
                        stored *= 2
                else:
                    st1 |= ST1_NO_DATA
                    st2 |= ST2_MISSING_DATA_MARK
                if not sector.id_crc_valid:
                    st1 |= ST1_DATA_ERROR

                e = OFF_TI_SECTOR_INFO + i * SECTOR_INFO_SIZE
                info[e:e + 8] = bytes((sector.cylinder, sector.head, sector.number, n, st1, st2, stored & 0xFF,
                                       (stored >> 8) & 0xFF))

                if sector.has_data and long_length:
                    start = sector.data
                    body += bytes(raw[(start + b) % raw_size] for b in range(long_length))
                elif sector.has_data:
                    field = raw[sector.data:sector.data + sector.size]
                    body += field
                    if weak:
                        body += bytes((~v & 0xFF) if track.weak_byte(sector.data + b) else v
                                      for b, v in enumerate(field))

            out += info
            out += body
            block_size = len(out) - block_start
            padded = (block_size + 255) & ~255
            if padded // 256 > 255:
                fail(f"DSK save refused: track cylinder {cylinder} side {side} is larger than 65280 bytes")
            out += bytes(padded - block_size)
            out[OFF_TRACK_SIZE_TABLE + index] = padded // 256

            if raw_size not in (DEFAULT_TRACK_SIZE_MFM, DEFAULT_TRACK_SIZE_FM):
                warnings.append(f"DSK cannot store the track length: track cylinder {cylinder} side {side} "
                                f"({raw_size} bytes) saved without it")

    return bytes(out), warnings


def write(disk: DiskImage, path: str) -> list[str]:
    data, warnings = serialize(disk)
    with open(path, "wb") as f:
        f.write(data)
    return warnings
