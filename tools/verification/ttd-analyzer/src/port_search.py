"""'When did the program ...' queries over a .ttd file's port journals.

The same events, options and results as the emulator's search
(core/src/debugger/ttd/ttdportsearch.{h,cpp}): every surface of the emulator
and this analyzer answer a question the same way, and the tests check that on
real recordings (tests/test_port_search.py). Nothing is replayed - the port
journals (flag bit 8) hold every IN result and OUT of the session with its
time and PC.

Named events:
    key [KEY]        IN: the program saw a key down, once per press. KEY: a
                     matrix key name ("a", "enter", "space", "caps", "symbol"
                     ...); only reads of that key's half-row alone count (a
                     read of all rows cannot tell A from Q). No KEY: any key
    ear              IN: the program saw the tape (EAR) bit change
    ay-read [REG]    IN from #FFFD
    ay-write [REG]   OUT to #BFFD
    ay-select [REG]  OUT to #FFFD
    border           OUT #FE changed the border color
    beeper           OUT #FE changed the beeper bit
    in / out         every IN / OUT (narrow with the options)

Options (text, as on the emulator's surfaces): limit, newest, from, to
(frame or frame:T), port, port_mask, value, value_mask, match
(any|equals|any-clear|any-set), trigger (every|rising|change), stream_mask,
ay_register.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from .ttd_format import PortJournal, PortRecord, TtdDump

READ, WRITE = "in", "out"
EVENT_NAMES = ["key", "ear", "ay-read", "ay-write", "ay-select", "border", "beeper", "in", "out"]

# 128K AY decoding: A15 and A1 select the chip, A14 register (1) or data (0)
_AY_MASK = 0xC002
_AY_REGISTER_PORT = 0xC000
_AY_DATA_PORT = 0x8000

# The keyboard matrix: half-row port -> keys from bit 0 to bit 4
_ROWS = {
    0xFEFE: ["caps", "z", "x", "c", "v"],
    0xFDFE: ["a", "s", "d", "f", "g"],
    0xFBFE: ["q", "w", "e", "r", "t"],
    0xF7FE: ["1", "2", "3", "4", "5"],
    0xEFFE: ["0", "9", "8", "7", "6"],
    0xDFFE: ["p", "o", "i", "u", "y"],
    0xBFFE: ["enter", "l", "k", "j", "h"],
    0x7FFE: ["space", "symbol", "m", "n", "b"],
}
# The keyboard API's aliases (DebugKeyboardManager::ResolveKeyName)
_ALIASES = {
    "shift": "caps", "capsshift": "caps", "caps_shift": "caps", "cs": "caps",
    "sym": "symbol", "symshift": "symbol", "sym_shift": "symbol", "ss": "symbol",
    "return": "enter", " ": "space",
}
_KEYS: Dict[str, Tuple[int, int]] = {
    name: (row, bit) for row, names in _ROWS.items() for bit, name in enumerate(names)
}


class PortQueryError(ValueError):
    """An unknown event, argument or option."""


@dataclass
class PortQuery:
    direction: str = READ
    port_mask: int = 0
    port_value: int = 0
    value_match: str = "any"
    value_mask: int = 0xFF
    value: int = 0
    trigger: str = "every"
    stream_mask: int = 0xFFFF
    ay_register: int = -1
    from_time: Tuple[int, int] = (0, 0)
    to_time: Tuple[int, int] = (2 ** 64 - 1, 2 ** 32 - 1)
    limit: int = 100
    newest_first: bool = False


@dataclass
class PortHit:
    record: PortRecord
    index: int
    ay_register: int = -1

    def as_dict(self) -> dict:
        d = {"index": self.index, "frame": self.record.frame, "tinframe": self.record.t_in_frame,
             "port": self.record.port, "value": self.record.value, "pc": self.record.pc}
        if self.ay_register >= 0:
            d["ay_register"] = self.ay_register
        return d


@dataclass
class PortSearchResult:
    direction: str
    hits: List[PortHit] = field(default_factory=list)
    truncated: bool = False
    scanned: int = 0


def _parse_number(text: str, maximum: int) -> int:
    s = str(text).strip().lower()
    base = 10
    if len(s) > 1 and s[0] in "#$":
        s, base = s[1:], 16
    elif len(s) > 2 and s.startswith("0x"):
        s, base = s[2:], 16
    try:
        n = int(s, base)
    except ValueError:
        raise PortQueryError(f"not a number: '{text}'") from None
    if not 0 <= n <= maximum:
        raise PortQueryError(f"'{text}' is out of range 0..{maximum}")
    return n


def _parse_time(text: str) -> Tuple[int, int]:
    frame, _, t = str(text).partition(":")
    return _parse_number(frame, 2 ** 64 - 1), (_parse_number(t, 2 ** 32 - 1) if t else 0)


def _register(arg: str) -> int:
    if arg is None or arg == "":
        return -1
    try:
        return _parse_number(arg, 15)
    except PortQueryError:
        raise PortQueryError(f"AY register must be 0..15, got '{arg}'") from None


def build_query(event: str, arg: Optional[str] = None) -> PortQuery:
    """A named event (ttd::BuildPortEventQuery)."""
    q = PortQuery()
    e = event.lower()
    arg = "" if arg is None else str(arg)
    if e == "key":
        q.direction, q.port_mask, q.port_value = READ, 0x0001, 0
        q.value_match, q.value_mask, q.trigger = "any-clear", 0x1F, "rising"
        if arg:
            name = _ALIASES.get(arg.lower(), arg.lower())
            if name not in _KEYS:
                raise PortQueryError(f"'{arg}' is not a key of the ZX Spectrum matrix")
            row, bit = _KEYS[name]
            q.port_mask, q.port_value, q.value_mask = 0xFF01, row & 0xFF00, 1 << bit
        return q
    if e == "ear":
        if arg:
            raise PortQueryError("'ear' takes no argument")
        q.direction, q.port_mask, q.port_value = READ, 0x0001, 0
        q.value_mask, q.trigger, q.stream_mask = 0x40, "change", 0x0001
        return q
    if e in ("ay-read", "ay-write", "ay-select"):
        reg = _register(arg)
        q.direction = READ if e == "ay-read" else WRITE
        q.port_mask = _AY_MASK
        q.port_value = _AY_DATA_PORT if e == "ay-write" else _AY_REGISTER_PORT
        if e == "ay-select":
            if reg >= 0:
                q.value_match, q.value_mask, q.value = "equals", 0xFF, reg
        else:
            q.ay_register = reg
        return q
    if e in ("border", "beeper"):
        if arg:
            raise PortQueryError(f"'{e}' takes no argument")
        q.direction, q.port_mask, q.port_value = WRITE, 0x0001, 0
        q.value_mask = 0x07 if e == "border" else 0x10
        q.trigger, q.stream_mask = "change", 0x0001
        return q
    if e in ("in", "out"):
        if arg:
            raise PortQueryError(f"'{e}' takes no argument (use the port and value filters)")
        q.direction = READ if e == "in" else WRITE
        return q
    raise PortQueryError(f"unknown event '{event}'")


def apply_option(q: PortQuery, name: str, value) -> None:
    """One text option (ttd::ApplyPortQueryOption)."""
    n = name.lower()
    v = str(value).lower() if not isinstance(value, bool) else ("true" if value else "false")
    if n == "limit":
        q.limit = _parse_number(v, 1_000_000)
        if q.limit == 0:
            raise PortQueryError("'limit' must be 1..1000000")
    elif n in ("newest", "newest_first"):
        if v not in ("true", "false", "1", "0"):
            raise PortQueryError(f"'{name}' must be true or false")
        q.newest_first = v in ("true", "1")
    elif n in ("from", "to"):
        t = _parse_time(v)
        if n == "from":
            q.from_time = t
        else:
            q.to_time = t
    elif n == "port":
        q.port_value = _parse_number(v, 0xFFFF)
        if q.port_mask == 0:
            q.port_mask = 0xFFFF
        q.port_value &= q.port_mask
    elif n == "port_mask":
        q.port_mask = _parse_number(v, 0xFFFF)
        q.port_value &= q.port_mask
    elif n == "value":
        q.value = _parse_number(v, 0xFF)
        if q.value_match == "any":
            q.value_match = "equals"
    elif n == "value_mask":
        q.value_mask = _parse_number(v, 0xFF)
    elif n == "match":
        if v not in ("any", "equals", "any-clear", "any-set"):
            raise PortQueryError(f"'{name}' must be any, equals, any-clear or any-set")
        q.value_match = v
    elif n == "trigger":
        if v not in ("every", "rising", "change"):
            raise PortQueryError(f"'{name}' must be every, rising or change")
        q.trigger = v
    elif n == "stream_mask":
        q.stream_mask = _parse_number(v, 0xFFFF)
    elif n == "ay_register":
        q.ay_register = _parse_number(v, 15)
    else:
        raise PortQueryError(f"unknown option '{name}'")


def _passes(q: PortQuery, value: int) -> bool:
    masked = value & q.value_mask
    if q.value_match == "any":
        return True
    if q.value_match == "equals":
        return masked == (q.value & q.value_mask)
    if q.value_match == "any-clear":
        return masked != q.value_mask
    return masked != 0


def _selected_register(value: int, current: int) -> int:
    if value < 16:
        return value
    if value >= 0xF0:
        return current  # TurboSound / TSFM control keeps the selection
    return -1


def search(reads: PortJournal, writes: PortJournal, q: PortQuery) -> PortSearchResult:
    """Scan the journals (ttd::SearchPortEvents)."""
    if q.limit < 1:
        raise PortQueryError("limit must be at least 1")
    if q.to_time < q.from_time:
        raise PortQueryError("the time window ends before it starts")
    journal = reads if q.direction == READ else writes
    follow_ay = q.ay_register >= 0
    result = PortSearchResult(direction=q.direction)
    previous: Dict[int, int] = {}
    hits: deque = deque()
    selected = -1
    w_index = 0
    wrecs = writes.records
    for index, r in enumerate(journal.records):
        t = (r.frame, r.t_in_frame)
        if q.to_time < t:
            break
        result.scanned += 1
        if follow_ay:
            if q.direction == READ:
                while w_index < len(wrecs) and (wrecs[w_index].frame, wrecs[w_index].t_in_frame) < t:
                    if (wrecs[w_index].port & _AY_MASK) == _AY_REGISTER_PORT:
                        selected = _selected_register(wrecs[w_index].value, selected)
                    w_index += 1
            elif (r.port & _AY_MASK) == _AY_REGISTER_PORT:
                selected = _selected_register(r.value, selected)
        if (r.port & q.port_mask) != q.port_value:
            continue
        if follow_ay and selected != q.ay_register:
            continue
        passes = _passes(q, r.value)
        stream = r.port & q.stream_mask
        if q.trigger == "every":
            hit = passes
        elif q.trigger == "rising":
            hit = passes and previous.get(stream, 0) == 0
            previous[stream] = 1 if passes else 0
        else:
            masked = r.value & q.value_mask
            hit = passes and stream in previous and previous[stream] != masked
            previous[stream] = masked
        if not hit or t < q.from_time:
            continue
        hits.append(PortHit(r, index, selected if follow_ay else -1))
        if len(hits) > q.limit:
            result.truncated = True
            if not q.newest_first:
                hits.pop()
                break
            hits.popleft()
    result.hits = list(hits)
    if q.newest_first:
        result.hits.reverse()
    return result


def search_dump(dump: TtdDump, event: str, arg: Optional[str] = None, **options) -> PortSearchResult:
    """Search a parsed .ttd: a named event, its argument and text options."""
    if dump.port_reads is None or dump.port_writes is None:
        raise PortQueryError("the file has no port journals (recorded on TSConf, ZX Next or with NeoGS, "
                             "before the journals existed, or resumed after the machine ran unrecorded)")
    q = build_query(event, arg)
    for name, value in options.items():
        apply_option(q, name, value)
    return search(dump.port_reads, dump.port_writes, q)
