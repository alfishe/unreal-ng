# ttd.ksy — Canonical Kaitai Struct schema for the Unreal-NG TTD session dump.
#
# Single source of truth for the .ttd binary format. The C++ writer in
# timetravelmanager.cpp conforms to this schema; conformance is verified by
# cross-language round-trip tests (C++ writes, Kaitai-generated Python parser
# reads). Third parties can regenerate parsers for any Kaitai-supported
# language:
#
#     kaitai-struct-compiler ttd.ksy -t python -o <outdir>
#     kaitai-struct-compiler ttd.ksy -t cpp    -o <outdir>
#     kaitai-struct-compiler ttd.ksy -t rust   -o <outdir>
#     # ... or Java, Go, JavaScript, Ruby, Lua, Nim, PHP, Swift, C#
#
# Compatibility contract:
#   * Schema version lives in two places (must always agree):
#       - meta.schema-version below
#       - schema_version field in every .ttd file header
#   * A reader that encounters schema_version > N MUST refuse to parse and
#     surface a clear error.
#   * Evolution within a major version is ADDITIVE ONLY (new optional blobs
#     appended at the end of a checkpoint, new flag bits — never reorder or
#     shrink existing fields).
#   * Breaking changes bump schema-version, git-tag this file as
#     `ttd-schema-vN`, and update both version fields atomically in one commit.
#
# License: MIT — re-publishable, vendorable, forkable.

meta:
  id: ttd_dump
  title: Unreal-NG TTD Session Dump
  file-extension: ttd
  endian: le
  application: unreal-ng
  license: MIT
  ks-version: 0.10
  schema-version: 1
  doc: |
    A binary serialization of an Unreal-NG Time-Travel Debugging recording
    session: the complete timeline of per-frame checkpoints plus the COW
    (copy-on-write) page store that backs RAM content.

    Producers (any of):
      * TimeTravelManager::SerializeSession()  (core C++ API)
      * core/tests/debugger/ttd/ttd_dump_format_test.cpp  (round-trip)
      * `automation-cli ttd dump --out session.ttd`  (CLI)
      * `POST /api/v1/emulator/{id}/ttd/dump`  (WebAPI wrapper, optional)

    Consumers:
      * TimeTravelManager::DeserializeSession()  (round-trip into a fresh
        manager; used by replay/restore tools and tests)
      * tools/verification/ttd-analyzer/  (Python: integrity checks,
        anomaly detection, framebuffer rendering)
      * Any third-party tool that generates a parser from this .ksy

# ---------------------------------------------------------------------------
# Top-level file layout
# ---------------------------------------------------------------------------
seq:
  - id: header
    type: header
    doc: Fixed-size file header (magic, version, model metadata).
  - id: page_store
    type: page_store_entry
    repeat: expr
    repeat-expr: header.page_store_count
    doc: |
      Deduplicated 16 KB RAM-page pool. Each checkpoint references one slot
      per model-RAM-page via its ram_page_refs vector (slot index or
      NEVER_TOUCHED = 0xFFFFFFFF).
  - id: checkpoints
    type: checkpoint
    repeat: expr
    repeat-expr: header.checkpoint_count
    doc: |
      Per-frame checkpoints, ordered by frame (strictly monotonically
      increasing). The first checkpoint is the session baseline captured
      at StartRecording; subsequent ones are appended at each OnFrameBoundary.

# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------
types:
  header:
    doc: |
      File header. Fixed layout so readers can read just the header to
      decide whether to proceed.
    seq:
      - id: magic
        contents: "TTDD"
        doc: 4-byte magic identifier ("TTD Dump").
      - id: schema_version
        type: u2
        doc: |
          Schema version. MUST match meta.schema-version in the .ksy the
          producer was built against. Readers refuse unknown future versions.
      - id: flags
        type: u2
        doc: |
          Bitfield. Bit 0 = little-endian (always 1 in v1). Bits 1-15
          reserved for future use (must be 0 in v1).
      - id: model_id
        type: u1
        doc: eModel enum value (which machine model was active).
      - id: model_ram_pages
        type: u1
        doc: |
          Number of physical RAM pages on the active model. Each checkpoint
          has exactly this many ram_page_refs entries.
      - id: cpu_state_size
        type: u2
        doc: |
          Byte size of the TTDCpuState struct as written by this producer.
          Lets a reader detect struct-layout drift between writer build and
          reader build of the C++ implementation. For .ksy-generated readers
          this field is informational only.
      - id: chipset_state_size
        type: u2
        doc: |
          Byte size of the TTDChipsetState struct as written by this producer.
          Same drift-detection purpose as cpu_state_size.
      - id: captured_at_unix_ms
        type: u8
        doc: Wall-clock capture time (milliseconds since Unix epoch).
      - id: emulator_id_len
        type: u1
      - id: emulator_id
        size: emulator_id_len
        type: str
        encoding: UTF-8
        doc: Symbolic instance identifier of the source emulator.
      - id: session_state
        type: u1
        doc: |
          TTDSessionState enum value at capture time
          (0 = idle, 1 = recording, 2 = detached).
      - id: session_start_frame
        type: u8
        doc: Frame counter at session start (typically 0).
      - id: session_end_frame
        type: u8
        doc: Last captured frame counter.
      - id: page_store_count
        type: u4
        doc: Number of page_store_entry records following the header.
      - id: checkpoint_count
        type: u4
        doc: Number of checkpoint records following the page store.
      - id: reserved
        size: 8
        doc: Reserved for future additive fields (must be zero in v1).

  page_store_entry:
    doc: |
      One 16 KB page of RAM content. No metadata — the slot index is the
      reader's only handle. Checkpoints reference slots by zero-based index
      into the page_store sequence.
    seq:
      - id: data
        size: 16384
        doc: Raw 16384-byte RAM page content.

  cpu_state:
    doc: |
      Architectural Z80 state. Mirrors TTDCpuState in ttd_checkpoint.h.
      Excludes host-side fields (memory interface pointers, debugger
      cursors, transient decode scratch).

      The C++ writer emits sizeof(TTDCpuState) bytes verbatim, which on
      every supported compiler (GCC/Clang/MSVC, x86_64/arm64) includes 3
      padding bytes at structurally-imposed offsets — the C++ struct uses
      natural alignment, so a u16 following an odd count of u8 fields gets
      a 1-byte pad inserted before it (and likewise for u32 after u8).
      Kaitai's generated parsers read fields sequentially with no implicit
      padding, so the pad bytes are declared explicitly below as ``_pad_*``
      fields. The hand-written Python parser in ttd-analyzer skips them
      with the same comment markers.
    seq:
      - id: pc
        type: u2
      - id: sp
        type: u2
      - id: af
        type: u2
      - id: bc
        type: u2
      - id: de
        type: u2
      - id: hl
        type: u2
      - id: ix
        type: u2
      - id: iy
        type: u2
      - id: alt_af
        type: u2
      - id: alt_bc
        type: u2
      - id: alt_de
        type: u2
      - id: alt_hl
        type: u2
      - id: i
        type: u1
      - id: r_low
        type: u1
      - id: r_hi
        type: u1
      - id: iff1
        type: u1
      - id: iff2
        type: u1
      - id: im
        type: u1
      - id: halted
        type: u1
      - id: _pad_before_memptr
        type: u1
        doc: |
          Compiler-inserted padding to align memptr (u2) to a 2-byte
          boundary after the 7 u8 fields above (offset 31 in the struct).
      - id: memptr
        type: u2
        doc: Undocumented MEMPTR / WZ register.
      - id: q
        type: u1
        doc: Undocumented Q register (affects CCF/SCF flag behavior).
      - id: _pad_before_eipos
        type: u1
        doc: |
          Padding to align eipos (u2) after the single q u8 (offset 35).
      - id: eipos
        type: u2
        doc: EI instruction position (post-EI interrupt latency).
      - id: haltpos
        type: u2
        doc: HALT instruction position.
      - id: nmi_in_progress
        type: u1
      - id: int_pending
        type: u1
        doc: INT line state (latched).
      - id: int_gate
        type: u1
        doc: External interrupts gate (1 = enabled).
      - id: _pad_before_halt_cycle
        type: u1
        doc: |
          Padding to align halt_cycle (u4) to a 4-byte boundary after
          3 u8 fields (offset 43).
      - id: halt_cycle
        type: u4
        doc: Cycle at which HALT became active.

  chipset_state:
    doc: |
      Port-latch subset of EmulatorState + counters. Mirrors
      TTDChipsetState in ttd_checkpoint.h. All fields are present
      unconditionally (no conditional layout) so the schema stays simple;
      model-irrelevant fields read as 0.
    seq:
      # ---- Counters ----
      - id: t_states
        type: u8
      - id: frame_counter
        type: u8
      # ---- Standard Spectrum 128K port latches ----
      - id: p7ffd
        type: u1
        doc: 128K banking / screen / ROM select. Bit 3 selects screen bank.
      - id: pfe
        type: u1
        doc: Beeper / EAR / border color / mic. Bits 0-2 = border color.
      - id: peff7
        type: u1
        doc: Beta Disk interface control.
      - id: pxxxx
        type: u1
      - id: pbffd
        type: u1
        doc: AY-3-8912 register select.
      - id: pfffd
        type: u1
        doc: AY-3-8912 data.
      - id: pdffd
        type: u1
        doc: Pentagon 512K / Profi extension banking.
      - id: pfdfd
        type: u1
        doc: Profi extension banking.
      - id: p1ffd
        type: u1
        doc: +3 / Pentagon 1024 banking.
      - id: pff77
        type: u1
        doc: TurboSound chip select.
      - id: border_attr
        type: u1
      - id: flags
        type: u1
        doc: Runtime execution flags (CF_TRDOS etc.).
      # ---- Extended port latches (populated only on relevant models) ----
      - id: p7efd
        type: u1
      - id: p78fd
        type: u1
      - id: p7afd
        type: u1
      - id: p7cfd
        type: u1
      - id: gmx_config
        type: u1
      - id: gmx_magic_shift
        type: u1
      - id: p00
        type: u1
      - id: p80fd
        type: u1
      - id: afe
        type: u1
      - id: afb
        type: u1
      - id: aff77
        type: u1
      - id: active_ay
        type: u1
      - id: pbd
        type: u1
      - id: pbe
        type: u1
      - id: pbf
        type: u1
      - id: pffba
        type: u1
      - id: p7fba
        type: u1
      - id: p0f
        type: u1
      - id: p1f
        type: u1
      - id: p4f
        type: u1
      - id: p5f
        type: u1
      - id: plsy256
        type: u1
      - id: wd_shadow
        size: 4
        doc: 2F, 4F, 6F, 8F WD1793 shadow registers.
      # ---- Video / palette ----
      - id: comp_pal
        size: 16
        doc: Hardware palette registers.
      - id: ulaplus_mode
        type: u1
      - id: ulaplus_reg
        type: u1
      - id: ulaplus_cram
        size: 64
        doc: ULAplus palette entries.
      # ---- ATM 7.10 / ATM3 memory mapping ----
      - id: pfff7
        size: 32
        doc: 8 x uint32 little-endian.

  peripheral_blob:
    doc: |
      A length-prefixed blob containing a serialized peripheral device
      (AY/TurboSound, FDC, Tape, Covox). The format of the blob's contents
      is device-specific; the .ttd format treats them as opaque bytes.
    seq:
      - id: size
        type: u4
      - id: data
        size: size

  checkpoint:
    doc: |
      One complete, self-sufficient machine state at a frame boundary.
      Always captured at tInFrame == 0.
    seq:
      - id: frame
        type: u8
        doc: Frame index since session start.
      - id: global_t
        type: u8
        doc: Denormalized sort key (== frame at frame boundary in v1).
      - id: cpu
        type: cpu_state
      - id: chipset
        type: chipset_state
      - id: ram_page_refs
        type: u4
        repeat: expr
        repeat-expr: _parent.header.model_ram_pages
        doc: |
          One slot index per model-RAM-page. The sentinel 0xFFFFFFFF means
          NEVER_TOUCHED — the live RAM content for that page is the
          historical content, so the restore path skips it.
      - id: ay
        type: peripheral_blob
        doc: AY / TurboSound state.
      - id: fdc
        type: peripheral_blob
        doc: WD1793 FDC + FDD state.
      - id: tape
        type: peripheral_blob
        doc: Tape state.
      - id: covox
        type: peripheral_blob
        doc: Covox 4-channel DAC state.
