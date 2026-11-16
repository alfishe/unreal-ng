"""The floppy model shared by every reader and writer: a port of unreal-disk's DiskImage
(core/src/3rdparty/unreal-disk/include/unrealdisk/diskimage.h).

A track is what READ TRACK returns: a byte stream from the index pulse (`Track.raw`), one bit per byte telling
whether that byte was written with a missing-clock pattern (`Track.clock`: A1 / C2 in MFM, C7 / D7 clocks in FM),
an optional weak-byte bitmap (`Track.weak`) and the encoding ("mfm" / "fm"). Sectors are never stored: they are
derived from the stream by `Track.reindex()`, a line-by-line port of C++ `Track::reindex`, into `SectorIndex`
entries (views with stream offsets, like C++ `DiskImage::Sector`). `Track.format_track()` lays out a track from a
`TrackFormatSpec` exactly as C++ `Track::formatTrack` does, so a loader that formats and then fills sectors
produces the same bytes as its C++ counterpart.

The sector-level convenience API of the earlier model stays: `Track.sectors` is a list of `Sector` values (C, H,
R, raw N, data, flags, the stored CRC bytes, the stream offsets) and assigning a list of `Sector` to it lays the
track out from them (the FDI loader's layout for MFM, the TD0 loader's for FM). Mutating a `Sector` taken from
`Track.sectors` does not change the track: assign the list back.

CRC values follow the C++ representation: `crc_wd1793` returns the CRC byte-swapped, so the stored on-disk bytes
read as a little-endian uint16 equal it (`stored_u16`).
"""

from __future__ import annotations

import binascii
from dataclasses import dataclass, field

MFM = "mfm"
FM = "fm"

DEFAULT_TRACK_SIZE_MFM = 6250
DEFAULT_TRACK_SIZE_FM = 3125
MIN_TRACK_SIZE = 64
MAX_TRACK_SIZE = 12500
MAX_SECTORS_PER_TRACK = 255
DAM_SEARCH_WINDOW_MFM = 43
DAM_SEARCH_WINDOW_FM = 30
MAX_CYLINDERS = 86
MAX_SIDES = 2
NO_OFFSET = -1

IDAM = 0xFE
DAM_NORMAL = 0xFB
DAM_DELETED = 0xF8
INDEX_MARK = 0xFC


class DiskConversionError(Exception):
    """A load or a save that cannot be done at all. `warnings` holds what the loader / writer reported."""

    def __init__(self, message: str, warnings: list[str] | None = None):
        super().__init__(message)
        self.warnings = list(warnings or [])


# region <CRC>

def crc_wd1793(data) -> int:
    """CRCHelper::crcWD1793: CRC-16/CCITT preset 0xCDB4 (A1 A1 A1 already fed), byte-swapped"""
    crc = binascii.crc_hqx(data, 0xCDB4)
    return ((crc & 0xFF) << 8) | (crc >> 8)


def crc_wd1793_fm(data) -> int:
    """CRCHelper::crcWD1793FM: CRC-16/CCITT preset 0xFFFF right before the FM mark byte, byte-swapped"""
    crc = binascii.crc_hqx(data, 0xFFFF)
    return ((crc & 0xFF) << 8) | (crc >> 8)


def crc_for(encoding: str, data) -> int:
    return crc_wd1793(data) if encoding == MFM else crc_wd1793_fm(data)


def crc_bytes(value: int) -> bytes:
    """The on-disk byte order of a crc_wd1793 value (true high byte first)"""
    return bytes((value & 0xFF, (value >> 8) & 0xFF))


def stored_u16(raw, offset: int) -> int:
    """Two stream bytes as C++ reads a stored CRC (little-endian uint16 overlay)"""
    return raw[offset] | (raw[offset + 1] << 8)

# endregion </CRC>


def clamp_track_size(size: int) -> int:
    return min(max(size, MIN_TRACK_SIZE), MAX_TRACK_SIZE)


