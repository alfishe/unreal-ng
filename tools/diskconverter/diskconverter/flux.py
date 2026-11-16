"""Flux / bit-cell layer shared by the HFE and SCP formats: a pure-Python port of the C++ library
unreal-disk (core/src/3rdparty/unreal-disk/src/flux/flux_pll.cpp, mfm_decoder.cpp, mfm_encoder.cpp).

Everything here matches the C++ bit for bit:

- `FluxPll`: flux intervals (ns) -> bit cells. Nominal cell 2000 ns (MFM) / 4000 ns (FM), gain 5 %,
  cells per interval = round-half-away(interval / cell) clamped to 1..8, cell clamped to 75..125 % of nominal.
- `mfm_decode` / `fm_decode`: bit cells -> raw bytes from the index + clock-mark bitmap. The MFM decoder
  re-aligns on every 0x4489 pattern (emits A1 with a clock mark; the C2 index sync 0x5224 is not looked for, it
  also occurs across 00 A1); the FM decoder never re-aligns and marks FE under clock C7 and F8..FC under clock D7.
- A flux interval ends with its transition: three cells are "001"; the time from the last transition to the index
  adds zero cells (`add_trailing_time`).
- `mfm_encode` / `fm_encode`: raw bytes + clock bitmap -> bit cells.

Bit cells are carried as a `str` of '0' / '1' characters: that keeps the per-bit work inside C-level string
operations (slicing, `find`, `int(..., 2)`), which is what makes whole-disk conversions fast in pure Python.

`RawTrackData` is what C++ puts into a DiskImage track after a load (`Track::setRaw` + `setClockBitmap` +
`setWeakByte`): `normalize_track` applies the same size clamp and bitmap sizing.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass

# region <Constants (fdc.h / diskimage.h / flux_pll.cpp)>
MFM_CELL_NS = 2000
FM_CELL_NS = 4000
PLL_GAIN_PERCENT = 5
MIN_CELL_PERCENT = 75
MAX_CELL_PERCENT = 125
MAX_CELLS_PER_INTERVAL = 8

MFM_SYNC_A1 = 0x4489
MFM_SYNC_A1_CELLS = format(MFM_SYNC_A1, "016b")
FM_CLOCK_IDAM = 0xC7
FM_CLOCK_DAM = 0xD7

DEFAULT_TRACK_SIZE_MFM = 6250   # MAX_TRACK_LEN
DEFAULT_TRACK_SIZE_FM = 3125    # NOMINAL_TRACK_LEN_FM
MIN_TRACK_SIZE = 64
MAX_TRACK_SIZE = 12500
GAP_FILL = 0x4E
# endregion </Constants>


@dataclass
class RawTrackData:
    """One track as C++ DiskImage holds it.

    encoding: "mfm" or "fm"
    raw:      byte stream, offset 0 == index pulse
    clock:    bit i (LSB first) = byte i carries a missing-clock mark (A1 in MFM, C7 / D7 clock in FM);
              (len(raw) + 7) // 8 bytes after `normalize_track`
    weak:     None, or a bitmap like `clock`: bit i = byte i is weak / unreliable
    """

    encoding: str
    raw: bytes
    clock: bytes
    weak: bytes | None = None


def default_track_size(encoding: str) -> int:
    return DEFAULT_TRACK_SIZE_FM if encoding == "fm" else DEFAULT_TRACK_SIZE_MFM


def blank_track(encoding: str = "mfm", size: int | None = None) -> RawTrackData:
    """Track::resizeRaw: a gap-filled stream, no marks, no weak bitmap"""
    size = _clamp_track_size(default_track_size(encoding) if size is None else size)
    return RawTrackData(encoding, bytes([GAP_FILL]) * size, bytes((size + 7) // 8), None)


def _clamp_track_size(size: int) -> int:
    return min(max(size, MIN_TRACK_SIZE), MAX_TRACK_SIZE)


def normalize_track(encoding: str, raw: bytes, clock: bytes, weak_offsets=()) -> RawTrackData:
    """Track::setRaw + setClockBitmap + setWeakByte as C++ applies them after a decode.

    setRaw clamps the stream to 64..12500 bytes (padding with 4E / truncating); setClockBitmap copies the
    decoder's bitmap bytes as they are (so a truncated stream may keep stray bits past its end, exactly as in
    C++); setWeakByte ignores offsets past the stream and allocates the weak bitmap on the first one set.
    """
    size = _clamp_track_size(len(raw))
    out_raw = bytes(raw[:size]) + bytes([GAP_FILL]) * (size - min(len(raw), size))
    bitmap_size = (size + 7) // 8
    out_clock = bytes(clock[:bitmap_size]) + bytes(bitmap_size - min(len(clock), bitmap_size))
    weak = None
    for offset in weak_offsets:
        if offset >= size:
            continue
        if weak is None:
            weak = bytearray(bitmap_size)
        weak[offset >> 3] |= 1 << (offset & 7)
    return RawTrackData(encoding, out_raw, out_clock, bytes(weak) if weak is not None else None)


# region <PLL>

def _round_div(numerator: int, denominator: int) -> int:
    """std::round(double(n) / double(d)) for non-negative integers (half away from zero). Exact: a true
    quotient that is not a half is at least 1/(2d) away from one, far more than the double's rounding error."""
    return (2 * numerator + denominator) // (2 * denominator)


