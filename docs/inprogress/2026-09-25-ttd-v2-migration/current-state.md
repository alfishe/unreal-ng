# TTD current state (master `d8be186d`, 2026-09-25)

What the time-travel debugger (TTD) actually does today, measured and read from
the code. Written as the baseline for the v1 → v2 migration
([README.md](README.md)). Where a design document says something different,
the code wins and the difference is listed in §9.

Abbreviation: **TTM.cpp** = `core/src/debugger/ttd/timetravelmanager.cpp`.
Items marked **(suspected)** were found by reading the code and have not been
reproduced by a test yet.

---

## 1. One-paragraph summary

TTD records one checkpoint per emulated frame, entirely in process memory. RAM
is stored as 4 KB pieces, each either a full compressed copy, a compressed XOR
against the previous version of the same piece, or "all zero". CPU registers,
port latches and every device's state are stored whole in every checkpoint.
Seeking restores the nearest checkpoint and silently re-runs the emulator up to
the exact T-state. Nothing bounds memory use except the 64 MB write journal, and
nothing is written to disk until the user explicitly saves a `.ttd` file. Of
the "v2" design, the page-granular codec has landed; memory bounds, disk
streaming, device-owned memory regions and the chunked file container have not.

## 2. Components

| File (in `core/src/debugger/ttd/`) | Role |
|---|---|
| `timetravelmanager.{h,cpp}` | Everything user-visible: record, seek, reverse, save/load, coverage queries, DeZog frame cache (1.6k + 4.8k lines) |
| `ttdcheckpoint.{h,cpp}` | Checkpoint, CPU state (48 B), chipset state (120 B), page references |
| `ttdcodecpagestore.{h,cpp}` | Reference-counted store of 4 KB pieces ("slots" in the code): Full / XorPrev / Zero, zstd level 1, CRC32C per piece |
| `ttddirtytracker.{h,cpp}` | Which 16 KB RAM pages were written this frame (256-bit atomic bitmap) |
| `ttdperipheralregistry.{h,cpp}`, `ttdserializable.h` | Device state registry and the `PeripheralId` table |
| `ttdwritejournal.{h,cpp}` | Every memory/port write (12 B record), 64 MB ring; columnar zstd blocks on save |
| `ttdcoverageindex.{h,cpp}` | Per-frame sets of executed / written / read addresses, used to skip frames in reverse search |
| `ttdinputjournal.*`, `ttdexternalevents.*` | Keyboard/mouse events and "replay barrier" markers (tape/disk) — **memory only, not saved** |
| `ttdbookmarks.*` | User labels (saved) |
| `ttddumpformat.h`, `ttd.ksy` | File format constants and the Kaitai schema (partly stale) |
| `atm/ttdatmpaging.*`, `scorpion/ttdscorpionprofrom.*` | Model-specific state serializers |

## 3. Recording

Every frame end, `MainLoop::OnFrameEnd` calls `OnFrameBoundary`, which captures:

1. **CPU and chipset**: stored whole (48 + 120 B). The chipset struct holds the
   T-state and frame counters, the standard port latches (`7FFD`, `FE`, `EFF7`,
   AY select/data, `FF77`), border, WD shadow, palette, ULAplus, the turbo
   state and the in-frame T-state overshoot (`cpu_t_in_frame`, added
   2026-09-25).
2. **Device blobs**: each registered device writes its state; the registry adds
   a 12-byte header and zstd-compresses it when that helps. This allocates and
   compresses every frame even when nothing changed.
3. **RAM**, via the dirty tracker (16 KB granularity, fed only by the *debug*
   memory write path — so recording forces `kDebugMode` on):
   - clean page → reuse the previous checkpoint's 4 pieces (reference count +1);
   - dirty page → each 4 KB piece XOR'd against `_prevPageCache` and compressed
     both as XOR and as full; the smaller one is kept; an unchanged piece reuses
     its stored piece;
   - **key frame every 50 frames** → every non-zero page is stored again in full
     (TTM.cpp:801-822). This bounds the XOR chain length (≤ 49) but costs
     O(installed RAM) every 50 frames;
   - after capture, `_prevPageCache` is refreshed by copying **all** model RAM
     (TTM.cpp:928-954): 128 KB per frame on a 128K machine, 4 MB on ZX-Evo.
4. **Write journal** (optional, "development mode"): one 12-byte record per
   memory or port write. Measured at ~2 300 records per frame on a demo.
5. **Coverage index**: sorted address sets per frame, 64-frame zstd blocks.

