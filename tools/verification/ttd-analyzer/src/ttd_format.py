"""Hand-written Python parser for the Unreal-NG .ttd binary format.

This module mirrors the canonical Kaitai Struct schema at
``core/src/debugger/ttd/ttd.ksy``. The .ksy is the single source of truth;
this file is the Python reader that conforms to it.

A Kaitai-generated parser can be substituted in its place by running::

    kaitai-struct-compiler core/src/debugger/ttd/ttd.ksy -t python \
        -o tools/verification/ttd-analyzer/src/

The generated module is API-compatible with this one for read access.
The hand-written version is shipped by default to avoid the ksc build-time
dependency and to keep the analyzer runnable from a fresh checkout with
only the Python standard library available.

Conformance is verified by ``tests/test_parser_conformance.py`` which loads
the C++-generated fixture under ``testdata/fixture.ttd`` and asserts every
field round-trips correctly.

Format versioning
-----------------
The .ttd schema is versioned in two places (must always agree):

* ``meta.schema-version`` in ``ttd.ksy``
* the ``schema_version`` field in every .ttd file header

This parser refuses unknown future versions with a clear error message.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Format constants (mirror core/src/debugger/ttd/ttd_dump_format.h)
# ---------------------------------------------------------------------------

MAGIC = b"TTDD"
SCHEMA_VERSION = 1
MAX_SUPPORTED_SCHEMA_VERSION = SCHEMA_VERSION
FLAGS_LITTLE_ENDIAN = 0x0001
PAGE_SIZE = 16384
NEVER_TOUCHED_PAGE_REF = 0xFFFFFFFF

# ---------------------------------------------------------------------------
# Dataclasses for the parsed records
# ---------------------------------------------------------------------------


@dataclass
class Header:
    magic: bytes
    schema_version: int
    flags: int
    model_id: int
    model_ram_pages: int
    cpu_state_size: int
    chipset_state_size: int
    captured_at_unix_ms: int
    emulator_id: str
    session_state: int
    session_start_frame: int
    session_end_frame: int
    page_store_count: int
    checkpoint_count: int


@dataclass
class CpuState:
    pc: int
    sp: int
    af: int
    bc: int
    de: int
    hl: int
    ix: int
    iy: int
    alt_af: int
    alt_bc: int
    alt_de: int
    alt_hl: int
    i: int
    r_low: int
    r_hi: int
    iff1: int
    iff2: int
    im: int
    halted: int
    memptr: int
    q: int
    eipos: int
    haltpos: int
    nmi_in_progress: int
    int_pending: int
    int_gate: int
    halt_cycle: int


@dataclass
class ChipsetState:
    t_states: int
    frame_counter: int
    p7ffd: int
    pfe: int
    peff7: int
    pxxxx: int
    pbffd: int
    pfffd: int
    pdffd: int
    pfdfd: int
    p1ffd: int
    pff77: int
    border_attr: int
    flags: int
    # Extended port latches
    p7efd: int
    p78fd: int
    p7afd: int
    p7cfd: int
    gmx_config: int
    gmx_magic_shift: int
    p00: int
    p80fd: int
    afe: int
    afb: int
    aff77: int
    active_ay: int
    pbd: int
    pbe: int
    pbf: int
    pffba: int
    p7fba: int
    p0f: int
    p1f: int
    p4f: int
    p5f: int
    plsy256: int
    wd_shadow: bytes
    comp_pal: bytes
    ulaplus_mode: int
    ulaplus_reg: int
    ulaplus_cram: bytes
    pfff7: bytes  # 32 bytes (8 × u32)


@dataclass
class Checkpoint:
    index: int
    frame: int
    global_t: int
    cpu: CpuState
    chipset: ChipsetState
    ram_page_refs: List[int]  # slot index or NEVER_TOUCHED_PAGE_REF
    ay_blob: bytes
    fdc_blob: bytes
    tape_blob: bytes
    covox_blob: bytes


@dataclass
class TtdDump:
    """The fully-parsed .ttd file in memory.

    The page store is held as a single contiguous bytearray for memory
    efficiency — for a typical 2000-checkpoint session it's <5 MB and
    random page access by index is a single slice operation.
    """

    header: Header
    page_store: bytes  # page_store_count * PAGE_SIZE bytes, packed
    checkpoints: List[Checkpoint] = field(default_factory=list)

    # Convenience accessors -------------------------------------------------

    @property
    def page_count(self) -> int:
        return len(self.page_store) // PAGE_SIZE

    def get_page(self, slot_index: int) -> bytes:
        """Return the 16 KB page at the given slot index."""
        if slot_index < 0 or slot_index >= self.page_count:
            raise IndexError(
                f"page slot {slot_index} out of range (count={self.page_count})"
            )
        start = slot_index * PAGE_SIZE
        return self.page_store[start : start + PAGE_SIZE]

    def materialize_ram(self, cp: Checkpoint) -> bytes:
        """Build the full RAM image for a checkpoint by following its refs."""
        out = bytearray(self.header.model_ram_pages * PAGE_SIZE)
        for page_idx, ref in enumerate(cp.ram_page_refs):
            if ref == NEVER_TOUCHED_PAGE_REF:
                # Live RAM at session start was the historical content; in
                # the dump we don't know what that was. Zero-fill is the
                # best we can do — surface this in the integrity report.
                continue
            out[page_idx * PAGE_SIZE : (page_idx + 1) * PAGE_SIZE] = self.get_page(ref)
        return bytes(out)


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------


class TtdFormatError(Exception):
    """Raised when the .ttd file is malformed, truncated, or unsupported."""


class _Reader:
    """Sequential little-endian reader over a bytes buffer."""

    __slots__ = ("_buf", "_pos")

    def __init__(self, buf: bytes, pos: int = 0):
        self._buf = buf
        self._pos = pos

    @property
    def pos(self) -> int:
        return self._pos

    @property
    def remaining(self) -> int:
        return len(self._buf) - self._pos

    def need(self, n: int) -> None:
        if self.remaining < n:
            raise TtdFormatError(
                f"unexpected end-of-file at offset {self._pos}: "
                f"need {n} more bytes, have {self.remaining}"
            )

    def u8(self) -> int:
        self.need(1)
        v = self._buf[self._pos]
        self._pos += 1
        return v

    def u16(self) -> int:
        self.need(2)
        v = struct.unpack_from("<H", self._buf, self._pos)[0]
        self._pos += 2
        return v

    def u32(self) -> int:
        self.need(4)
        v = struct.unpack_from("<I", self._buf, self._pos)[0]
        self._pos += 4
        return v

    def u64(self) -> int:
        self.need(8)
        v = struct.unpack_from("<Q", self._buf, self._pos)[0]
        self._pos += 8
        return v

    def take(self, n: int) -> bytes:
        self.need(n)
        v = self._buf[self._pos : self._pos + n]
        self._pos += n
        return v

    def expect(self, expected: bytes, what: str) -> None:
        actual = self.take(len(expected))
        if actual != expected:
            raise TtdFormatError(
                f"{what}: expected {expected!r}, got {actual!r}"
            )


def parse_header(r: _Reader) -> Header:
    magic = r.take(4)
    if magic != MAGIC:
        raise TtdFormatError(
            f"bad magic {magic!r} — not a .ttd file (expected {MAGIC!r})"
        )

    schema_version = r.u16()
    if schema_version > MAX_SUPPORTED_SCHEMA_VERSION:
        raise TtdFormatError(
            f"file is schema v{schema_version}, this parser supports up to "
            f"v{MAX_SUPPORTED_SCHEMA_VERSION}. Regenerate the analyzer from "
            f"the latest ttd.ksy or downgrade the file producer."
        )
    if schema_version != SCHEMA_VERSION:
        raise TtdFormatError(
            f"unsupported schema v{schema_version} (only v{SCHEMA_VERSION} implemented)"
        )

    flags = r.u16()
    if (flags & FLAGS_LITTLE_ENDIAN) == 0:
        raise TtdFormatError(
            "file is big-endian; only little-endian .ttd files are supported"
        )

    model_id = r.u8()
    model_ram_pages = r.u8()
    cpu_state_size = r.u16()
    chipset_state_size = r.u16()
    captured_at_unix_ms = r.u64()
    emulator_id_len = r.u8()
    emulator_id = r.take(emulator_id_len).decode("utf-8", errors="replace")
    session_state = r.u8()
    session_start_frame = r.u64()
    session_end_frame = r.u64()
    page_store_count = r.u32()
    checkpoint_count = r.u32()
    r.take(8)  # reserved

    return Header(
        magic=magic,
        schema_version=schema_version,
        flags=flags,
        model_id=model_id,
        model_ram_pages=model_ram_pages,
        cpu_state_size=cpu_state_size,
        chipset_state_size=chipset_state_size,
        captured_at_unix_ms=captured_at_unix_ms,
        emulator_id=emulator_id,
        session_state=session_state,
        session_start_frame=session_start_frame,
        session_end_frame=session_end_frame,
        page_store_count=page_store_count,
        checkpoint_count=checkpoint_count,
    )


def parse_cpu(r: _Reader) -> CpuState:
    # The C++ ``TTDCpuState`` struct is plain POD with natural alignment —
    # the writer emits ``sizeof(TTDCpuState)`` bytes verbatim, which on every
    # supported compiler (GCC/Clang/MSVC, x86_64/arm64) includes 3 padding
    # bytes at structurally-imposed offsets. We must skip them or every
    # subsequent field will be misaligned.
    #
    # Layout (offset → field):
    #   00-23 : 12 × u16 (pc..alt_hl)
    #   24-30 : 7 × u8  (i, r_low, r_hi, iff1, iff2, im, halted)
    #   31    : PADDING (align memptr u16 to 2-byte boundary)
    #   32-33 : memptr u16
    #   34    : q u8
    #   35    : PADDING (align eipos u16)
    #   36-37 : eipos u16
    #   38-39 : haltpos u16
    #   40-42 : nmi_in_progress, int_pending, int_gate u8
    #   43    : PADDING (align halt_cycle u32 to 4-byte boundary)
    #   44-47 : halt_cycle u32
    pc = r.u16(); sp = r.u16()
    af = r.u16(); bc = r.u16(); de = r.u16(); hl = r.u16()
    ix = r.u16(); iy = r.u16()
    alt_af = r.u16(); alt_bc = r.u16(); alt_de = r.u16(); alt_hl = r.u16()
    i = r.u8(); r_low = r.u8(); r_hi = r.u8()
    iff1 = r.u8(); iff2 = r.u8(); im = r.u8(); halted = r.u8()
    r.u8()  # padding byte before memptr (offset 31)
    memptr = r.u16()
    q = r.u8()
    r.u8()  # padding byte before eipos (offset 35)
    eipos = r.u16()
    haltpos = r.u16()
    nmi_in_progress = r.u8()
    int_pending = r.u8()
    int_gate = r.u8()
    r.u8()  # padding byte before halt_cycle (offset 43)
    halt_cycle = r.u32()
    return CpuState(
        pc=pc, sp=sp, af=af, bc=bc, de=de, hl=hl, ix=ix, iy=iy,
        alt_af=alt_af, alt_bc=alt_bc, alt_de=alt_de, alt_hl=alt_hl,
        i=i, r_low=r_low, r_hi=r_hi, iff1=iff1, iff2=iff2, im=im,
        halted=halted, memptr=memptr, q=q, eipos=eipos, haltpos=haltpos,
        nmi_in_progress=nmi_in_progress, int_pending=int_pending,
        int_gate=int_gate, halt_cycle=halt_cycle,
    )


def parse_chipset(r: _Reader) -> ChipsetState:
    return ChipsetState(
        t_states=r.u64(),
        frame_counter=r.u64(),
        p7ffd=r.u8(),
        pfe=r.u8(),
        peff7=r.u8(),
        pxxxx=r.u8(),
        pbffd=r.u8(),
        pfffd=r.u8(),
        pdffd=r.u8(),
        pfdfd=r.u8(),
        p1ffd=r.u8(),
        pff77=r.u8(),
        border_attr=r.u8(),
        flags=r.u8(),
        p7efd=r.u8(),
        p78fd=r.u8(),
        p7afd=r.u8(),
        p7cfd=r.u8(),
        gmx_config=r.u8(),
        gmx_magic_shift=r.u8(),
        p00=r.u8(),
        p80fd=r.u8(),
        afe=r.u8(),
        afb=r.u8(),
        aff77=r.u8(),
        active_ay=r.u8(),
        pbd=r.u8(),
        pbe=r.u8(),
        pbf=r.u8(),
        pffba=r.u8(),
        p7fba=r.u8(),
        p0f=r.u8(),
        p1f=r.u8(),
        p4f=r.u8(),
        p5f=r.u8(),
        plsy256=r.u8(),
        wd_shadow=r.take(4),
        comp_pal=r.take(16),
        ulaplus_mode=r.u8(),
        ulaplus_reg=r.u8(),
        ulaplus_cram=r.take(64),
        pfff7=r.take(32),
    )


def parse_blob(r: _Reader) -> bytes:
    size = r.u32()
    if size > (1 << 20):  # 1 MB sanity cap
        raise TtdFormatError(
            f"implausible peripheral blob size {size} (>1 MB)"
        )
    return r.take(size)


def parse_checkpoint(r: _Reader, model_ram_pages: int, index: int) -> Checkpoint:
    frame = r.u64()
    global_t = r.u64()
    cpu = parse_cpu(r)
    chipset = parse_chipset(r)
    ram_page_refs = [r.u32() for _ in range(model_ram_pages)]
    ay = parse_blob(r)
    fdc = parse_blob(r)
    tape = parse_blob(r)
    covox = parse_blob(r)
    return Checkpoint(
        index=index,
        frame=frame,
        global_t=global_t,
        cpu=cpu,
        chipset=chipset,
        ram_page_refs=ram_page_refs,
        ay_blob=ay,
        fdc_blob=fdc,
        tape_blob=tape,
        covox_blob=covox,
    )


def parse_bytes(data: bytes) -> TtdDump:
    """Parse a complete .ttd file from an in-memory bytes buffer."""
    r = _Reader(data)
    header = parse_header(r)

    # Page store: a single contiguous block. We hold it as one bytes object
    # so get_page is a slice (no per-page allocation).
    page_bytes = r.take(header.page_store_count * PAGE_SIZE)

    checkpoints: List[Checkpoint] = []
    for i in range(header.checkpoint_count):
        checkpoints.append(parse_checkpoint(r, header.model_ram_pages, i))

    return TtdDump(header=header, page_store=page_bytes, checkpoints=checkpoints)


def parse_file(path: str) -> TtdDump:
    """Parse a .ttd file from disk. Reads the whole file into memory —
    for typical session sizes (<10 MB) this is faster than incremental IO."""
    with open(path, "rb") as f:
        data = f.read()
    return parse_bytes(data)
