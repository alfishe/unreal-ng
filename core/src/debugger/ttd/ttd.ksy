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
# v2 breaking changes (Phase 5 codec — see
# docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md):
#   * 4 KB sub-pages (was 16 KB). Each emulator RAM page is split into
#     4 × 4 KB sub-pages, each with its own slot in the page store.
#   * Per-slot encoding discriminator: Full / XorPrev / Zero.
#   * zstd level-1 compression of every non-Zero slot payload.
#   * Per-slot CRC32C integrity field (4 bytes): the CRC of the reconstructed
#     4 KB, stored by the writer and verified by readers.
#   * Per-checkpoint frame_kind (I-frame vs P-frame) + keyframe_anchor.
#   * Checkpoint RAM refs are now 4 * model_ram_pages u32 slot indices.
#   * No v1 backwards compatibility - older files are refused with an error.
#
# The format has not shipped, so it is amended in place rather than versioned.
# header.model_ram_pages is u2; it was u1 until a 4 MB machine (TSConf / ZX-Evo)
# was found to have exactly 256 RAM pages, truncating to 0 and making the
# checkpoint RAM ref table parse as empty. Sessions recorded before that
# amendment do not parse and must be re-recorded.
#
# This file previously declared schema-version 2 while the C++ writer emitted 1.
# They are both 1 now.
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
    session: the complete timeline of per-frame checkpoints plus the
    codec-aware COW (copy-on-write) page store that backs RAM content.

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
    type: page_slot
    repeat: expr
    repeat-expr: header.page_store_count
    doc: |
      Codec-aware COW page store. Each slot is one of:
        * Full     — independent 4 KB snapshot (zstd-1 compressed)
        * XorPrev  — XOR delta against prev_slot's reconstructed bytes
        * Zero     — all-zero 4 KB region (payload omitted)
      Slots are referenced by compact zero-based index from each checkpoint's
      ram_sub_slots vector.
  - id: checkpoints
    type: checkpoint
    repeat: expr
    repeat-expr: header.checkpoint_count
    doc: |
      Per-frame checkpoints, ordered by frame (strictly monotonically
      increasing). The first checkpoint is the session baseline (always a
      KeyFrame); subsequent ones alternate between KeyFrame and DeltaFrame
      per the I/P discriminator in TimeTravelManager::CaptureNow.

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
          Schema version. MUST be 1. Readers refuse unknown versions with a
          clear error.
      - id: flags
        type: u2
        doc: |
          Bitfield. Bit 0 = little-endian (always 1; the writer
          static_asserts little-endian). Bit 1 = a write-journal section
          follows the checkpoint table. Bit 2 = a reverse-search coverage
          index section follows the write journal. Bit 3 = an advisory
          bookmarks section follows the coverage index (TD-4 agent
          bookmarks: u32 count, then per bookmark u64 frame, u32 tInFrame,
          u8 label_len, label bytes — labels are unique, non-empty and at
          most 63 chars). Bit 4 = the write journal holds every write
          of the session (journaling never paused, the ring never
          overwrote a record): only then may a reader answer write/port
          reverse queries from it instead of replaying. Bit 5 = every
          in-frame position (tInFrame, journal globalT) counts T-states at
          the model's top CPU clock (B4); without it a model with a hardware
          turbo is refused, because its positions counted at the clock
          running then and repeat after a mid-frame switch down. Bit 6 = an
          input-journal section follows the bookmarks: u32 count, then per
          event (21 bytes) u64 frame, u32 tInFrame, u8 kind
          (TTDInputKind: 0 Key, 1 MouseMove, 2 MouseButtons, 3 MouseWheel,
          4 MouseCounters, 5 KeyboardReset, 6 GSCommand, 7 GSData, 8 GSNmi,
          9 GSResetCard, 10 GSReset, 11 PcKey - a physical PC key for the PS/2
          controller, key = PcKey, 12 NetEvent - a host network answer for a
          virtual-network socket, 13 NetLinkReset - every host connection
          gone; their network fields live in the bit-10 section, 14 Joystick - a
          Kempston joystick state write, u1 buttonMask = the state byte), u8 key, u8 pressed (0/1), s2 dx, s2 dy,
          u1 buttonMask, s1 wheelSteps, u1 value; ascending time. Bit 7 = an
          external-event section follows: u32 count, then per marker u64
          frame, u32 tInFrame, u8 kind (TTDExternalEventKind; unknown values
          are kept), u8 reason_len (0..63), reason bytes; ascending time.
          The writer always sets bits 6 and 7 (the sections may be empty), so
          a file without them predates saved input: it replays inside a frame
          without the recorded input. Both sections are replay data - a
          reader must not treat them as optional annotations.
          Bit 8 = the port journals follow the external events: every IN
          result, then every OUT, of the main CPU in execution order, each
          with its time and PC (ttd-port-read-journal.md). Each journal: u8
          record_count, u4 block_records (32768), u4 block_count, then per
          block u4 records, u8 base_frame (its first record's frame), u4
          crc32c of the raw block, u4 compressed_size, zstd payload; the raw
          block is five columns of `records` entries - u2 ports, u1 values,
          u2 PCs, u4 frame deltas (from the previous record, 0 for the
          first), u4 T-states (TTD time in the frame: absolute for the first
          record and when the frame changed, else the step from the previous
          record). Every block but the last holds block_records records;
          records are in time order. Then u4 cursor_count (=
          checkpoint_count) and one u8 cursor per checkpoint (the journal
          position at its capture; non-decreasing, at most record_count).
          Written only when the session holds all of its history's I/O; with
          it a replay feeds the CPU the recorded IN values and needs no media
          or host device. Replay data, like bits 6 and 7.

          The flag-gated trailing sections (write journal, coverage index,
          bookmarks, input journal, external events, port journals, network
          inputs) are
          not yet modeled in
          this schema's top-level seq;
          the C++ writer/reader pair (TimeTravelManager::SerializeSession /
          DeserializeSession) is authoritative for their layouts, and a
          reader that stops after `checkpoints` gets the machine states of a
          session, without the accelerators, annotations and replay inputs.
          Bit 9 (0x0200) = the header's last 8 bytes hold the peripheral
          mask (see `peripheral_mask`); older files leave them zero.
          Bit 10 (0x0400) = a network-input section follows the port
          journals (the last section; only when the session has NetEvents):
          u32 count, then per NetEvent input in journal order u32 event_index, u16 socket,
          u8 event (1 Connected, 2 ConnectFailed, 3 Data, 4 PeerClosed,
          5 Reset, 6 Accepted, 7 Datagram, 8 EchoReply, 9 ListenFailed, 10 ModemLines),
          u8 status, u32 addr, u16 port, u32 payload_offset,
          u32 payload_length; then u32 payload_size and the payload bytes
          (what the machine received from the host network).
          Bits 11-15 reserved (must be 0).
      - id: model_id
        type: u1
        doc: eModel enum value (which machine model was active).
      - id: model_ram_pages
        type: u2
        doc: |
          Exclusive RAM page-index bound for the active model. Each checkpoint
          carries model_ram_pages x 4 ram_sub_slots entries (4 sub-pages per
          16 KB emulator page).

          This is a BOUND, not a page count. Models that map RAM at
          non-contiguous page numbers need a bound larger than the number of
          pages they actually own: a 48K machine has 3 pages but addresses them
          as pages 0, 2 and 5, so its bound is 6 and the unused slots inside the
          range are serialized as never-touched refs.

          u2 because a 4 MB machine has exactly 256 pages.
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
      - id: rom_signature
        type: u8
        doc: |
          FNV-1a fingerprint of the whole ROM region the session was recorded
          against. Checkpoints store which ROM PAGE is paged in, never the ROM
          bytes, so replaying against a different ROM set maps the recorded
          page numbers onto different code. On ProfROM machines a plane id only
          means something relative to its image, so a mismatch is a hard error
          rather than a warning. Zero means "unknown" — written when the
          producer had no memory attached — and skips the check.
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
        doc: |
          Number of page_slot records following the header. In v2 this is
          the count of LIVE slots (refcount > 0) — dead slots are not
          serialized. Slots are written in compact remapped order so a
          reader's index N here matches the indices referenced by
          checkpoints' ram_sub_slots.
      - id: checkpoint_count
        type: u4
        doc: Number of checkpoint records following the page store.
      - id: peripheral_mask
        type: u8
        doc: |
          With flags bit 9: bit N set = TTD peripheral id N (TurboSound 0,
          Beta Disk 1, ..., General Sound 5, GS LW 11, NeoGS 12, ...) has a
          state blob in the first checkpoint - the recorded machine's device
          set, readable without walking to the checkpoints. Without bit 9:
          zero (formerly reserved).
  page_slot:
    doc: |
      One entry in the v2 codec page store. Encoded layout per slot:
        u8  encoding       (0=Full, 1=XorPrev, 2=Zero)
        u32 refcount       (informational; reader rebuilds its own)
        u32 prev_slot      (compact index; 0xFFFFFFFF when encoding != XorPrev)
        u32 crc32c         (CRC32C of the reconstructed 4 KB; readers
                            verify it after reconstruction)
        u32 payload_size   (bytes of zstd-compressed payload; 0 for Zero)
        u8[payload_size]   payload
    seq:
      - id: encoding
        type: u1
        doc: |
          0 = Full (independent 4 KB snapshot, zstd-1 compressed payload)
          1 = XorPrev (XOR against prev_slot's reconstructed bytes, zstd-1)
          2 = Zero (all-zero 4 KB region, payload omitted)
      - id: refcount
        type: u4
        doc: |
          Informational only. The reader rebuilds its own refcount model
          by walking checkpoints' ram_sub_slots vectors. The writer
          includes this for diagnostics and round-trip verification.
      - id: prev_slot
        type: u4
        doc: |
          Compact slot index of the slot this one XORs against. Only
          meaningful when encoding == 1 (XorPrev); set to 0xFFFFFFFF
          otherwise. Must point to a slot earlier in the page_store
          sequence (delta chains are strictly forward-only).
      - id: crc32c
        type: u4
        doc: |
          CRC32C (Castagnoli) of the RECONSTRUCTED 4 KB content (after
          decompression and, for XorPrev, applying the delta chain), as
          computed by the page store when the piece was captured; Zero
          pieces carry the CRC of an all-zero 4 KB. Readers recompute it
          after reconstruction and treat a mismatch as an integrity error.
          zstd frames carry no checksum of their own, so this is the only
          check on page payloads.
      - id: payload_size
        type: u4
        doc: |
          Byte length of the following payload. For encoding == Zero this
          is always 0. For Full / XorPrev this is the size of the
          zstd-1-compressed frame (typically 200-2000 bytes for real
          emulator state).
      - id: payload
        size: payload_size
        doc: |
          Raw payload bytes:
            * encoding == Full    → zstd frame, decompresses to 4 KB
            * encoding == XorPrev → zstd frame, decompresses to 4 KB XOR mask
            * encoding == Zero    → omitted (payload_size == 0)

  cpu_state:
    doc: |
      Architectural Z80 state. Mirrors TTDCpuState in ttd_checkpoint.h.
      Excludes host-side fields (memory interface pointers, debugger
      cursors, transient decode scratch).

      The C++ writer emits sizeof(TTDCpuState) == 48 bytes verbatim. The
      alignment gaps are named members on the C++ side rather than implicit
      padding: these objects are copied by member-wise assignment (which does
      not copy padding) and then hashed byte-wise, so unnamed padding would
      leak uninitialized bytes into the hash. Offset 31 is ``reserved0``
      (always zero); offsets 35 and 43 now carry ``boundary`` and
      ``int_acked_in_pulse`` (zero in sessions recorded before them).
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
      - id: reserved0
        type: u1
        doc: |
          Explicit filler aligning memptr (u2) to a 2-byte boundary after
          the 7 u8 fields above (offset 31). Named in the C++ struct so
          member-wise assignment copies it; always zero.
      - id: memptr
        type: u2
        doc: Undocumented MEMPTR / WZ register.
      - id: q
        type: u1
        doc: Undocumented Q register (affects CCF/SCF flag behavior).
      - id: boundary
        type: u1
        doc: |
          Instruction-boundary state (Z80BoundaryEnum: 0 none, 1/2 pending
          DD/FD prefix, 3 INT shadow, 4 LD A,I/R quirk, 5 NMI just
          acknowledged), in the former filler at offset 35. Older files: 0.
      - id: eipos
        type: u2
        doc: Legacy EI position; unused since boundary.
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
      - id: int_acked_in_pulse
        type: u1
        doc: |
          The current INT pulse was acknowledged (machines whose INT ends at
          the acknowledge, e.g. ZX-Evo); in the former filler at offset 43.
          Older files: 0.
      - id: halt_cycle
        type: u4
        doc: Cycle at which HALT became active.

  chipset_state:
    doc: |
      Port-latch subset of EmulatorState + counters. Mirrors
      TTDChipsetState in ttdcheckpoint.h.

      Amendment (in-place, pre-release — sessions recorded before it do not
      parse and must be re-recorded): this structure now carries ONLY the
      standard Spectrum 128K ports, shrinking to 120 bytes (it was 168, or
      184 once the Scorpion ProfROM fields were added). Every
      extended / model-specific latch that used to live here (pXXXX, pDFFD,
      pFDFD, p1FFD, the GMX p7xFD group, Quorum p00 / p80FD, the ATM pFFF7
      array, the Profi palette, Scorpion ProfROM state, video_mode, ...) moved out into
      per-model serializers reached through TTDPeripheralRegistry and is
      carried in `peripheral_blob` entries instead. The common format is
      model-agnostic: it knows nothing about any specific machine.
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
      - id: pbffd
        type: u1
        doc: AY-3-8912 register select.
      - id: pfffd
        type: u1
        doc: AY-3-8912 data.
      - id: pff77
        type: u1
        doc: TurboSound chip select.
      - id: border_attr
        type: u1
      - id: flags
        type: u1
        doc: Runtime execution flags (CF_TRDOS etc.).
      # ---- FDC state (common to Beta Disk models) ----
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
      - id: hw_turbo_ratio
        type: u1
        doc: |
          Model-neutral HARDWARE turbo: the guest-visible CPU clock ratio, 1..8
          (1 = base clock, 2 = 7 MHz, 4 = 14 MHz, 6 = 21 MHz).
          A hardware turbo keeps the 20 ms frame and only multiplies the CPU
          T-states inside it, so the audio path descales by this (the AY, beeper
          and Covox clocks are unchanged). Queued value.
      - id: hw_turbo_ratio_applied
        type: u1
        doc: As composed into current_z80_frequency_multiplier at the frame boundary.
      - id: current_z80_frequency_multiplier
        type: u1
        doc: |
          CPU multiplier relative to the base clock. Zero means the session
          predates this field being captured; readers should treat it as 1.
      - id: next_z80_frequency_multiplier
        type: u1
        doc: Queued multiplier, applied at the next frame start.
      - id: cpu_t_in_frame
        size: 3
        doc: |
          z80.t at the capture, 24-bit little-endian: the CPU's in-frame
          T-state (a frame ends when its last instruction crosses the
          boundary, so a checkpoint's CPU sits a few T-states in - the
          overshoot). Restored with the checkpoint so a replayed frame runs
          with the original instruction timing. Taken from the former
          reserved tail; sessions recorded before it read 0.
      - id: hw_clock_den
        type: u1
        doc: |
          Denominator of the hardware clock ratio, queued (CPU T per base T =
          hw_turbo_ratio / hw_clock_den). 0 means 1: only the Profi in hi-res
          (3 / 5 MHz = 6/7, 10/7) writes a 7. Taken from the reserved tail.
      - id: hw_clock_den_applied
        type: u1
        doc: The denominator as composed into the running clock; 0 means 1.
      - id: reserved
        size: 1
        doc: |
          Explicit tail filler keeping the struct free of implicit padding
          (sizeof == 120; four bytes were taken from it for the CPU clock
          fields above, three for cpu_t_in_frame, two for the clock denominator). The C++ side copies these objects by member-wise
          assignment and hashes them byte-wise, so unnamed padding would
          leak uninitialized bytes into the hash. Always zero.

  registry_blob:
    doc: |
      One peripheral blob: its registry id followed by the opaque serialized
      state. A reader that does not know the id MUST keep the
      bytes rather than drop them — the C++ reader does, so re-serializing a
      session stays byte-identical even on a build lacking that model.
    seq:
      - id: peripheral_id
        type: u1
        doc: |
          PeripheralId enum value (see ttdserializable.h): 0 TurboSound, 1 BetaDisk,
          2 Tape, 3 Covox, 4 TSFM, 5 GeneralSound, 6 ScorpionProfROM, 7 KempstonMouse,
          8 AtmPaging, 9 ProfiPaging (Profi: 16-entry palette, pDFFD latch, front-panel switches),
          10 MoonSound, 11 GeneralSoundLightweight, 12 NeoGS (card state; RAM and flash in the blob until v2 regions),
          13 Plus3Paging, 14 Upd765 (+3 floppy controller),
          15 EvoSdCard (ZX-Evo Z-Controller + SD card protocol state),
          16 TsConfPaging (TSConf machine state), 17 AtaChannel (IDE board: channel, both units, adapter latches; a second channel appended on the Sprinter),
          18 Ds12887 (MC146818 / DS12887 clock: cells, address latch, time base;
          ATM3, Profi, Scorpion SMUC), 19 EvoPs2 (ZX-Evo AVR PS/2 keyboard: the
          16-byte scan code log, its pointers, the parser flags, the modifier
          mask and the held keys), 20 ZxNetUsb (ZXNETUSB card ports, W5300
          registers and socket states, unsent bytes, and journal references
          for the received bytes; the virtual network's guest-side tables -
          netstate.h),
          21 EvoTurboCache (ZX-Evo at 14 MHz: the DRAM's code and data cache words, 6 bytes),
          22 EvoFontRam (ZX-Evo text-mode font RAM, 2 KB, and the glyph byte #0EBD reads: 2050 bytes),
          23 KempstonJoystick (Kempston joystick: u1 version, u1 state byte, active high; carried by
          machines whose decoder answers #1F - ATM3, Scorpion, TS-Conf),
          24 SerialPort (the 16550 on #xxEF - the ZX-Evo AVR's or a ZX-WiFi card's - and its peer:
          netstate::SerialPort; without a peer only the header and the UART registers and FIFOs),
          25 SprinterPld (Sprinter Sp2000 PLD and decoder, sprinter_pld_blob below),
          26 Atm2Kbc (ATM Turbo 2+ keyboard controller: Atm2Kbc::State - the MCS-51 RAM, SFRs, PC, clock,
          interrupt and UART state, the board latches, the PS/2 keyboard model, the controller's time base),
          27 MachineSerialPeer (the peer on a machine serial port that is no 16550 on #xxEF - the ATM Turbo 2+
          keyboard controller's RS-232: netstate::Com, the peer part only),
          28 SprinterVideoRam (u1 version 1, then the 256 KB video RAM; a whole-array blob until TTD v2
          memory regions), 29 Z84C15 (u1 version 2, then the Z84C15's on-chip block, 227 bytes: z84c15_blob below; version 1, 171 bytes with the timer-only CTC, is not restored),
          30 SprinterFastRam (u1 version 1, then the 64 KB fast RAM; whole-array blob until v2 regions),
          31 SprinterInput (sprinter_input_blob below: the AT keyboard's byte stream and the serial mouse),
          32 SprinterCovoxBlaster, 33 SprinterIsa (the ISA slots: u1 version 1, u1 the whole #9FBD latch, u1 x 2
          the card kind fitted in slot 1 / 2 - 0 none, 1 zxbus, 2 ram, 3 ne2000, 4 el3c509b, 5 sprinteresp,
          6 modem, 7 dual16552 - then each card's own bus state, none so far; a session whose kinds differ from
          the fitted cards is refused at load), 34 SprinterPads (reserved for a Sprinter device still to come -
          the extended pads: no blob is written under it yet),
          35 Wd1793Context (u1 version 1, then 112 bytes: the WD1793 command in flight beyond the BetaDisk
          blob - rate-retry search, byte cell, rotational delay, the sector and tracks in use and the transfer
          pointers as (drive, track, offset), the read-track noise seed, up to 4 queued command steps as tags;
          layout in wd1793.cpp SaveTransferContext. Declared by the Sprinter; restored after BetaDisk).
          Blobs are restored in ascending id order.
          Every Sprinter blob (25, 28-31, 35) starts with its version byte; a blob of another version is not
          loaded. The Sprinter also carries 18 Ds12887, 17 AtaChannel (two channels), 1 BetaDisk, 7 KempstonMouse,
          23 KempstonJoystick.
          36 AtmIoBus (ATM Turbo 2+ INTERNAL I/O connector: the #FB bus address latch, 1 byte + 3 reserved),
          37 Atm2IoEsp (the ATM2IOESP card: netstate::SerialPort - its 16550 and peer, as SerialPort),
          38 EvoMouse (the ZX-Evo AVR's PS/2 mouse: version, X, Y, buttons + wheel, plugged in, 3 reserved),
          39 ZiFiLine (the TS AVR's ZiFi UART and its peer: netstate::SerialPort, as SerialPort),
          40 ZiFi (the TS AVR's ZiFi API block, 32 bytes: version 1, api, err, selectZf, imr, isr, zibtr, zitor,
          ribtr, ritor, 5 reserved, u8 x 8 zfLastRx, u8 x 8 rsLastRx).
          41 CdDrive (the IDE board's ATAPI CD drives beyond AtaChannel, cd_drive_blob below; only on a board
          with a CD unit, so the blobs of every other machine are unchanged),
          42 Vdac2Memory (the TS-Conf VDAC2 card's FT812 memory regions whole, in region order: RAM_G, DL0, DL1,
          REG, CMD, SPECIAL, INFLIGHT; until TTD v2 memory regions; only with the card),
          43 Vdac2 (the VDAC2 card: u1 version 1, u1 showing, u1 intAsserted, u1 reserved, u4 edgeCount, u8 frameBase,
          u8 position, u8 remainder, u8 nextEvent, u8 x 16 edges, then the FT812 control state, eve-emu EveSaveState;
          restored after 42),
          44 ProfiXtKbc (the Profi PROFI-XT keyboard controller: ProfiXtKbc::State - u4 version 1, u1 engine (0 firmware,
          1 table), the output latch, WAIT flip-flop, read in progress, reset line, the time base, the MCS-48 (clock, PC,
          A, PSW, 256 bytes RAM of which the 8035 uses 64, port latches and pins, F1, memory bank, interrupt and timer
          state, T0 / T1 / INT), the XT keyboard's wire (queued set-1 bytes, the frame in flight, typematic key, held
          keys) and the table engine's closed positions per PC key; only on a Profi with the controller fitted),
          45 EthernetNics (the frame-level network cards in expansion slots, the Sprinter's NE2000: u1 version 1,
          u1 card count, per card u1 key length, the key ("isa2.eth"), then the board: u1 version 1, u1 variant
          (0 RTL8019AS, 1 UM9003, 2 NE1000), the DP8390 state, the 93C46 EEPROM state, 16 KB packet RAM, 8 bytes
          RTL8019AS page 3 (9346CR, BPAGE, CONFIG1-4, stalled, reserved), 6 bytes station address; only with such a card),
          46 SlotSerial1 (the UART card in expansion slot 1, the Sprinter's SprinterESP: netstate::SerialPort as id
          24 - the TL16C550C and its peer, an ESP module with its AT state, sockets and received bytes by journal
          reference; only with such a card), 47 SlotSerial2 (the same for expansion slot 2),
          48 SlotSerial1B (the second UART of the card in expansion slot 1: SprinterSerial's COM2; the same
          netstate::SerialPort; a Hayes modem peer keeps its command state in the record's ESP bytes, peer kind 6),
          49 SlotSerial2B (the same for expansion slot 2).
          BetaDisk (1) blob: 254 bytes = WD1793 controller 146 + 4 x FDD 27
          (layout in wd1793.cpp, TTDSerializable region). Bytes 143..145 are
          the controller clock policy (0 Fixed1MHz, 1 AutoStepTurbo, 2 Latched),
          the clock now (1 = 1 MHz, 2 = 2 MHz) and the data separator rate
          (0 = 250 kbit/s, 1 = 500 kbit/s). Recordings made before 2026-09-29
          carry the older 251-byte blob, which the reader reports as a size
          mismatch and does not restore.
      - id: state
        type: peripheral_blob

  cd_drive_blob:
    doc: |
      Payload of peripheral 41 CdDrive (ttdcddrive.cpp): the CD audio side and the READ CD
      staging of every unit of the IDE board (channel * 2 + position; 2 units, 4 on the
      Sprinter), zero for a unit that is no CD drive. Little-endian.
    seq:
      - id: version
        type: u1
        doc: 2 (v1, without disc_identity, is not restored)
      - id: cd_units
        type: u1
        doc: bit n set - unit n is a CD drive
      - id: reserved
        size: 6
      - id: units
        type: cd_drive_unit
        repeat: eos
  cd_drive_unit:
    seq:
      - id: head
        type: s8
        doc: |
          The optical head in 1 / 3,500,000 of a 44.1 kHz sample from LBA 0; while playing it is
          relative to the frame start (the head = this + elapsed base T-states x 44100)
      - id: play_start_lba
        type: u4
      - id: end_lba
        type: u4
        doc: the play stops before this frame
      - id: status
        type: u1
        doc: 0 idle (15h), 1 playing (11h), 2 paused (12h), 3 completed (13h, reported once), 4 error (14h, once)
      - id: sotc
        type: u1
        doc: page 0Eh stop on track crossing
      - id: port_select
        size: 4
        doc: page 0Eh channels routed to output ports 0-3 (bit 0 left, bit 1 right)
      - id: port_volume
        size: 4
      - id: flags
        type: u1
        doc: bit 0 - the play ends at a data track (status 14h)
      - id: reserved
        size: 5
      - id: stage_pos
        type: u2
      - id: stage_len
        type: u2
      - id: tray_open
        type: u1
        doc: START STOP UNIT eject opened the tray (no disc for the drive until it loads); 0 before 2026-10-02
      - id: prevent_removal
        type: u1
        doc: PREVENT ALLOW MEDIUM REMOVAL (bit 0)
      - id: stage_reserved
        size: 2
      - id: stage
        size: 2816
        doc: the READ CD sector (with C2 and subchannel fields) still going to the 2048-byte data buffer
      - id: disc_identity
        type: u8
        doc: |
          v2: the inserted disc's ContentId (0 without a disc). A restore onto a disc with
          another identity is reported (warning), not refused

  sprinter_pld_blob:
    doc: |
      Payload of peripheral 25 SprinterPld (ttdsprinter.cpp). Little-endian.
      The accelerator section is empty (size 0) until Sprinter phase S5, which
      bumps the version when it fills it.
    seq:
      - id: version
        type: u1
        doc: 1
      - id: pld_state
        size: 112
        doc: SprinterPldState (sprinterpldstate.h) - cells #C0-#FF, SC, PN, CNF, ROM_RG, ALL_MODE, PORT_Y, RGMOD, HOLD, flags, configuration state, bitstream count and hashes, load watchdog.
      - id: cbl_control
        type: u1
      - id: powered_on
        type: u1
      - id: dcp_opened_frame
        type: s8
      - id: dcp_opened_pc
        type: u2
      - id: int_mode_page
        type: u1
      - id: int_frame_lines
        type: u2
      - id: int_acked_pulse
        type: s8
      - id: int_keyboard_latched
        type: u1
      - id: screen_frame_lines
        type: u2
        doc: Frame height the raster runs with (320 / 312; 0 = no Sprinter screen).
      - id: module_name
        size: 32
        type: strz
        encoding: ASCII
        doc: Active PLD configuration module, looked up by name on load.
      - id: module_state_room
        type: u2
        doc: Room for the largest module state the build registers (Standard: 0).
      - id: module_state_used
        type: u2
        doc: The first module_state_used bytes of module_state are the active module's.
      - id: module_state
        size: module_state_room
      - id: accel_size
        type: u2
      - id: accel_state
        size: accel_size

  z84c15_blob:
    doc: |
      Payload of peripheral 29 Z84C15 after its version byte (Z84C15::SaveState).
    seq:
      - id: system
        size: 8
        doc: SCRP, WCR, MWBR, CSBR, MCR, WDTMR, WDTCR, interrupt priority (#F4).
      - id: wait_generator
        size: 6
        doc: WCR as written, effective WCR, MWBR, M1 cycles left in the power-on window, after-ED flag, active.
      - id: watchdog_running
        type: u1
      - id: watchdog_fired
        type: u1
      - id: watchdog_start
        type: u8
        doc: The clock (the board's units; the Sprinter's 42 MHz crystal ticks) the current watchdog count started at.
      - id: watchdog_clocks_before
        type: u8
        doc: System clocks the watchdog counted before watchdog_start (a clock speed change folds them).
      - id: ctc_clock_num
        type: u4
        doc: One system clock lasts ctc_clock_num / ctc_clock_den clock units (the Sprinter - 12 / multiplier).
      - id: ctc_clock_den
        type: u4
      - id: ctc_vector
        type: u1
      - id: ctc_channels
        size: 32
        repeat: expr
        repeat-expr: 4
        doc: |
          control, time constant, awaiting constant, running, waiting for the trigger edge, count at the
          anchor (0 = 256), anchor u8 (timer - the clock; counter or waiting - the input edges counted),
          zero counts before the anchor u8, zero counts turned into requests u8, IP, IUS.
      - id: sio_channels
        size: 18
        repeat: expr
        repeat-expr: 2
        doc: WR0-WR7, pointer, receive FIFO[3], FIFO count, last data, overrun, Rx-first armed, Rx-first IP, Rx IUS.
      - id: pio_ports
        size: 11
        repeat: expr
        repeat-expr: 2
        doc: mode, direction, output, vector, interrupt control, mask, next, inputs, condition, IP, IUS.

  sprinter_input_blob:
    doc: |
      Payload of peripheral 31 SprinterInput (89 bytes, version 3; version 2 was 88 bytes, without
      the PLD keyboard flags; version 1 was 85 bytes, without the board mouse counters). Times are
      base (3.5 MHz) T-states of the machine's cumulative clock.
    seq:
      - id: version
        type: u1
      - id: kbd_queue
        size: 16
      - id: kbd_head
        type: u1
      - id: kbd_count
        type: u1
      - id: kbd_overflow
        type: u1
      - id: kbd_repeat_key
        type: u1
      - id: kbd_held
        size: 16
      - id: kbd_next_byte_at
        type: u8
      - id: kbd_last_byte_at
        type: u8
      - id: kbd_repeat_at
        type: u8
      - id: mouse_packet
        size: 3
      - id: mouse_sent
        type: u1
      - id: mouse_last_x
        type: u1
      - id: mouse_last_y
        type: u1
      - id: mouse_last_buttons
        type: u1
      - id: mouse_synced
        type: u1
      - id: mouse_next_byte_at
        type: u8
      - id: kbd_overruns
        type: u8
      - id: board_mouse_x
        type: u1
        doc: The board mouse counters both mouse views read (serial packets, the PLD's Kempston view).
      - id: board_mouse_y
        type: u1
      - id: board_mouse_buttons
        type: u1
        doc: Active low, D0 left, D1 right, D2 middle.
      - id: pld_keyboard_flags
        type: u1
        doc: |
          The PLD's keyboard block (KBD.TDF) decoding the wire: bit 0 KB_EXT (last byte #E0), bit 1
          KB_OFF (last byte other than #E0 was #F0), bit 2 KB_CTRL, bit 3 KB_ALT, bit 4 KB_SH.

  peripheral_blob:
    doc: |
      A length-prefixed blob containing a serialized peripheral device
      (AY/TurboSound, FDC, Tape, Covox). The format of the blob's contents
      is device-specific; the .ttd format treats them as opaque bytes.
      Unchanged from v1.
    seq:
      - id: size
        type: u4
      - id: data
        size: size

  checkpoint:
    doc: |
      One complete machine state at a frame boundary. Always captured at
      tInFrame == 0. v2 adds two fields up front: frame_kind (I-frame vs
      P-frame) and keyframe_anchor (the frame index of the I-frame this
      delta chain roots at, for seek-side delta-chain reconstruction).
    seq:
      - id: frame
        type: u8
        doc: Frame index since session start.
      - id: global_t
        type: u8
        doc: Denormalized sort key (== frame at frame boundary in v2).
      - id: frame_kind
        type: u1
        doc: |
          0 = KeyFrame (I-frame): every model RAM page captured as 4
              independent Full sub-page snapshots. Self-sufficient for
              restore.
          1 = DeltaFrame (P-frame): only dirty pages re-captured, encoded
              as XorPrev slots. Restore walks the delta chain back to the
              anchoring KeyFrame.
      - id: keyframe_anchor
        type: u8
        doc: |
          Frame index of the KeyFrame this checkpoint's delta chain roots
          at. For a KeyFrame this equals `frame`; for a DeltaFrame this
          equals the most recent KeyFrame's frame index. Used by the seek
          engine to bound recovery work after a corrupt delta.
      - id: cpu
        type: cpu_state
      - id: chipset
        type: chipset_state
      - id: ram_sub_slots
        type: u4
        repeat: expr
        repeat-expr: _parent.header.model_ram_pages * 4
        doc: |
          Flat list of (4 * model_ram_pages) slot indices, in
          (page, sub) order:
              ram_sub_slots[page * 4 + sub]
          Each entry is either:
            * a slot index into the page_store sequence (0-based, compact)
            * the sentinel 0xFFFFFFFF (NEVER_TOUCHED_SLOT) meaning the
              sub-page was never written during the session up to this
              checkpoint — live RAM content IS the historical content, so
              the restore path skips it.

        doc: AY / TurboSound state.

        doc: WD1793 FDC + FDD state.

        doc: Tape state.
      - id: peripheral_blob_count
        type: u2
        doc: |
          Number of peripheral blobs produced by TTDPeripheralRegistry. Every
          device goes through the registry — the core four (TurboSound, Beta
          Disk, Tape, Covox) and any model-specific ones alike — so a device
          absent on this machine simply has no entry rather than an empty
          fixed slot. Entries are written sorted by peripheral_id so the byte
          image is reproducible; the producer's container is an unordered map.
      - id: peripheral_blobs
        type: registry_blob
        repeat: expr
        repeat-expr: peripheral_blob_count
        doc: Covox 4-channel DAC state.
