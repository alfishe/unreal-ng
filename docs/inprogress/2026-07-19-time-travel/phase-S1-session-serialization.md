# Phase S1 — Session Serialization (.ttd) + Python Analyzer

Started: 2026-07-23
Completed: 2026-07-23
Reference: This is a side-phase that supports every other phase by giving TTD
sessions a portable, versioned binary representation with a published schema.
Parent TDD: extends §10.4 (automation) and §15.1 (test corpus).

Cross-cutting decisions applied: see [`decisions.md`](decisions.md), in
particular the new **Session serialization** section added in this phase.

## Outcome

A TTD recording can now be written to a self-contained `.ttd` binary file
and read back byte-identically, with a published canonical schema that
any third party can generate parsers for. A standalone Python analyzer
(`tools/verification/ttd-analyzer/`) consumes the same format, with no
C++ build dependency, and provides integrity checks, anomaly detection,
markdown reporting, and screen rendering from the captured state.

**7 round-trip tests green** (`TTD_Dump_Format_Test`, 254 total TTD
tests still green). Python analyzer validated against a 559 KB C++-generated
fixture; all subcommands working.

## Why this is a side-phase (Phase S1)

Phase 1 (recorder) and Phase 2 (seek engine) deal with the in-memory TTD
state machine. Phase 3 is the timeline UI. None of those depend on a
portable on-disk representation — capture/seek operate on live state.

The `.ttd` serialization is orthogonal infrastructure:

- Phase 1, 2, 3, 4 don't change behaviour whether `.ttd` exists or not.
- The `.ttd` format unblocks **offline analysis** of recordings captured
  by any producer (core test, automation CLI, WebAPI wrapper) without
  needing the emulator running.
- It's the substrate for the user-reported bug triage flow:
  "capture a session → save → analyze offline → find what's wrong".

Numbering it `S1` ("S" for serialization / side-phase) avoids collision
with the linear phase progression while making clear it's not optional
decoration — it's a foundational cross-cutting capability.

## Items delivered

| # | Item | Files | Tests |
|---|------|-------|-------|
| 1 | Canonical Kaitai Struct schema (`ttd.ksy`) + C++ constants header | `core/src/debugger/ttd/ttd.ksy` (411 lines), `core/src/debugger/ttd/ttd_dump_format.h` (50 lines) | — |
| 2 | `SerializeSession` / `DeserializeSession` on `TimeTravelManager` | `core/src/debugger/ttd/timetravelmanager.{h,cpp}` (+~570 lines impl) | `TTD_Dump_Format_Test` (7 cases) |
| 3 | `CaptureRestoreSelfTest` — capture→restore hash-comparison diagnostic | same as above | covered by Item 2 tests |
| 4 | Round-trip test including the disabled fixture-writer for Python conformance | `core/tests/debugger/ttd/ttd_dump_format_test.cpp` (~400 lines) | 7 cases green |
| 5 | Python analyzer — pure-stdlib parser mirroring `ttd.ksy` | `tools/verification/ttd-analyzer/src/ttd_format.py` (480 lines) | validated against C++ fixture |
| 6 | Python analyzer — integrity checks | `tools/verification/ttd-analyzer/src/integrity_check.py` (162 lines) | validated against fixture |
| 7 | Python analyzer — heuristic anomaly detector | `tools/verification/ttd-analyzer/src/anomaly_detector.py` (299 lines) | validated against fixture |
| 8 | Python analyzer — ZX Spectrum framebuffer renderer | `tools/verification/ttd-analyzer/src/framebuffer_renderer.py` (281 lines) | produces 320×256 PNG/PPM from fixture |
| 9 | Python analyzer — markdown timeline report | `tools/verification/ttd-analyzer/src/timeline_report.py` (251 lines) | produces report on fixture |
| 10 | Python analyzer — CLI entry + subcommands | `tools/verification/ttd-analyzer/src/main.py` (427 lines) | 6 subcommands validated |
| 11 | Python analyzer — `requirements.txt`, `run.sh`, `testdata/fixture.ttd` | supporting files | — |