@dataclass
class TrackFormatSpec:
    """DiskImage::TrackFormatSpec: a data-driven track layout for Track.format_track"""

    encoding: str = MFM
    track_length: int = DEFAULT_TRACK_SIZE_MFM
    size_code: int = 1
    sector_numbers: list[int] = field(default_factory=list)
    sector_size_codes: list[int] = field(default_factory=list)
    sector_cylinders: list[int] = field(default_factory=list)
    sector_heads: list[int] = field(default_factory=list)
    id_only: list[int] = field(default_factory=list)
    sector_data_lengths: list[int] = field(default_factory=list)
    index_mark: bool = False
    gap_index: int = 80
    gap_post_index: int = 50
    gap_pre_id: int = 10
    sync_length: int = 12
    gap_post_id: int = 22
    gap_post_data: int = 60
    gap_fill: int = 0x4E
    data_fill: int = 0x00
    data_mark: int = 0xFB

    @classmethod
    def trdos(cls, order: list[int] | None = None, count: int = 16) -> "TrackFormatSpec":
        """The legacy TR-DOS layout: 16 x 388 bytes + 42 bytes end gap = 6250"""
        return cls(sector_numbers=[order[i] if order else i + 1 for i in range(count)])

    @classmethod
    def ibm(cls, sectors: int, size_code: int, first_sector: int = 1, gap3: int = 0x2A,
            filler: int = 0xE5) -> "TrackFormatSpec":
        return cls(size_code=size_code, index_mark=True, gap_pre_id=0, gap_post_id=22, gap_post_data=gap3,
                   data_fill=filler, sector_numbers=[(first_sector + i) & 0xFF for i in range(sectors)])

    @classmethod
    def ibm3740(cls) -> "TrackFormatSpec":
        spec = cls.ibm(16, 0, 1, 27, 0xE5)
        spec.encoding = FM
        spec.track_length = DEFAULT_TRACK_SIZE_FM
        spec.gap_fill = 0xFF
        spec.sync_length = 6
        spec.gap_index = 40
        spec.gap_post_index = 26
        spec.gap_post_id = 11
        return spec

    def sector_count(self) -> int:
        return len(self.sector_numbers)

    def size_code_for(self, i: int) -> int:
        return self.sector_size_codes[i] if i < len(self.sector_size_codes) else self.size_code

    def is_id_only(self, i: int) -> bool:
        return i < len(self.id_only) and self.id_only[i] != 0

    def data_length_for(self, i: int) -> int:
        if i < len(self.sector_data_lengths) and self.sector_data_lengths[i] != 0:
            return self.sector_data_lengths[i]
        return 128 << (self.size_code_for(i) & 3)

    def bytes_per_sector(self, i: int) -> int:
        marks = 3 if self.encoding == MFM else 0
        total = self.gap_pre_id + self.sync_length + marks + 7
        if not self.is_id_only(i):
            total += self.gap_post_id + self.sync_length + marks + 1 + self.data_length_for(i) + 2
        return total + self.gap_post_data

    def index_mark_bytes(self) -> int:
        if not self.index_mark:
            return 0
        marks = 3 if self.encoding == MFM else 0
        return self.gap_index + self.sync_length + marks + 1 + self.gap_post_index

    def total_bytes(self) -> int:
        return self.index_mark_bytes() + sum(self.bytes_per_sector(i) for i in range(len(self.sector_numbers)))

    def fits(self) -> bool:
        return self.total_bytes() <= self.track_length