A checkpoint also carries a **dense page reference table**: 4 × u32 per 16 KB
page, every frame. 128 B/frame on Pentagon 128, but 4 KB/frame (~720 MB/hour)
on a 256-page machine.

## 4. Seek and reverse

- **Seek**: binary search for the target frame's checkpoint (there is one per
  frame) → restore CPU, chipset, in-frame T-state, devices, paging, all RAM
  (every 4 KB piece decoded through its XOR chain) → silent replay to the target
  T-state with the recorded input injected.
- **Find last access**: write/port queries scan the write journal backwards;
  read/execute queries (or a wrapped journal) walk frames backwards, skipping
  frames the coverage index rules out, restoring and re-running the rest.
- **Reverse step / continue**: built from the above (one replay pass collecting
  instruction starts for N > 4).
- **Resume recording from the past**: seek, cut everything after that point,
  continue recording.

## 5. The `.ttd` file

One pass, written whole on save, read whole on load. Header `TTDD`,
`schema_version = 1` ("amended in place" — the format is treated as
unreleased, so any change re-records the fixtures instead of bumping the
version). Then: pieces → checkpoints → optional write journal → optional
coverage index → optional bookmarks (flag bits 1-3).

Compatibility checks at load: magic, exact schema version, little-endian flag,
model id, ROM signature, CPU/chipset struct sizes (48/120). **Not checked**:
emulator configuration that exact replay depends on (T-states per frame, CPU
clock, audio core rate, decimator mode), whether the page table matches the
live model, checkpoint ordering, reserved bytes, checksums.

## 6. Device state (registry)

| Id | Device | State | Registered |
|---|---|---|---|
| 0 | TurboSound (legacy 2×AY) | 925 B | every model (TurboSound slot) |
| 1 | BetaDisk (WD1793 + 4 drives) | 251 B, disk *contents* not captured | every model |
| 2 | Tape | 42 B, position only | every model |
| 3 | Covox | 4 B | every model |
| 4 | TurboSound FM | 2000 B (internal layout v4) | every model (TurboSound slot) |
| 5 | GeneralSound | reserved, no serializer on master | — |
| 6 | Scorpion paging + ProfROM | 8 B | SCORPION, PROFSCORP |
| 7 | Kempston mouse | 8 B | every model |
| 8 | ATM paging | 44 B | ATM710, ATM3 |

- Model-specific state is declared by the port decoder
  (`GetTTDModelStateIds` / `CreateTTDSerializers`); a declaration without a
  serializer refuses recording. Sound devices register directly, outside that
  guard.
- There is no per-blob version. TSFM checks its own layout number only with a
  debug-build `assert`.
- The restore report (missing / wrong-size / unclaimed blobs) is computed and
  **ignored** (TTM.cpp:1164): a device with no usable blob silently keeps its
  live state.

## 7. What is not captured

| Gap | Effect |
|---|---|
| **RAM page 255 on 4 MB machines** (u8 page cache uses `0xFF` as "not RAM") | That page is never marked dirty or journaled; changes appear only at the next key frame. Coverage lumps it with ROM |
| Keyboard matrix, joystick | Replay starts from the live host keyboard state |
| Input journal, external-event markers — in the **file** | A loaded session replays inside a frame without the recorded keys and crosses tape/disk barriers silently |
| Disk and tape image contents | By design: loads invalidate the session; disk writes are barriers (lost on save, see above) |
| CMOS/NVRAM (ATM), Profi RTC (branch) | Deliberate so far; RTC reads host time, so replay is not deterministic |
| TS-Conf, cache/misc pages | Not supported |

## 8. Costs (Apple Silicon, Release, Pentagon + TSFM)

| Measurement | Value |
|---|---|
| Frame, TTD off / hooks only / gaming / development | 1769 / 1827 / 2255 / 2270 µs (development ≈ +28%) |
| `OnFrameBoundary` by dirty 16 KB pages: 0 / 1 / 4 / 16 / 64 | 14 / 84 / 298 / 567 / 661 µs |
| Find-last on a loaded 300-frame session, without / with coverage index | 695 ms / 0.39 ms |
| Reverse-continue, without / with index | 1097 ms / 8.4 ms |
| Reverse step N = 1 / 64 | 3.3 / 45 ms |
| Recorded fixtures (300 frames): pages / device blobs / refs / fixed | 215 B–2.3 KB / 309–646 B / 128 B / 195 B per frame |
| Whole files | 0.4–3.5 MB per 6 s ≈ 0.3–2.1 GB/hour; the write journal dominates |

Note how the **device blobs cost more than RAM on idle workloads** — they are
stored whole every frame.