## Phase-specific design decisions

### 1. The Kaitai `.ksy` is the single source of truth

`ttd.ksy` is canonical. The C++ writer (`SerializeSession`), the C++ reader
(`DeserializeSession`), and the Python parser (`ttd_format.py`) all conform
to it. When the format changes:

1. Edit `ttd.ksy` and bump `meta.schema-version` (additive-only within a
   major version).
2. Edit the matching C++ constants in `ttd_dump_format.h`.
3. Edit the writer/reader in `timetravelmanager.cpp`.
4. Edit the Python parser in `ttd_format.py`.
5. Regenerate the fixture (run the disabled C++ test) and re-run the
   Python analyzer against it.

Both version markers must always agree. A reader refuses a file with an
unknown future schema version with a clear error message.

### 2. Why Kaitai Struct and not Protobuf / Msgpack / DFDL / FlatBuffers

Surveyed before picking (decision recorded in `decisions.md`):

- **Kaitai Struct** — purpose-built for binary formats in the retro/emulator
  community (NES, SNES, GBA, DS formats all use it). Schema is YAML, MIT-
  licensed, parsers generate for Python/C++/Java/JS/Go/Rust. Reads are
  mmap-friendly POD. **Winner.**
- **DFDL** — enterprise XML-oriented, overkill, weak Python story.
- **Protobuf** — RPC-oriented; every field requires a tag; not mmap-friendly.
- **Msgpack** — schemaless; no canonical published schema to version.
- **FlatBuffers** — close second but loses mmap-friendly POD reads for
  variable-length records, and the schema language is heavier.

Kaitai gives us third-party parsers as a one-liner:

```
kaitai-struct-compiler core/src/debugger/ttd/ttd.ksy -t python -o ...
```

### 3. Universal capability — not behind any single API

The original user prompt asked "why do we need dump endpoint? what is the
most universal strategy suitable both for core standalone and for
automation/webapi wrapped mode?". Answer: the capability lives in
`core/src/debugger/ttd/`, exposed by `TimeTravelManager::SerializeSession`
taking a `std::ostream&`. Three producers all call the same code:

- **Core tests** — pass a `std::ostringstream` for in-process round-trip.
- **Automation CLI** — will pass a `std::ofstream` for `ttd dump <path>`.
- **WebAPI wrapper** — will pass a `std::ostringstream` and return the
  bytes as a download (thin handler around the universal capability).

No producer-specific serialization logic. Same bytes regardless of caller.

### 4. POD-with-padding handling

The C++ `TTDCpuState` / `TTDChipsetState` structs are POD with natural
alignment — the writer emits `sizeof(Struct)` bytes verbatim, padding
included. This is **deliberate**: it makes the C++ writer trivial (one
`out.write(&cp.cpu, sizeof(cp.cpu))` call) and the C++ reader trivial
likewise. The trade-off is that schema-driven parsers (Kaitai-generated
*and* the hand-written Python parser) must explicitly declare the padding
bytes.

Padding layout for `TTDCpuState` (size = 48 bytes on every supported
compiler):

| Offset | Field            | Type | Comment                          |
|--------|------------------|------|----------------------------------|
| 0-23   | pc..alt_hl       | u16  | 12 × 16-bit registers            |
| 24-30  | i..halted        | u8   | 7 × 8-bit registers              |
| 31     | _pad_before_memptr | u1 | aligns memptr to 2-byte boundary |
| 32-33  | memptr           | u16  |                                  |
| 34     | q                | u8   |                                  |
| 35     | _pad_before_eipos | u1  | aligns eipos to 2-byte boundary  |
| 36-37  | eipos            | u16  |                                  |
| 38-39  | haltpos          | u16  |                                  |
| 40-42  | nmi_in_progress..int_gate | u8 | 3 × u8                |
| 43     | _pad_before_halt_cycle | u1 | aligns halt_cycle to 4-byte bdry |
| 44-47  | halt_cycle       | u32  |                                  |