def _i32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def _tdiv(numerator: int, denominator: int) -> int:
    """C++ integer division (truncation toward zero)"""
    q = abs(numerator) // abs(denominator)
    return q if (numerator >= 0) == (denominator >= 0) else -q


_BLOCK = 8
_BLOCK_PROBE = 512
_BLOCK_CACHE_LIMIT = 1 << 18


class FluxPll:
    """fdc::flux::FluxPll"""

    MFM = "mfm"
    FM = "fm"

    def __init__(self, mode: str = MFM, gain_percent: int = PLL_GAIN_PERCENT):
        self.nominal_cell_ns = MFM_CELL_NS if mode == self.MFM else FM_CELL_NS
        self.gain_percent = gain_percent
        self.current_cell_ns = self.nominal_cell_ns
        self._parts: list[str] = []
        # (cell, interval) -> (cells string, new cell): flux from encoders and real drives repeats a handful of
        # interval values, so most intervals are a dictionary hit
        self._cache: dict[tuple[int, int], tuple[str, int]] = {}
        self._block_cache: dict = {}
        self._blocks_enabled = True

    def reset(self) -> None:
        self.current_cell_ns = self.nominal_cell_ns
        self._parts = []

    def _step(self, cell: int, interval_ns: int) -> tuple[str, int]:
        cells = _round_div(interval_ns, cell)
        cells = min(max(cells, 1), MAX_CELLS_PER_INTERVAL)
        expected = _i32(cells * cell)
        error = _i32(_i32(interval_ns) - expected)
        adjustment = _tdiv(_i32(error * self.gain_percent), 100)
        min_cell = (self.nominal_cell_ns * MIN_CELL_PERCENT) // 100
        max_cell = (self.nominal_cell_ns * MAX_CELL_PERCENT) // 100
        new_cell = min(max(_i32(cell + adjustment), min_cell), max_cell)
        # An interval runs from one transition to the next: the cells before it, then the cell of the transition
        return "0" * (cells - 1) + "1", new_cell

    def _single(self, cell: int, interval: int) -> tuple[str, int]:
        key = (cell, interval)
        hit = self._cache.get(key)
        if hit is None:
            hit = self._cache[key] = self._step(cell, interval)
        return hit

    def _run_single(self, intervals, cell: int) -> int:
        cache = self._cache
        step = self._step
        append = self._parts.append
        for interval in intervals:
            key = (cell, interval)
            hit = cache.get(key)
            if hit is None:
                hit = cache[key] = step(cell, interval)
            append(hit[0])
            cell = hit[1]
        return cell

    def add_intervals(self, intervals_ns) -> None:
        """FluxPll::addIntervals. Same arithmetic per interval as C++; for speed, runs of _BLOCK intervals that
        repeat from the same cell width (ideal flux written by an encoder) are looked up as one step. When the
        flux does not repeat (a real drive's jitter) the block lookup turns itself off."""
        intervals = intervals_ns if isinstance(intervals_ns, list) else list(intervals_ns)
        cell = self.current_cell_ns
        full = len(intervals) - len(intervals) % _BLOCK
        blocks = self._block_cache
        append = self._parts.append
        single = self._single
        position = 0
        if self._blocks_enabled:
            hits = 0
            looked = 0
            iterator = iter(intervals[:full])
            for block in zip(*([iterator] * _BLOCK)):
                key = (cell, block)
                hit = blocks.get(key)
                looked += 1
                if hit is None:
                    parts = []
                    c = cell
                    for interval in block:
                        cells, c = single(c, interval)
                        parts.append(cells)
                    hit = ("".join(parts), c)
                    if len(blocks) < _BLOCK_CACHE_LIMIT:
                        blocks[key] = hit
                else:
                    hits += 1
                append(hit[0])
                cell = hit[1]
                if looked == _BLOCK_PROBE and hits * 4 < looked:
                    self._blocks_enabled = False
                    break
            position = looked * _BLOCK
        self.current_cell_ns = self._run_single(intervals[position:], cell)

    def add_trailing_time(self, ns: int) -> None:
        """FluxPll::addTrailingTime: the time from the last transition to the index, as zero cells (at most
        MAX_CELLS_PER_INTERVAL - 1)"""
        cells = min(max(_round_div(ns, self.current_cell_ns), 0), MAX_CELLS_PER_INTERVAL - 1)
        self._parts.append("0" * cells)

    def bit_cells(self) -> str:
        if len(self._parts) != 1:
            self._parts = ["".join(self._parts)]
        return self._parts[0]

    @staticmethod
    def detect_mode(intervals_ns) -> str:
        """Double density at 300 rpm: MFM gives 4 / 6 / 8 us intervals, FM 4 / 8 us only. MFM when the ~6 us ones
        (5..7 us) are at least 1/20 of all the double-density ones (3.5..9 us); no such interval at all, or more under
        3.5 us (high density) than double-density ones: MFM"""
        fast = 0
        middle = 0
        short_or_long = 0
        for interval, count in Counter(intervals_ns).items():
            if 1000 <= interval < 3500:
                fast += count
            elif 5000 <= interval < 7000:
                middle += count
            elif 3500 <= interval < 5000 or 7000 <= interval < 9000:
                short_or_long += count
        if middle + short_or_long == 0 or fast > middle + short_or_long:
            return FluxPll.MFM
        return FluxPll.MFM if middle * 20 >= middle + short_or_long else FluxPll.FM