def fit_track_spec(spec: TrackFormatSpec, min_gap: int) -> tuple[bool, str]:
    """The FDI / TD0 loaders' buildTrackSpec tail: shrink gap 3 down to min_gap, then grow the track up to
    MAX_TRACK_SIZE. Returns (fits, note); note is the loader's message ("" when the spec fitted as given)."""
    count = len(spec.sector_numbers)
    if spec.fits():
        return True, ""
    gap = spec.gap_post_data
    while gap >= min_gap:
        spec.gap_post_data = gap
        if spec.fits():
            return True, f"gap3 reduced to {gap} bytes to fit {count} sectors"
        gap -= 1
    spec.gap_post_data = min_gap
    needed = spec.total_bytes()
    if needed <= MAX_TRACK_SIZE:
        spec.track_length = needed
        return True, f"track length grown to {needed} bytes to fit {count} sectors"
    return False, f"{count} sectors ({needed} bytes) exceed the maximum track length {MAX_TRACK_SIZE}"


@dataclass
class Sector:
    """One sector as a value: what its ID field says (which may not match its physical track - protections
    rely on that), its data and flags. `size_code` is the raw N byte; the data length is 128 << (N & 3), as the
    WD1793 reads it. id_crc_stored / data_crc_stored are the two CRC bytes as they lie on the disk (taken from
    the stream, or given to reproduce a broken CRC when a track is built from sectors; without them a broken
    CRC is written as the computed one with both bytes inverted, as the C++ FDI / TD0 loaders do).
    The offsets locate the sector in the stream it was read from (-1: none)."""

    cylinder: int
    head: int
    number: int
    size_code: int
    data: bytes
    crc_valid: bool = True
    deleted: bool = False
    no_data: bool = False
    id_crc_valid: bool = True
    id_crc_stored: bytes | None = None
    data_crc_stored: bytes | None = None
    idam_offset: int = NO_OFFSET
    dam_offset: int = NO_OFFSET
    data_offset: int = NO_OFFSET

    @property
    def data_size(self) -> int:
        return 128 << (self.size_code & 3)

    def __post_init__(self) -> None:
        if not self.no_data and len(self.data) != self.data_size:
            raise DiskConversionError(
                f"sector C={self.cylinder} H={self.head} R={self.number}: data length {len(self.data)} does not "
                f"match size code {self.size_code} ({self.data_size} bytes)"
            )