## 9. Integrity

Bit-flip experiment (2026-09-25, `testdata/ttd/tsfm_tech_support.ttd`, 150
random single-bit flips per section; "caught" = parse or `validate` error):

| Section | Caught | Silent |
|---|---|---|
| header | 55 | 95 |
| pages | 22 | 128 |
| checkpoints | 65 | 85 |
| write journal | 46 | 104 |
| coverage | 0 | 150 |

Why:

- Only 4 KB page pieces carry a CRC32C, and the C++ side checks it **lazily, at
  restore**: a mismatch fills that 4 KB with zeros, logs a warning, and the seek
  still reports success (TTM.cpp:1258-1264). At the time of the
  experiment the Python analyzer computed the CRC but never compared it, so
  most page flips passed `validate` (fixed 2026-09-25: 300 of 300 payload flips
  are now caught).
- CPU/chipset bytes, device blobs, page references, the journal, the coverage
  index and most header fields have no checksum at all. A flip there restores a
  silently wrong machine, or makes reverse search skip real matches.
- zstd frames are written without zstd's own checksum.
- Corrupt u32 sizes in the journal/coverage loaders are not bounded: a flip can
  request a 4 GB allocation (`bad_alloc`), which WebAPI and Qt do not catch.

## 10. Suspected bugs and robustness gaps

| # | Issue | Status |
|---|---|---|
| B1 | Stale `_prevPageCache` after *Resume recording from here* at or after the last key frame: the first delta is XOR'd against the wrong base → zero-filled or silently old pages | suspected; `TTD_Corpus_Test` replay does not compare RAM, so it would not catch it |
| B2 | `DeserializeSession` does not clear the write journal: a loaded file without a journal answers find-last from the previous live recording | suspected |
| B3 | Empty write journal → write/port find-last returns "no match" without falling back to replay (TTM.cpp:3448) | suspected |
| B4 | Hardware turbo: journal timestamps and the replay clamp assume `z80.t < frame length` | suspected |
| B5 | A failed load has already cleared the live session and leaves a partial timeline with page reference counts off by one | read in code |
| B6 | Page-255 gap (§7) | read in code, confirmed by inspection |
| B7 | `sessionHeapBytes` ("Memory MB" in Qt and WebAPI) excludes page payloads, the 64 MB journal, coverage and caches | read in code |
| B8 | Python analyzer: does not know flag bit 3 (bookmarks) → reports `trailing_bytes` on files with bookmarks. (The missing CRC comparison and the false "writer stores 0" comments were fixed 2026-09-25: 300/300 payload flips now caught) | read in code |
| B9 | Turning TTD or debug mode off mid-recording silently corrupts history (perf review F2) | from [core-perf review](../2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md) |

Stale documentation in code (the "CRC always 0 on write" comments were fixed
2026-09-25; the PoC reader `tools/poc/010-ttd-gui` still carries one): the
pre-codec format comment block
(TTM.cpp:2260-2296), `_forceNextKeyFrame` "set by reset/load", "no heap
allocation" in `ttdserializable.h`, "64 MB budget" in the page store header.
Dead code: `MaterializedRamCache`, the two journal offsets in `TTDCheckpoint`,
the `DebuggerEdit`/`HardwareReset` event kinds.

## 11. Who depends on the current design

A format or architecture change touches all of these together:

- **C++**: `TimeTravelManager` public API, `TTDSessionInfo`, `PeripheralId`,
  the port decoder TTD hooks, `ttd::dump` constants, `TTDWriteRecord`.
- **WebAPI**: 21 routes under `/api/v1/emulator/{id}/ttd/` (status JSON keys are
  a contract). **MCP** `time_travel`. **CLI** `ttd …`. **Python** 25 `ttd_*`
  bindings. **Lua** the same plus journal toggles.
- **Debuggers**: DeZog (history, frame cache, find-last, reverse-continue), GDB
  reverse support.
- **Qt**: `ttdwidget.cpp` (record, load/export, scrubber, "Rec From Here").
- **Tooling**: `tools/verification/ttd-analyzer/` (hand-written parser,
  validator, fixture recorder), `ttd.ksy`, `testdata/ttd/` fixtures gated by
  `TTD_Corpus_Test`, `.recipe/analysis/ttd-*.md`, 49 test files in
  `core/tests/debugger/ttd/`, the benchmarks in `core/benchmarks/debugger/ttd/`.
- **PoCs**: `tools/poc/010-ttd-gui` (its reader expects schema 3 — already
  broken), `tools/poc/011-ttd-v2-capture-analysis`.