`ttd.ksy` declares these as `_pad_*` fields (Kaitai convention for
"consumed but not exported"); the Python parser skips them with `r.u8()`
calls annotated with the same offset comments. **A future hardening pass
on the C++ side could `static_assert(offsetof(TTDCpuState, memptr) == 32)`
to catch the day a new field lands and shifts the layout.**

Discovered while landing this phase: an earlier Python parser draft
crashed with "implausible peripheral blob size 1929379840 (>1 MB)" — that
was the parser misaligned by 3 bytes reading into the next field. Lesson:
the very first conformance test against a C++-generated fixture is the
safety net. The fixture generator lives at
`TTD_Dump_Format_Test.DISABLED_WriteFixtureFile_ForPythonConformance`.

### 5. `CaptureRestoreSelfTest` as a triage primitive

`TimeTravelManager::CaptureRestoreSelfTest()` is a single-frame
capture→restore→hash round-trip with a `SelfTestResult` payload. It's
deliberately small (one frame, no replay) so any failure isolates:

| Test result                            | Diagnosis                                          |
|----------------------------------------|----------------------------------------------------|
| `pre_post_match == true`               | capture and restore are individually correct. Bugs in multi-frame state evolution live elsewhere (silent replay, journal injection, peripheral state-on-edge). |
| `pre_post_match == false`, hashes differ | inspect diff in CPU/chipset/RAM digest to narrow down. RAM digest mismatch alone → page-store bug. CPU mismatch → field-copy bug. Chipset mismatch → port-latch bug. |

For the user-reported "no seek refresh" + "Play → reset" bugs from
sessions up to 2250 frames, `CaptureRestoreSelfTest` passes — confirming
capture/restore are individually correct and the bugs are in the
multi-frame state evolution, not the single-frame primitives.

### 6. Hand-written Python parser as default; Kaitai-generated as drop-in

The analyzer ships with a hand-written parser (`ttd_format.py`, stdlib
only). This is a deliberate trade-off:

- **Pro**: zero build-time dependency. `python3 main.py info foo.ttd`
  works on a fresh checkout.
- **Pro**: same API contract as a Kaitai-generated parser would have.
- **Con**: every schema change requires updating two places (the `.ksy`
  *and* the hand-written parser).

The hand-written parser is the default because the schema is small
(~10 record types) and changes infrequently. If the schema grows or
third-party parsers become a priority, regenerate from `.ksy` and drop
the hand-written version in its place — no caller changes required.

### 7. Analyzer subcommand surface

The CLI has six subcommands, each with a single purpose, exit codes that
make CI integration trivial:

| Subcommand    | Purpose                                | Exit code                |
|---------------|----------------------------------------|--------------------------|
| `info FILE`   | Header + checkpoint summary            | 0                        |
| `validate`    | Structural integrity only (CI gate)    | 0=ok, 1=errors           |
| `analyze`     | Integrity + anomalies + summary        | 0                        |
| `report`      | Markdown report (stdout or `-o file`)  | 0                        |
| `render`      | One checkpoint's screen to PNG/PPM     | 0                        |
| `render-all`  | Every checkpoint to OUTDIR/frame_*.png | 0                        |
| `heatmap`     | 1-px-per-checkpoint dirty-page heatmap | 0                        |

`validate` is the CI gate: it exits non-zero on any structural error
(truncated file, dangling page ref, non-monotonic timeline) and is
suitable for blocking a release that contains a corrupt recording.

## What this phase deliberately does NOT ship