class SectorIndex:
    """DiskImage::Sector: an index entry and view over one sector inside its track's stream"""

    __slots__ = ("track", "idam", "dam", "data", "size", "has_data", "deleted", "id_crc_valid", "data_crc_valid")

    def __init__(self, track: "Track", idam: int):
        self.track = track
        self.idam = idam
        self.dam = NO_OFFSET
        self.data = NO_OFFSET
        self.size = 0
        self.has_data = False
        self.deleted = False
        self.id_crc_valid = False
        self.data_crc_valid = False

    # region <ID field>
    @property
    def cylinder(self) -> int:
        return self.track.raw[self.idam + 1]

    @property
    def head(self) -> int:
        return self.track.raw[self.idam + 2]

    @property
    def number(self) -> int:
        return self.track.raw[self.idam + 3]

    @property
    def size_code(self) -> int:
        return self.track.raw[self.idam + 4]

    @property
    def id_crc(self) -> int:
        return stored_u16(self.track.raw, self.idam + 5)

    def compute_id_crc(self) -> int:
        return crc_for(self.track.encoding, self.track.raw[self.idam:self.idam + 5])

    def is_id_crc_valid(self) -> bool:
        self.id_crc_valid = self.compute_id_crc() == self.id_crc
        return self.id_crc_valid

    def set_id_crc(self, value: int) -> None:
        raw = self.track.raw
        raw[self.idam + 5] = value & 0xFF
        raw[self.idam + 6] = (value >> 8) & 0xFF
        self.is_id_crc_valid()
        self.track.touch()
    # endregion

    # region <Data field>
    def data_bytes(self) -> bytes:
        return bytes(self.track.raw[self.data:self.data + self.size]) if self.has_data else b""

    def data_address_mark(self) -> int:
        return self.track.raw[self.data - 1] if self.has_data else 0

    def set_data_address_mark(self, dam: int) -> None:
        if self.has_data:
            self.track.raw[self.data - 1] = dam
            self.deleted = dam == DAM_DELETED
            self.track.touch()

    def data_crc(self) -> int:
        return stored_u16(self.track.raw, self.data + self.size) if self.has_data else 0xFFFF

    def compute_data_crc(self) -> int:
        if not self.has_data:
            return 0xFFFF
        return crc_for(self.track.encoding, self.track.raw[self.data - 1:self.data + self.size])

    def is_data_crc_valid(self) -> bool:
        if not self.has_data:
            return False
        self.data_crc_valid = self.compute_data_crc() == self.data_crc()
        return self.data_crc_valid

    def set_data_crc(self, value: int) -> None:
        if self.has_data:
            raw = self.track.raw
            raw[self.data + self.size] = value & 0xFF
            raw[self.data + self.size + 1] = (value >> 8) & 0xFF
            self.is_data_crc_valid()
            self.track.touch()

    def recalculate_data_crc(self) -> None:
        if not self.has_data:
            return
        crc = self.compute_data_crc()
        raw = self.track.raw
        raw[self.data + self.size] = crc & 0xFF
        raw[self.data + self.size + 1] = crc >> 8
        self.data_crc_valid = True
        self.track.touch()

    def write_data(self, data, recalculate_crc: bool = False) -> None:
        """Copy data into the data field (at most `size` bytes), as the loaders' memcpy into sector->data"""
        if not self.has_data:
            return
        n = min(len(data), self.size)
        self.track.raw[self.data:self.data + n] = data[:n]
        self.track.touch()
        if recalculate_crc:
            self.recalculate_data_crc()
    # endregion

    def to_sector(self) -> Sector:
        raw = self.track.raw
        id_stored = bytes(raw[self.idam + 5:self.idam + 7])
        id_valid = self.compute_id_crc() == self.id_crc
        if self.has_data:
            data = bytes(raw[self.data:self.data + self.size])
            data_stored = bytes(raw[self.data + self.size:self.data + self.size + 2])
            data_valid = self.compute_data_crc() == self.data_crc()
        else:
            data, data_stored, data_valid = b"", None, False
        return Sector(cylinder=raw[self.idam + 1], head=raw[self.idam + 2], number=raw[self.idam + 3],
                      size_code=raw[self.idam + 4], data=data, crc_valid=data_valid if self.has_data else True,
                      deleted=self.deleted, no_data=not self.has_data, id_crc_valid=id_valid,
                      id_crc_stored=id_stored, data_crc_stored=data_stored, idam_offset=self.idam,
                      dam_offset=self.dam, data_offset=self.data)


