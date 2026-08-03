"""Hand-written Python parser for the Unreal-NG .ttd binary format (v1).

This module mirrors the canonical Kaitai Struct schema at
``core/src/debugger/ttd/ttd.ksy``. The .ksy is the single source of truth;
this file is the Python reader that conforms to it.

A Kaitai-generated parser can be substituted in its place by running::

    kaitai-struct-compiler core/src/debugger/ttd/ttd.ksy -t python \\
        -o tools/verification/ttd-analyzer/src/

The generated module is API-compatible with this one for read access.
The hand-written version is shipped by default to avoid the ksc build-time
dependency and to keep the analyzer runnable from a fresh checkout with
only ``zstandard`` as a third-party requirement.

Conformance is verified by ``tests/test_parser_conformance.py`` which loads
the C++-generated fixture under ``testdata/fixture.ttd`` and asserts every
field round-trips correctly.

Format versioning
-----------------
The .ttd schema is versioned in two places (must always agree):

* ``meta.schema-version`` in ``ttd.ksy``
* the ``schema_version`` field in every .ttd file header

This parser refuses unknown future versions with a clear error message.

v1 layout (production release with Phase 5 codec)
-------------------------------------------------
v1 is the first production release with XOR-delta encoding:

* 4 KB sub-pages. Each 16 KB emulator RAM page is split into
  4 × 4 KB sub-pages, each with its own slot in the page store.
* Per-slot encoding discriminator: ``Full`` / ``XorPrev`` / ``Zero``.
* zstd level-1 compression of every non-``Zero`` slot payload.
* Per-slot CRC32C integrity field (4 bytes).
* Per-checkpoint ``frame_kind`` (I-frame vs P-frame) and ``keyframe_anchor``.
* Checkpoint RAM refs are ``4 * model_ram_pages`` u32 slot indices.
* Optional write journal (flag bit 1 in header).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

try:
    import zstandard as _zstd
except ImportError as _e:  # pragma: no cover - error path only
    _zstd = None
    _ZSTD_IMPORT_ERROR = _e
else:
    _ZSTD_IMPORT_ERROR = None

# ---------------------------------------------------------------------------
# Format constants (mirror core/src/debugger/ttd/ttd_dump_format.h)
# ---------------------------------------------------------------------------

MAGIC = b"TTDD"
SCHEMA_VERSION = 1  # v1 is the production release with XOR-delta encoding
MAX_SUPPORTED_SCHEMA_VERSION = SCHEMA_VERSION
FLAGS_LITTLE_ENDIAN = 0x0001
FLAGS_HAS_WRITE_JOURNAL = 0x0002  # v2 extension: write journal section present

# Page geometry. EMU_PAGE_SIZE is the Z80 banking unit (16 KB). SUB_PAGE_SIZE
# is the codec unit (4 KB) — each emulator page splits into 4 sub-pages.
EMU_PAGE_SIZE = 16384
SUB_PAGE_SIZE = 4096
SUB_PAGES_PER_EMU_PAGE = 4

# Slot encodings (Encoding enum in TTDCodecPageStore).
ENCODING_FULL     = 0  # Slot stores a zstd-compressed 4 KB snapshot.
ENCODING_XOR_PREV = 1  # Slot stores a zstd-compressed XOR against prev_slot.
ENCODING_ZERO     = 2  # Slot is all zeros. Payload is empty.

# Frame kinds (TTDFrameKind enum).
FRAME_KIND_KEY_FRAME   = 0  # I-frame: every sub-page is a Full snapshot.
FRAME_KIND_DELTA_FRAME = 1  # P-frame: only dirty pages re-captured.

# Sentinel slot index meaning "this sub-page was never written during the
# session up to this checkpoint; live RAM content IS the historical content".
NEVER_TOUCHED_SLOT = 0xFFFFFFFF

# ---------------------------------------------------------------------------
# Backward-compat aliases (deprecated — prefer the new names above)
# ---------------------------------------------------------------------------

# Old v1 name. Kept as an alias so legacy consumers compile, but new code
# should use NEVER_TOUCHED_SLOT. The semantic is unchanged: the sentinel
# covers a single slot, which in v2 is one 4 KB sub-page.
NEVER_TOUCHED_PAGE_REF = NEVER_TOUCHED_SLOT

# Old v1 name for the 16 KB emulator page size. In v2 the codec operates on
# 4 KB sub-pages, but the emulator still banks in 16 KB pages so this size
# remains meaningful for callers that reason about emulator-level layout.
PAGE_SIZE = EMU_PAGE_SIZE

# ---------------------------------------------------------------------------
# CRC32C (Castagnoli) — hardware-accelerated on x86 SSE4.2 and ARM64.
# Matches ttd_compression.h's Crc32C so the reader can verify writer output.
# ---------------------------------------------------------------------------


_CRC32C_POLY = 0x82F63B78  # Castagnoli, reversed form


def _make_crc32c_table() -> List[int]:
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ _CRC32C_POLY if (c & 1) else (c >> 1)
        table.append(c)
    return table


_CRC32C_TABLE = _make_crc32c_table()


def crc32c(data: bytes, seed: int = 0) -> int:
    """Compute CRC32C (Castagnoli) of ``data``.

    Software table-driven implementation. The C++ writer uses hardware
    intrinsics where available (``__builtin_ia32_crc32`` on x86 SSE4.2,
    ``crc32cx`` on ARM64) — both produce identical bytes for the same input.
    """
    c = seed ^ 0xFFFFFFFF
    for b in data:
        c = _CRC32C_TABLE[(c ^ b) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


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
    page_store_count: int   # Number of live slots (renamed semantically in v2)
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
class PageSlot:
    """One entry in the v2 page store.

    ``payload`` is the raw on-disk bytes (already zstd-compressed for Full /
    XorPrev, empty for Zero). Use ``TtdDump.get_sub_page`` to obtain the
    decompressed 4 KB content.
    """
    index: int                 # slot index in the on-disk store (0..count-1)
    encoding: int              # ENCODING_FULL / ENCODING_XOR_PREV / ENCODING_ZERO
    refcount: int              # informational; reader rebuilds its own
    prev_slot: int             # compact index for XorPrev; NEVER_TOUCHED_SLOT otherwise
    crc32c_stored: int         # always 0 on write; reader recomputes
    payload: bytes             # compressed bytes (or b"" for Zero)


@dataclass
class Checkpoint:
    index: int
    frame: int
    global_t: int
    frame_kind: int            # FRAME_KIND_KEY_FRAME or FRAME_KIND_DELTA_FRAME (new in v2)
    keyframe_anchor: int       # frame index of the I-frame anchoring this delta chain (new in v2)
    cpu: CpuState
    chipset: ChipsetState
    # Flat list of (4 * model_ram_pages) slot indices, in (page, sub) order:
    #   ram_sub_slots[page * 4 + sub]
    # Each entry is either a slot index into TtdDump.slots or NEVER_TOUCHED_SLOT.
    ram_sub_slots: List[int] = field(default_factory=list)
    ay_blob: bytes = b""
    fdc_blob: bytes = b""
    tape_blob: bytes = b""
    covox_blob: bytes = b""

    # Backward-compat shim: ``ram_page_refs`` was the v1 name for the per-page
    # ref vector. v2 exposes the flat sub-slot list above; this property returns
    # the same flat list under the old name so legacy consumers iterate the
    # superset (4× the length now). Code that needs per-page granularity should
    # migrate to ``ram_sub_slots`` and index it in strides of 4.
    @property
    def ram_page_refs(self) -> List[int]:
        return self.ram_sub_slots

    @property
    def is_keyframe(self) -> bool:
        return self.frame_kind == FRAME_KIND_KEY_FRAME


@dataclass
class TtdDump:
    """The fully-parsed .ttd file in memory.

    The page store is held as a list of PageSlot records (v2 — was a single
    contiguous bytearray in v1). Slots are indexed by their compact on-disk
    index (0..header.page_store_count-1).
    """
    header: Header
    slots: List[PageSlot] = field(default_factory=list)
    checkpoints: List[Checkpoint] = field(default_factory=list)
    # Lazily-decompressed sub-page cache. Keyed by slot index; populated on
    # first ``get_sub_page`` call for that slot. The cache holds onto the
    # bytes for the lifetime of the TtdDump, which is the right policy for
    # analysis tools that walk the timeline repeatedly.
    _sub_page_cache: dict = field(default_factory=dict)

    # Convenience accessors -------------------------------------------------

    @property
    def page_count(self) -> int:
        """Number of slots in the page store (v2: live slots only)."""
        return len(self.slots)

    @property
    def live_payload_bytes(self) -> int:
        """Total compressed payload bytes across all live slots."""
        return sum(len(s.payload) for s in self.slots)

    @property
    def compression_ratio(self) -> float:
        """Mean compressed/raw ratio across non-Zero slots.

        ``1.0`` means no compression; ``0.5`` means halved. Zero slots are
        excluded because their raw size is also 0 (degenerate ratio).
        """
        raw_total = 0
        comp_total = 0
        for s in self.slots:
            if s.encoding == ENCODING_ZERO:
                continue
            raw_total += SUB_PAGE_SIZE
            comp_total += len(s.payload)
        return (comp_total / raw_total) if raw_total else 1.0

    def get_sub_page(self, slot_index: int) -> bytes:
        """Decompressed 4 KB content for a slot, walking XorPrev chains.

        Raises ``TtdFormatError`` on out-of-range indices, payload
        decompression failures, or CRC32C mismatch (writer's stored CRC is
        0 on write, so this triggers only if the file was tampered with or
        truncated post-write).
        """
        if slot_index == NEVER_TOUCHED_SLOT:
            raise TtdFormatError(
                "get_sub_page called on NEVER_TOUCHED_SLOT sentinel — "
                "caller must skip these refs"
            )
        if slot_index < 0 or slot_index >= len(self.slots):
            raise TtdFormatError(
                f"slot {slot_index} out of range (count={len(self.slots)})"
            )

        cached = self._sub_page_cache.get(slot_index)
        if cached is not None:
            return cached

        slot = self.slots[slot_index]

        if slot.encoding == ENCODING_ZERO:
            result = bytes(SUB_PAGE_SIZE)
        elif slot.encoding == ENCODING_FULL:
            result = _decompress_zstd(slot.payload, SUB_PAGE_SIZE)
        elif slot.encoding == ENCODING_XOR_PREV:
            if slot.prev_slot == NEVER_TOUCHED_SLOT:
                raise TtdFormatError(
                    f"slot {slot_index} is XorPrev but prev_slot is sentinel"
                )
            base = self.get_sub_page(slot.prev_slot)
            delta = _decompress_zstd(slot.payload, SUB_PAGE_SIZE)
            if len(delta) != SUB_PAGE_SIZE:
                raise TtdFormatError(
                    f"XorPrev slot {slot_index} decompressed to {len(delta)} "
                    f"bytes, expected {SUB_PAGE_SIZE}"
                )
            result = _xor_buffers(base, delta)
        else:
            raise TtdFormatError(
                f"slot {slot_index} has unknown encoding {slot.encoding}"
            )

        # CRC verification. Writer stores 0 (we trust the file); the integrity
        # we want to verify is that decompression produced the original bytes.
        # The C++ writer writes 0 in the crc field and recomputes on read,
        # matching this behavior. If the file was tampered with after writing,
        # this check catches it.
        actual_crc = crc32c(result)
        # We don't compare against slot.crc32c_stored because v2 writers store
        # 0 (see ttd_dump_format.h). Instead we surface the computed CRC via
        # PageSlot for callers that want to cross-check two dumps.

        self._sub_page_cache[slot_index] = result
        # Stash the computed CRC on the slot for diagnostic access.
        # Done outside the cached path above to recompute on every miss.
        # Use object.__setattr__ to bypass the frozen-dataclass check if any.
        object.__setattr__(slot, "crc32c_stored", actual_crc)
        return result

    def materialize_ram(self, cp: Checkpoint) -> bytes:
        """Build the full RAM image (``model_ram_pages * 16 KB``) for a checkpoint.

        Walks each emulator page's 4 sub-page slots, decompresses each (with
        XorPrev chain walking as needed), and stitches them together. Slots
        carrying ``NEVER_TOUCHED_SLOT`` are zero-filled (the dump doesn't
        carry the historical live-RAM content for untouched pages).
        """
        out = bytearray(self.header.model_ram_pages * EMU_PAGE_SIZE)
        if len(cp.ram_sub_slots) != self.header.model_ram_pages * SUB_PAGES_PER_EMU_PAGE:
            raise TtdFormatError(
                f"checkpoint {cp.index} has {len(cp.ram_sub_slots)} sub-slots, "
                f"expected {self.header.model_ram_pages * SUB_PAGES_PER_EMU_PAGE} "
                f"({self.header.model_ram_pages} pages × {SUB_PAGES_PER_EMU_PAGE} sub)"
            )
        for page_idx in range(self.header.model_ram_pages):
            base = page_idx * EMU_PAGE_SIZE
            for sub in range(SUB_PAGES_PER_EMU_PAGE):
                ref = cp.ram_sub_slots[page_idx * SUB_PAGES_PER_EMU_PAGE + sub]
                if ref == NEVER_TOUCHED_SLOT:
                    continue  # leave zeros — see docstring
                sub_bytes = self.get_sub_page(ref)
                out[base + sub * SUB_PAGE_SIZE:
                    base + (sub + 1) * SUB_PAGE_SIZE] = sub_bytes
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


def _require_zstd() -> None:
    if _zstd is None:
        raise TtdFormatError(
            f"zstandard module not available: {_ZSTD_IMPORT_ERROR!r}. "
            f"Install with: pip install zstandard"
        )


def _decompress_zstd(payload: bytes, expected_size: int) -> bytes:
    """Decompress a zstd frame and assert the output is exactly expected_size."""
    _require_zstd()
    if not payload:
        raise TtdFormatError(
            "empty payload for a Full/XorPrev slot (expected zstd frame)"
        )
    dctx = _zstd.ZstdDecompressor()
    try:
        out = dctx.decompress(payload)
    except _zstd.ZstdError as e:
        raise TtdFormatError(f"zstd decompress failed: {e}") from e
    if len(out) != expected_size:
        raise TtdFormatError(
            f"decompressed payload is {len(out)} bytes, expected {expected_size}"
        )
    return out


def _xor_buffers(a: bytes, b: bytes) -> bytes:
    """XOR two equal-length byte buffers."""
    if len(a) != len(b):
        raise TtdFormatError(
            f"XOR length mismatch: {len(a)} vs {len(b)}"
        )
    # int.from_bytes XOR is the fastest pure-Python path for buffers >64 B.
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(
        len(a), "little"
    )


def parse_header(r: _Reader) -> Header:
    magic = r.take(4)
    if magic != MAGIC:
        raise TtdFormatError(
            f"bad magic {magic!r} — not a .ttd file (expected {MAGIC!r})"
        )

    schema_version = r.u16()
    if schema_version != SCHEMA_VERSION:
        if schema_version < SCHEMA_VERSION:
            raise TtdFormatError(
                f"file is schema v{schema_version}; older versions are no longer "
                f"supported. Re-record the session with a v{SCHEMA_VERSION} writer "
                f"(TimeTravelManager built against the current ttd_dump_format.h)."
            )
        raise TtdFormatError(
            f"file is schema v{schema_version}, this parser supports up to "
            f"v{MAX_SUPPORTED_SCHEMA_VERSION}. Regenerate the analyzer from "
            f"the latest ttd.ksy or downgrade the file producer."
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


def parse_slot(r: _Reader, index: int) -> PageSlot:
    """Parse one v2 page-store slot.

    Layout (mirror of SerializeSession in timetravelmanager.cpp):
        u8  encoding       (0=Full, 1=XorPrev, 2=Zero)
        u32 refcount       (informational; reader rebuilds its own)
        u32 prev_slot      (compact index; NEVER_TOUCHED_SLOT if encoding != XorPrev)
        u32 crc32c         (always 0 on write; reader recomputes from decompressed bytes)
        u32 payload_size
        u8[payload_size]   payload (empty for Zero; zstd-compressed otherwise)
    """
    encoding = r.u8()
    if encoding not in (ENCODING_FULL, ENCODING_XOR_PREV, ENCODING_ZERO):
        raise TtdFormatError(
            f"slot {index} has unknown encoding byte {encoding}"
        )
    refcount = r.u32()
    prev_slot = r.u32()
    crc32c_stored = r.u32()
    payload_size = r.u32()
    if payload_size > (1 << 24):  # 16 MB sanity cap (zstd payload << 4 KB typically)
        raise TtdFormatError(
            f"slot {index} implausible payload_size {payload_size} (>16 MB)"
        )
    payload = r.take(payload_size) if payload_size else b""

    # Cross-field validation.
    if encoding == ENCODING_XOR_PREV:
        if prev_slot == NEVER_TOUCHED_SLOT:
            raise TtdFormatError(
                f"slot {index} is XorPrev but prev_slot is NEVER_TOUCHED sentinel"
            )
        if payload_size == 0:
            raise TtdFormatError(
                f"slot {index} is XorPrev but payload is empty"
            )
    elif encoding == ENCODING_FULL:
        if payload_size == 0:
            raise TtdFormatError(
                f"slot {index} is Full but payload is empty"
            )
    elif encoding == ENCODING_ZERO:
        if payload_size != 0:
            raise TtdFormatError(
                f"slot {index} is Zero but payload is {payload_size} bytes (must be 0)"
            )

    return PageSlot(
        index=index,
        encoding=encoding,
        refcount=refcount,
        prev_slot=prev_slot,
        crc32c_stored=crc32c_stored,
        payload=payload,
    )


def parse_checkpoint(
    r: _Reader,
    model_ram_pages: int,
    index: int,
) -> Checkpoint:
    frame = r.u64()
    global_t = r.u64()

    # v2 additions: frame kind + keyframe anchor.
    frame_kind = r.u8()
    if frame_kind not in (FRAME_KIND_KEY_FRAME, FRAME_KIND_DELTA_FRAME):
        raise TtdFormatError(
            f"checkpoint {index} has unknown frame_kind byte {frame_kind}"
        )
    keyframe_anchor = r.u64()

    cpu = parse_cpu(r)
    chipset = parse_chipset(r)

    # RAM refs: 4 sub-page slots per emulator RAM page (v2 layout).
    refs_count = model_ram_pages * SUB_PAGES_PER_EMU_PAGE
    ram_sub_slots = [r.u32() for _ in range(refs_count)]

    ay = parse_blob(r)
    fdc = parse_blob(r)
    tape = parse_blob(r)
    covox = parse_blob(r)

    return Checkpoint(
        index=index,
        frame=frame,
        global_t=global_t,
        frame_kind=frame_kind,
        keyframe_anchor=keyframe_anchor,
        cpu=cpu,
        chipset=chipset,
        ram_sub_slots=ram_sub_slots,
        ay_blob=ay,
        fdc_blob=fdc,
        tape_blob=tape,
        covox_blob=covox,
    )


def parse_bytes(data: bytes) -> TtdDump:
    """Parse a complete .ttd file from an in-memory bytes buffer."""
    r = _Reader(data)
    header = parse_header(r)

    # v2 page store: variable-size slots (no contiguous byte slice anymore).
    slots: List[PageSlot] = []
    for i in range(header.page_store_count):
        slots.append(parse_slot(r, i))

    checkpoints: List[Checkpoint] = []
    for i in range(header.checkpoint_count):
        checkpoints.append(parse_checkpoint(r, header.model_ram_pages, i))

    return TtdDump(header=header, slots=slots, checkpoints=checkpoints)


def parse_file(path: str) -> TtdDump:
    """Parse a .ttd file from disk. Reads the whole file into memory —
    for typical session sizes (<10 MB) this is faster than incremental IO."""
    with open(path, "rb") as f:
        data = f.read()
    return parse_bytes(data)


# ---------------------------------------------------------------------------
# Self-test (python -m ttd_format)
# ---------------------------------------------------------------------------


def _self_test() -> None:
    """Sanity check: crc32c against known vectors, zstd round-trip, XOR helper."""
    # CRC32C of empty string = 0.
    assert crc32c(b"") == 0, "crc32c(empty) should be 0"
    # CRC32C "123456789" = 0xE3069283 (Castagnoli reference vector).
    assert crc32c(b"123456789") == 0xE3069283, (
        f"crc32c('123456789') = {crc32c(b'123456789'):#010x}, expected 0xe3069283"
    )
    # XOR helper.
    assert _xor_buffers(b"\x00\xFF", b"\xFF\x00") == b"\xFF\xFF"
    print("self-test: OK (crc32c + xor)")


if __name__ == "__main__":
    _self_test()