- **No integration with the automation CLI / WebAPI wrapper yet.** The
  capability is in core, but there's no `emu.ttd_dump(path)` Python
  binding, no `ttd dump` CLI command, no `POST /ttd/dump` WebAPI route
  yet. Producers are wired as they're needed; the universal capability is
  in place.
- **No MPEG-style keyframe/delta encoding of the page store.** The user's
  original framing suggested MPEG I/P/B frames; the shipped v1 format is
  COW-only (deduplicated unique pages, no deltas). This is sufficient for
  ~2000-frame sessions; revisit when a session exceeds ~50 MB on disk.
- **No streaming writer.** The whole session is held in memory and dumped
  in one pass. For multi-GB sessions a chunked writer would be needed.
- **No 64-bit-clean schema for very long sessions.** `checkpoint_count`
  is `u32` (max 4G checkpoints); `frame_counter` is `u64` (sufficient
  for ~500 years at 50 Hz). Schema v2 can extend if needed.
- **No reverse round-trip from Python.** The analyzer only reads `.ttd`
  files; it can't generate them. Producing .ttd files from non-C++ tools
  is a non-goal — the C++ writer is canonical.

## Tests

### C++ side (`core/tests/debugger/ttd/ttd_dump_format_test.cpp`)

- `Header_RoundTrip_EmptySession` — empty timeline still round-trips.
- `Header_BadMagic_Fails` — non-`.ttd` inputs rejected cleanly.
- `Deserialize_FutureSchemaVersion_Fails` — forward-compat guard works.
- `SingleCheckpoint_RoundTrip_IsIdentical` — the core round-trip test.
- `MultiCheckpoint_WithDirtyPages_RoundTrip` — page store + remapping.
- `CaptureRestoreSelfTest_Passes_OnFreshSession` — single-frame
  capture→restore is byte-identical.
- `CaptureRestoreSelfTest_Passes_AfterRamMutation` — same after writes.
- `DISABLED_WriteFixtureFile_ForPythonConformance` — generates the
  fixture used by the Python analyzer.

### Python side

- Validated against `testdata/fixture.ttd` (559 KB real .ttd from the
  C++ test). All six subcommands (`info`, `validate`, `analyze`,
  `report`, `render`, `heatmap`) produce expected output.
- The hand-written parser correctly identifies the fixture's intentional
  degenerate case (3 checkpoints at the same frame) as a
  `timeline_not_monotonic` integrity error — exactly what it should do.
- Screen render produces 320×256 PNG (256×192 visible + 32 px border
  on each side) with the correct ZX Spectrum palette (15 of the 16
  standard colours present in the test fixture).

## Followups (not blocking, parked)

- **Wire producers.** Add `ttd dump <path>` to the automation CLI and
  `POST /api/v1/emulator/{id}/ttd/dump` to the WebAPI. Both are thin
  wrappers around `SerializeSession(std::ostream&)`.
- **Per-instruction StepBack** (deferred from Phase 2 Item 4). The
  `ReplayWithinFrame` infrastructure is in place; this phase didn't
  touch it.
- **Schema v2 design space.** When v2 becomes necessary (e.g., to add
  TS-Conf cram/sfile files at 2 KB/checkpoint), draft the v2 layout,
  bump `MAX_SUPPORTED_SCHEMA_VERSION`, keep v1 readers working on v1
  files. Do NOT make v1 readers try to interpret v2 — refuse cleanly.

## Bug fixed during this phase (root cause: schema-discipline gap)

**u8 vs u16 size mismatch for `_modelRamPages`.** The C++ engine stores
`_modelRamPages` as `uint16_t`, but the original `.ksy` declared
`model_ram_pages` as `u1`. The writer was emitting 2 bytes; the reader
expected 1 byte; every subsequent field was misaligned by 1 byte
(`cpu_state_size` parsed as `12288` instead of `48`). Fixed by casting
to `uint8_t` before `WritePod`. **The lesson**: every engine field
written to disk needs to have its size confirmed against the schema —
not assumed from the C++ type.