class Track:
    """DiskImage::Track: raw stream + clock-mark bitmap + optional weak bitmap + derived sector index.

    A new Track is erased: 6250 gap bytes (0x4E), MFM, no marks (C++ eraseAll / resizeRaw)."""

    def __init__(self, cylinder: int = 0, side: int = 0):
        self.cylinder = cylinder
        self.side = side
        self.raw = bytearray()
        self.clock = bytearray()
        self.weak: bytearray | None = None
        self.encoding = MFM
        self.index: list[SectorIndex] = []
        self.index_mark_offset = NO_OFFSET
        self._version = 0
        self._snapshot: tuple[int, list[Sector]] | None = None
        self.resize_raw(DEFAULT_TRACK_SIZE_MFM)

    def touch(self) -> None:
        self._version += 1

    # region <Storage>
    @property
    def length(self) -> int:
        return len(self.raw)

    def _allocate_raw(self, size: int, encoding: str, fill: int) -> None:
        size = clamp_track_size(size)
        self.raw = bytearray((fill,)) * size
        self.clock = bytearray((size + 7) // 8)
        self.weak = None
        self.encoding = encoding
        self.touch()

    def resize_raw(self, size: int, encoding: str = MFM, fill: int = 0x4E) -> None:
        """Track::resizeRaw: a gap-filled stream, bitmaps cleared, index rebuilt"""
        self._allocate_raw(size, encoding, fill)
        self.reindex()

    def set_raw(self, data, encoding: str = MFM) -> None:
        """Track::setRaw: the stream replaced by a copy of data (clamped to 64..12500, padded with 0x4E), clock
        bitmap cleared"""
        self._allocate_raw(len(data), encoding, 0x4E)
        n = min(len(data), len(self.raw))
        self.raw[:n] = data[:n]
        self.reindex()

    def set_clock_bitmap(self, bitmap) -> None:
        """Track::setClockBitmap: bit i = byte i carries a missing-clock mark (bits past the stream are kept, as
        in C++: they only make the track count as one with clock information)"""
        self.clock = bytearray(len(self.clock))
        n = min(len(bitmap), len(self.clock))
        self.clock[:n] = bitmap[:n]
        self.touch()
        self.reindex()

    def set_stream(self, raw, clock=None, weak=None, encoding: str = MFM) -> None:
        """setRaw + setClockBitmap + the weak bytes, in one go (a lossless copy of another track's stream)"""
        self._allocate_raw(len(raw), encoding, 0x4E)
        n = min(len(raw), len(self.raw))
        self.raw[:n] = raw[:n]
        if clock is not None:
            n = min(len(clock), len(self.clock))
            self.clock[:n] = clock[:n]
        if weak is not None and any(weak):
            self.weak = bytearray(len(self.clock))
            n = min(len(weak), len(self.weak))
            self.weak[:n] = weak[:n]
        self.reindex()

    def has_clock_marks(self) -> bool:
        return any(self.clock)

    def clock_mark(self, offset: int) -> bool:
        return offset < len(self.raw) and bool(self.clock[offset >> 3] & (1 << (offset & 7)))

    def set_clock_mark(self, offset: int, on: bool) -> None:
        if offset >= len(self.raw):
            return
        if on:
            self.clock[offset >> 3] |= 1 << (offset & 7)
        else:
            self.clock[offset >> 3] &= ~(1 << (offset & 7)) & 0xFF
        self.touch()

    def has_weak_bits(self) -> bool:
        return self.weak is not None

    def weak_byte(self, offset: int) -> bool:
        return self.weak is not None and offset < len(self.raw) and bool(self.weak[offset >> 3] & (1 << (offset & 7)))

    def set_weak_byte(self, offset: int, on: bool) -> None:
        if offset >= len(self.raw):
            return
        if self.weak is None:
            if not on:
                return
            self.weak = bytearray(len(self.clock))
        if on:
            self.weak[offset >> 3] |= 1 << (offset & 7)
        else:
            self.weak[offset >> 3] &= ~(1 << (offset & 7)) & 0xFF
        self.touch()

    def weak_bitmap(self) -> bytes:
        """The weak bytes as zxdisk dump prints them: (length + 7) / 8 bytes, only bits of the stream"""
        if self.weak is None:
            return b""
        out = bytearray((len(self.raw) + 7) // 8)
        for i in range(len(self.raw)):
            if self.weak[i >> 3] & (1 << (i & 7)):
                out[i >> 3] |= 1 << (i & 7)
        return bytes(out)
    # endregion

    # region <Formatting>
    def format_track(self, cylinder: int, side: int, spec: TrackFormatSpec | None = None) -> None:
        """Track::formatTrack: data filled with spec.data_fill, every CRC valid, clock marks on every sync /
        mark byte, index rebuilt. Bytes past the track length are dropped, as C++ put() does."""
        if spec is None:
            spec = TrackFormatSpec.trdos()
        mfm = spec.encoding == MFM
        self._allocate_raw(spec.track_length, spec.encoding, spec.gap_fill)
        raw = self.raw
        clock = self.clock
        size = len(raw)
        pos = 0
        crc = crc_wd1793 if mfm else crc_wd1793_fm

        def put(chunk, marked: bool = False) -> None:
            nonlocal pos
            n = min(len(chunk), size - pos)
            if n <= 0:
                return
            raw[pos:pos + n] = chunk[:n]
            if marked:
                for i in range(pos, pos + n):
                    clock[i >> 3] |= 1 << (i & 7)
            pos += n

        def put_crc(start: int) -> None:
            value = crc(raw[start:pos])
            put(bytes((value & 0xFF, value >> 8)))

        def sync_and_mark(mark: int) -> None:
            put(bytes(spec.sync_length))
            if mfm:
                put(b"\xa1\xa1\xa1", True)
                put(bytes((mark,)))
            else:
                put(bytes((mark,)), True)

        gap = bytes((spec.gap_fill,))
        if spec.index_mark:
            put(gap * spec.gap_index)
            put(bytes(spec.sync_length))
            if mfm:
                put(b"\xc2\xc2\xc2", True)
                put(b"\xfc")
            else:
                put(b"\xfc", True)
            put(gap * spec.gap_post_index)

        for i, number in enumerate(spec.sector_numbers):
            c = spec.sector_cylinders[i] if i < len(spec.sector_cylinders) else cylinder
            h = spec.sector_heads[i] if i < len(spec.sector_heads) else side
            put(gap * spec.gap_pre_id)
            sync_and_mark(IDAM)
            id_start = pos - 1
            put(bytes((c & 0xFF, h & 0xFF, number & 0xFF, spec.size_code_for(i) & 0xFF)))
            put_crc(id_start)
            if not spec.is_id_only(i):
                put(gap * spec.gap_post_id)
                sync_and_mark(spec.data_mark)
                dam_start = pos - 1
                put(bytes((spec.data_fill,)) * spec.data_length_for(i))
                put_crc(dam_start)
            put(gap * spec.gap_post_data)

        self.reindex()
    # endregion

    # region <Scanner>
    def reindex(self) -> None:
        """Track::reindex: rebuild the sector index from the stream.
        MFM: sync = A1 A1 A1 carrying clock marks (a plain byte match when the track has no clock information),
        then the mark byte: FE = ID field, F8..FB = data field, C2 C2 C2 FC = index mark.
        FM: the mark byte carries a clock mark (without clock information: preceded by two 0x00 bytes).
        A data field must start within the datasheet window after the ID CRC and fit before the end of the stream."""
        self.touch()
        raw = self.raw
        clock = self.clock
        n = len(raw)
        mfm = self.encoding == MFM
        use_clock = any(clock)
        window = DAM_SEARCH_WINDOW_MFM if mfm else DAM_SEARCH_WINDOW_FM
        crc = crc_wd1793 if mfm else crc_wd1793_fm
        sectors: list[SectorIndex] = []
        self.index = sectors
        self.index_mark_offset = NO_OFFSET

        def marked(i: int) -> bool:
            return i < n and (clock[i >> 3] >> (i & 7)) & 1 == 1

        def is_mark_at(p: int) -> bool:
            if p >= n:
                return False
            if mfm:
                if p < 3:
                    return False
                s = p - 3
                if raw[s] != 0xA1 or raw[s + 1] != 0xA1 or raw[s + 2] != 0xA1:
                    return False
                return not use_clock or (marked(s) and marked(s + 1) and marked(s + 2))
            if use_clock:
                return marked(p)
            return p >= 2 and raw[p - 1] == 0 and raw[p - 2] == 0

        def is_index_mark_at(p: int) -> bool:
            if p >= n or raw[p] != INDEX_MARK:
                return False
            if mfm:
                if p < 3:
                    return False
                s = p - 3
                if raw[s] != 0xC2 or raw[s + 1] != 0xC2 or raw[s + 2] != 0xC2:
                    return False
                return not use_clock or (marked(s) and marked(s + 1) and marked(s + 2))
            return is_mark_at(p)

        pos = 3 if mfm else 0
        find = raw.find
        while pos < n and len(sectors) < MAX_SECTORS_PER_TRACK:
            # Jump to the next byte the scanner reacts to: FE, or FC while no index mark was found
            next_fe = find(b"\xfe", pos)
            if self.index_mark_offset == NO_OFFSET:
                next_fc = find(b"\xfc", pos)
                if next_fc != -1 and (next_fe == -1 or next_fc < next_fe):
                    if is_index_mark_at(next_fc):
                        self.index_mark_offset = next_fc
                    pos = next_fc + 1
                    continue
            if next_fe == -1:
                break
            pos = next_fe
            if not is_mark_at(pos):
                pos += 1
                continue
            if pos + 7 > n:
                break

            sector = SectorIndex(self, pos)
            sector.id_crc_valid = crc(raw[pos:pos + 5]) == (raw[pos + 5] | (raw[pos + 6] << 8))
            sector.size = 128 << (raw[pos + 4] & 3)
            id_end = pos + 7
            last_dam = id_end + window
            resume = id_end
            dam = id_end
            while dam <= last_dam and dam < n:
                mark = raw[dam]
                if 0xF8 <= mark <= 0xFB and is_mark_at(dam):
                    data_start = dam + 1
                    if data_start + sector.size + 2 <= n:
                        sector.has_data = True
                        sector.dam = dam
                        sector.data = data_start
                        sector.deleted = mark == DAM_DELETED
                        end = data_start + sector.size
                        sector.data_crc_valid = crc(raw[dam:end]) == (raw[end] | (raw[end + 1] << 8))
                        if not use_clock:
                            resume = end + 2
                    break
                dam += 1
            sectors.append(sector)
            pos = resume
    # endregion

    # region <Sector access>
    def sector_count(self) -> int:
        return len(self.index)

    def find_index(self, number: int) -> SectorIndex | None:
        """Track::findSector: the first sector (stream order) with this ID number"""
        for sector in self.index:
            if sector.number == number:
                return sector
        return None

    def get_sector(self, sector_no: int) -> SectorIndex | None:
        """Track::getSector: by 0-based logical index (ID number sector_no + 1)"""
        return self.find_index((sector_no + 1) & 0xFF)

    def data_for_sector(self, sector_no: int) -> bytes | None:
        sector = self.get_sector(sector_no)
        return sector.data_bytes() if sector and sector.has_data else None

    @property
    def sectors(self) -> list[Sector]:
        """The sectors as values, in stream order (a fresh copy: changing one does not change the track)"""
        if self._snapshot is None or self._snapshot[0] != self._version:
            self._snapshot = (self._version, [s.to_sector() for s in self.index])
        return [Sector(**vars(s)) for s in self._snapshot[1]]

    @sectors.setter
    def sectors(self, sectors: list[Sector]) -> None:
        self.set_sectors(sectors)

    def set_sectors(self, sectors: list[Sector], encoding: str | None = None) -> str:
        """Lay the track out from sector values, the way the C++ FDI loader does for MFM (TR-DOS gaps, gap 3
        shrunk to 16, then the track grown) and the TD0 loader for FM (IBM 3740 layout). Returns the loader's
        note ("" when the layout fitted as is). Raises DiskConversionError when the sectors exceed 12500 bytes."""
        encoding = encoding or self.encoding
        if not sectors:
            if encoding == FM:
                self.resize_raw(DEFAULT_TRACK_SIZE_FM, FM, 0xFF)
            else:
                self.resize_raw(DEFAULT_TRACK_SIZE_MFM)
            return ""
        if encoding == FM:
            spec = TrackFormatSpec.ibm3740()
            spec.sector_numbers = [s.number for s in sectors]
            min_gap = 10
        else:
            spec = TrackFormatSpec.trdos([s.number for s in sectors], len(sectors))
            min_gap = 16
        spec.sector_size_codes = [s.size_code for s in sectors]
        spec.sector_cylinders = [s.cylinder for s in sectors]
        spec.sector_heads = [s.head for s in sectors]
        spec.id_only = [1 if s.no_data else 0 for s in sectors]
        ok, note = fit_track_spec(spec, min_gap)
        if not ok:
            raise DiskConversionError(f"cylinder {self.cylinder} head {self.side}: {note}")
        self.format_track(self.cylinder, self.side, spec)
        for view, sector in zip(list(self.index), sectors):
            good_id = view.compute_id_crc()
            if not sector.id_crc_valid:
                stored = sector.id_crc_stored
                if stored is not None and len(stored) == 2 and stored_u16(stored, 0) != good_id:
                    view.set_id_crc(stored_u16(stored, 0))
                else:
                    view.set_id_crc(good_id ^ 0xFFFF)
            if not view.has_data or sector.no_data:
                continue
            view.write_data(sector.data)
            if sector.deleted:
                view.set_data_address_mark(DAM_DELETED)
            view.recalculate_data_crc()
            if not sector.crc_valid:
                stored = sector.data_crc_stored
                good = view.data_crc()
                if stored is not None and len(stored) == 2 and stored_u16(stored, 0) != good:
                    view.set_data_crc(stored_u16(stored, 0))
                else:
                    view.set_data_crc(good ^ 0xFFFF)
        self.reindex()
        return note

    def find(self, number: int) -> Sector | None:
        sector = self.find_index(number)
        return sector.to_sector() if sector else None

    def is_standard_trdos(self) -> bool:
        """16 sectors R=1..16 (any order), 256 bytes, data present, ID and data CRC valid, not deleted"""
        if len(self.index) != 16:
            return False
        seen = set()
        for sector in self.index:
            if (sector.size_code != 1 or not sector.has_data or sector.deleted or not sector.is_data_crc_valid()
                    or not sector.is_id_crc_valid() or not 1 <= sector.number <= 16):
                return False
            seen.add(sector.number)
        return seen == set(range(1, 17))
    # endregion


class DiskImage:
    """DiskImage: cylinders x sides tracks. A new image has every track erased (no marks)."""

    def __init__(self, cylinders: int, heads: int = 2, write_protected: bool = False):
        self.cylinders = cylinders
        self.heads = heads
        self.write_protected = write_protected
        self.format = ""
        self.warnings: list[str] = []
        self.tracks: dict[tuple[int, int], Track] = {}
        for cyl in range(cylinders):
            for head in range(heads):
                self.tracks[(cyl, head)] = Track(cyl, head)

    @property
    def sides(self) -> int:
        return self.heads

    def get_track(self, cylinder: int, head: int) -> Track | None:
        """getTrackForCylinderAndSide"""
        if 0 <= cylinder < self.cylinders and 0 <= head < self.heads:
            return self.tracks.get((cylinder, head))
        return None

    def track(self, cylinder: int, head: int) -> Track:
        """The track at (cylinder, head), created erased when the image has none there"""
        trk = self.tracks.get((cylinder, head))
        if trk is None:
            trk = self.tracks[(cylinder, head)] = Track(cylinder, head)
        return trk

    def track_at(self, index: int) -> Track | None:
        """getTrack(uint8_t): by logical index cylinder * sides + side"""
        index &= 0xFF
        if self.heads <= 0 or index >= self.cylinders * self.heads:
            return None
        return self.tracks.get(divmod(index, self.heads))

    def iter_tracks(self):
        for cyl in range(self.cylinders):
            for head in range(self.heads):
                trk = self.get_track(cyl, head)
                if trk is not None:
                    yield cyl, head, trk

    def sector_bytes(self, cylinder: int, head: int, number: int) -> bytes | None:
        trk = self.get_track(cylinder, head)
        if trk is None:
            return None
        sector = trk.find_index(number)
        if sector is None or not sector.has_data:
            return None
        return sector.data_bytes()

    def is_standard_trdos_geometry(self) -> bool:
        if self.heads not in (1, 2):
            return False
        for cyl in range(self.cylinders):
            for head in range(self.heads):
                trk = self.get_track(cyl, head)
                if trk is None or not trk.is_standard_trdos():
                    return False
        return True