# endregion </PLL>


# region <Decoders>

def _set_bit(bitmap: bytearray, index: int) -> None:
    bitmap[index >> 3] |= 1 << (index & 7)


def _mfm_decode(cells: str) -> tuple[bytes, bytes, list[int]]:
    """mfm_decode plus the cell index at which each byte was emitted"""
    padded = "0" * 16 + cells           # padded[16 + p] == cells[p]
    total = len(cells)
    out = bytearray()
    ends: list[int] = []
    sync_positions: list[int] = []
    last = -1                            # cell index of the last emit (count restarts after it)
    while True:
        # After cell p the register holds padded[p + 1 .. p + 16], so a sync found at padded index f completes
        # at cell f - 1; the first candidate is the cell after the last emit (f >= last + 2)
        found = padded.find(MFM_SYNC_A1_CELLS, last + 2)
        sync_end = found - 1
        limit = sync_end if found >= 0 else total
        count = (limit - last - 1) // 16
        if count > 0:
            start = last + 1
            # bytes complete at cells start+15, start+31, ...; data cells are the odd ones of each window
            data_cells = cells[start + 1:start + 16 * count:2]
            out += int(data_cells, 2).to_bytes(count, "big")
            ends.extend(range(start + 15, start + 16 * count, 16))
            last += 16 * count
        if found < 0:
            break
        sync_positions.append(len(out))
        out.append(0xA1)
        ends.append(sync_end)
        last = sync_end
    clock = bytearray((len(out) + 7) // 8)
    for index in sync_positions:
        _set_bit(clock, index)
    return bytes(out), bytes(clock), ends


def mfm_decode(cells: str) -> tuple[bytes, bytes]:
    """fdc::flux::MfmDecoder.decode on a fresh decoder: (bytes, clock bitmap of (len + 7) // 8 bytes).

    The C++ decoder shifts each cell into a 16-bit register; whenever the register equals 0x4489 it emits A1
    with a clock mark and restarts its 16-cell count, otherwise every 16th cell emits the byte of the odd cells
    of the register. The register is never cleared, so the pattern may straddle a byte boundary. Here: the
    register's initial zeros are a 16-cell prefix, `find` locates each sync, the bytes between two syncs come
    out of one slice."""
    raw, clock, _ = _mfm_decode(cells)
    return raw, clock


def _weak_per_byte(weak_cells: str, ends: list[int]) -> list[int]:
    """Decoder::weakBytes: a byte is weak when a weak cell came in since the byte before it was emitted"""
    if "1" not in weak_cells:
        return []
    prefix = [0]
    running = 0
    for cell in weak_cells:
        running += cell == "1"
        prefix.append(running)
    out = []
    previous = -1
    for index, end in enumerate(ends):
        if prefix[end + 1] - prefix[previous + 1]:
            out.append(index)
        previous = end
    return out


def mfm_decode_weak(cells: str, weak_cells: str) -> tuple[bytes, bytes, list[int]]:
    """MfmDecoder.decode(cells, count, weakCells): (bytes, clock bitmap, offsets of the weak bytes)"""
    raw, clock, ends = _mfm_decode(cells)
    return raw, clock, _weak_per_byte(weak_cells, ends)


_FM_DAM = frozenset((0xF8, 0xF9, 0xFA, 0xFB, 0xFC))   # data marks, and FC: the index mark (clock D7 too)


def fm_decode(cells: str) -> tuple[bytes, bytes]:
    """fdc::flux::FmDecoder.decode on a fresh decoder: cell pairs (clock, data), 8 pairs per byte from the
    first cell on (no re-alignment), a trailing partial byte is dropped"""
    count = (len(cells) // 2) // 8
    if count == 0:
        return b"", b""
    used = cells[:count * 16]
    data = int(used[1::2], 2).to_bytes(count, "big")
    clocks = int(used[0::2], 2).to_bytes(count, "big")
    clock = bytearray((count + 7) // 8)
    for index, (byte, pattern) in enumerate(zip(data, clocks)):
        if (byte == 0xFE and pattern == FM_CLOCK_IDAM) or (byte in _FM_DAM and pattern == FM_CLOCK_DAM):
            _set_bit(clock, index)
    return data, bytes(clock)


def fm_decode_weak(cells: str, weak_cells: str) -> tuple[bytes, bytes, list[int]]:
    """FmDecoder.decode(cells, count, weakCells): byte k is the cells 16k .. 16k + 15"""
    raw, clock = fm_decode(cells)
    return raw, clock, _weak_per_byte(weak_cells, [16 * k + 15 for k in range(len(raw))])


def decode(encoding: str, cells: str) -> tuple[bytes, bytes]:
    return fm_decode(cells) if encoding == "fm" else mfm_decode(cells)

# endregion </Decoders>


# region <Encoders>

def _mfm_byte_cells(prev_data_bit: int, byte: int) -> str:
    cells = []
    prev = prev_data_bit
    for i in range(7, -1, -1):
        data = (byte >> i) & 1
        clock = 1 if (not prev and not data) else 0
        cells.append("1" if clock else "0")
        cells.append("1" if data else "0")
        prev = data
    return "".join(cells)


_MFM_TABLE = [[_mfm_byte_cells(prev, byte) for byte in range(256)] for prev in (0, 1)]


def _fm_byte_cells(byte: int, clock_pattern: int) -> str:
    return "".join(("1" if (clock_pattern >> i) & 1 else "0") + ("1" if (byte >> i) & 1 else "0")
                   for i in range(7, -1, -1))


_FM_TABLE = [_fm_byte_cells(byte, 0xFF) for byte in range(256)]


def _marked_offsets(clock: bytes | None, length: int) -> list[int]:
    """Offsets below `length` whose clock bit is set (the bitmap may be shorter or longer than the stream)"""
    if not clock:
        return []
    out = []
    for slot, value in enumerate(clock[:(length + 7) // 8]):
        if value:
            base = slot << 3
            for bit in range(8):
                if (value >> bit) & 1 and base + bit < length:
                    out.append(base + bit)
    return out


_MFM_PAIR = {(prev, byte): _MFM_TABLE[prev][byte] for prev in (0, 1) for byte in range(256)}
_LSB = bytes(i & 1 for i in range(256))


def mfm_encode(raw: bytes, clock: bytes | None) -> str:
    """fdc::flux::MfmEncoder.encode on a fresh encoder. A clock-marked A1 becomes the 0x4489 pattern; every
    other byte (a clock-marked one included) is plain MFM; the first clock bit assumes a previous data bit 0.
    `clock` may be shorter than the stream (missing bytes read as 0).

    Each byte's cells depend only on the previous byte's last data bit, and the sync pattern leaves that bit at
    1 exactly as an A1 does, so the whole stream maps byte pairs through one table."""
    raw = bytes(raw)
    if not raw:
        return ""
    previous = b"\0" + raw[:-1].translate(_LSB)
    parts = list(map(_MFM_PAIR.__getitem__, zip(previous, raw)))
    for offset in _marked_offsets(clock, len(raw)):
        if raw[offset] == 0xA1:
            parts[offset] = MFM_SYNC_A1_CELLS
    return "".join(parts)


def fm_encode(raw: bytes, clock: bytes | None) -> str:
    """fdc::flux::FmEncoder.encode on a fresh encoder: clock FF, except a clock-marked FE (C7) and a clock-marked
    F8..FC (D7)"""
    raw = bytes(raw)
    parts = list(map(_FM_TABLE.__getitem__, raw))
    for offset in _marked_offsets(clock, len(raw)):
        byte = raw[offset]
        if byte == 0xFE:
            parts[offset] = _fm_byte_cells(byte, FM_CLOCK_IDAM)
        elif byte in _FM_DAM:
            parts[offset] = _fm_byte_cells(byte, FM_CLOCK_DAM)
    return "".join(parts)


def encode(encoding: str, raw: bytes, clock: bytes | None) -> str:
    return fm_encode(raw, clock) if encoding == "fm" else mfm_encode(raw, clock)

# endregion </Encoders>


# region <Bit packing helpers>

_REVERSE = bytes(int(format(i, "08b")[::-1], 2) for i in range(256))
_CELLS_MSB = [format(i, "08b") for i in range(256)]


def bit_reverse_bytes(data: bytes) -> bytes:
    return bytes(data).translate(_REVERSE)


def unpack_msb_first(data: bytes) -> str:
    table = _CELLS_MSB
    return "".join([table[b] for b in data])


def or_cells(a: str, b: str) -> str:
    """Cell-wise OR of two equal-length '0'/'1' strings"""
    if not a:
        return a
    return format(int(a, 2) | int(b, 2), f"0{len(a)}b")


def unpack_lsb_first(data: bytes) -> str:
    return unpack_msb_first(bit_reverse_bytes(data))


def pack_msb_first(cells: str) -> bytes:
    """Cells to bytes, first cell in bit 7; a partial last byte keeps its cells in the high bits"""
    if not cells:
        return b""
    count = (len(cells) + 7) // 8
    return int(cells.ljust(count * 8, "0"), 2).to_bytes(count, "big")


def pack_lsb_first(cells: str) -> bytes:
    return bit_reverse_bytes(pack_msb_first(cells))

# endregion </Bit packing helpers>


@dataclass
class FluxDisk:
    """A flux image (HFE / SCP) as C++ loads it: geometry plus {(cylinder, head): RawTrackData}.

    A track C++ would leave at its DiskImage default (an SCP track missing from the file: C++ fills it with a
    blank TR-DOS formatted track) is absent from `tracks`. `meta` holds the header fields the C++ loader keeps
    for a later save through the same loader object."""

    cylinders: int
    sides: int
    tracks: dict
    warnings: list
    meta: dict
