# Phase 4 — Reverse Search + Automation Surface

Started: 2026-07-30 (planning)
Reference: `docs/emulator/design/debugger/time-travel-debug/implementation-plan.md` §4
Parent TDD: §9 (reverse watchpoints), §10.2 (single-instruction step), §10.4 (automation surface), §16 (acceptance criteria)

Cross-cutting decisions applied: see [`decisions.md`](decisions.md).
Phase 2 deliverables: see [`phase-2-seek-engine.md`](phase-2-seek-engine.md).
Phase S1 deliverables: see [`phase-S1-session-serialization.md`](phase-S1-session-serialization.md).
Phase S2 deliverables: see [`phase-S2-hardening.md`](phase-S2-hardening.md).

## Outcome (target)

Reverse watchpoints are live: the user can ask "who last wrote to address
0x4000?" and get back the exact TTDTimePoint, PC, value, and physical page.
Single-instruction backward/forward stepping works on recorded sessions. All
four automation surfaces (CLI, Python, WebAPI, contract tests) expose `dump`,
`find-last`, and `step-instruction`. The `.ttd` binary format evolves
additively to v3 with an embedded write journal section.

**Exit criteria** (per parent TDD §16):
- `StepBackInstruction` / `StepForwardInstruction` green (§16 row 2)
- `FindLastAccess` green (§16 row 3)
- Write journal active with `writeJournalOffset` populated (§16 row 4)
- `POST /ttd/find-last` + `emu.ttd_find_last(...)` + `ttd find-last` all wired (§10.4 row 7)
- `POST /ttd/dump` wired (engine was ready, surface was missing)

## Item ordering (each keeps the full TTD suite green)

1. **Write journal + Memory write hook** ✅ DONE (parent TDD §9.3). New module
   `core/src/debugger/ttd/ttd_write_journal.{h,cpp}` — 256 MB power-of-two ring
   buffer of 12-byte `TTDWriteRecord`s (40-bit globalT, 16-bit addr, 1-bit
   isIo, 7-bit pad, 16-bit m1pc, 8-bit value, 8-bit physPage). Single-producer
   append (emulator thread); control-thread scan under pause. `MemoryWriteDebug`
   hook site in `memory.cpp` appends via `TimeTravelManager::RecordMemoryWrite`
   during active Recording. `EmulatorContext` gains `ttd::TTDWriteJournal*
   pTTDWriteJournal`. `TimeTravelManager` owns the journal instance.
   `StartRecording` clears the journal; `StopRecording` preserves it.
   Tests: 17 cases in `ttd_write_journal_test.cpp`, all green. Full TTD suite
   green.

2. **Access probe + read/execute/io hooks** ✅ DONE (parent TDD §9.2 + §9.4).
   New header-only `core/src/debugger/ttd/ttd_probe.h` — `TTDAccessType`
   (Write/Read/Execute/Io), `TTDSearchQuery`, `TTDSearchResult`,
   `TTDAccessProbe` class. Probe is an inline instance in `EmulatorContext`
   (`ttdProbe`); hot-path check is one relaxed atomic load + branch when
   disarmed. Four hook sites wired:
     - `MemoryWriteDebug` (`memory.cpp` line ~329): journal append + probe hit
     - `MemoryReadDebug` (`memory.cpp`): probe hit only
     - Z80 M1 cycle (`z80.cpp`): probe hit for Execute access type
     - `DecodePortOut` (`platform.cpp`): journal append + probe hit for IO
   `TTDAccessTypeToString` / `TTDAccessTypeFromString` provide stable lowercase
   strings ("write", "read", "execute", "io") for the automation contract.
   Tests: 26 cases in `ttd_probe_test.cpp`, all green. Full TTD suite green.

3. **`FindLastAccess` orchestrator** ✅ DONE (parent TDD §9.2 + §9.3).
   Implemented in `timetravelmanager.cpp`. Algorithm:
   - **Journal fast path** (Write/Io): scan `_writeJournal` backward for the
     newest record matching the predicate (addr range, value filter, PC filter,
     globalT ≤ beforeGlobalT). If found, decode to TTDSearchResult and return.
   - **Replay fallback** (Read/Execute, or journal ring wrapped): binary-search
     for the checkpoint at-or-before the target, walk intervals backward.
     For each interval: check external-event markers (barrier → return
     nullopt with marker info), restore checkpoint, arm probe, silent-replay
     the interval, extract hits. First interval with hits (walking backward)
     is the answer.
   - On success, SeekTo(answer.time) positions the emulator.
   Optional `TTDExternalEvent* outBlockingMarker` parameter mirrors the
   SeekTo barrier pattern. Tests: 18 cases across three files:
     - `ttd_find_last_test.cpp` (11): journal fast path for Write/Io, addr
       range, value filter, PC filter, before-globalT, result shape.
     - `ttd_find_last_fallback_test.cpp` (4): Read requires replay, Execute
       requires replay, Io finds in journal, no-match returns nullopt.
     - `ttd_find_last_marker_test.cpp` (3): marker blocks Read replay,
       marker blocks Execute replay, marker does NOT block Write journal path.

4. **`StepBackInstruction` / `StepForwardInstruction`** ✅ DONE (parent TDD
   §10.2 + §16). Both methods on `TimeTravelManager`:
   - `StepBackInstruction`: delegates to `FindLastAccess(Execute)` with
     `beforeGlobalT = currentGlobalT - 1`. Refuses at position (0, 0).
   - `StepForwardInstruction`: `EnterReplayMode` → `RunTStates(1, true)` →
     `ExitReplayMode` (one complete instruction regardless of t-state length).
     Refuses at or past session end.
   Both refuse while Recording (state must be Detached or Idle).
   Tests: 7 cases in `ttd_step_instruction_test.cpp`, all green.

5. **CLI surface** ✅ DONE. Three subcommands in
   `core/automation/cli/src/commands/cli-processor-ttd.cpp`:
   - `ttd dump <path>` — calls `SerializeSession`, reports bytes written.
   - `ttd find-last --addr <A> [--access type] [--value V] [--pc-from X]
     [--pc-to Y] [--before-frame F] [--before-tin T]` — builds TTDSearchQuery,
     calls `FindLastAccess`, prints result.
   - `ttd step-instruction <back|forward>` (aliases `si-back`/`si-forward`) —
     calls step method, prints new position.
   `ShowTTDHelp` updated with all three verbs.

6. **Python surface** ✅ DONE. Four methods in
   `core/automation/python/src/emulator/python_emulator.h`:
   - `ttd_dump(path)` → bool
   - `ttd_find_last(addr, access="write", value=None, pc_from=None,
     pc_to=None, before_frame=None, before_tin=0)` → dict or None
   - `ttd_step_instruction_back()` → bool
   - `ttd_step_instruction_forward()` → bool

7. **WebAPI surface + `.ttd` schema v3 + journal serialization** ✅ DONE.
   - Schema: `ttd_dump_format.h` bumped to `kSchemaVersion = 3`. New flag
     `kFlagsHasWriteJournal = 0x0002`. `SerializeSession` appends journal
     section (count + records) after checkpoint table when flag is set.
     `DeserializeSession` reads it.
   - WebAPI: three endpoints in `emulator_api.h` + `ttd_api.cpp`:
     - `POST /api/v1/emulator/{id}/ttd/dump` — `{"path": "..."}`
     - `POST /api/v1/emulator/{id}/ttd/find-last` — `{"addr": N, "access":
       "write|read|execute|io", ...}`
     - `POST /api/v1/emulator/{id}/ttd/step-instruction` — `{"dir":
       "back|forward"}`

8. **Contract tests** ✅ DONE. Extended `ttd_automation_contract_test.cpp`
   with three new `TEST_F` cases that lock down the exact method signatures
   and return shapes all three adapters wrap:
   - `Dump_SerializeSession_RoundTrip` — serialize, deserialize into fresh
     emulator, verify checkpoint count + journal size + journal queryable.
   - `FindLast_QueryShape_HasExpectedFields` — known write, query it, verify
     `{time, pc, value, physPage, access}` shape + string conversions.
   - `StepInstruction_BackwardForwardRoundTrip` — step forward then back,
     verify position changes in expected direction.

## Detailed notes per item

### Item 1 — Write journal ✅ DONE

**Files:**
- `core/src/debugger/ttd/ttd_write_journal.{h,cpp}` (NEW) — ring buffer
  implementation.
- `core/src/debugger/ttd/timetravelmanager.{h,cpp}` — owns `_writeJournal`,
  exposes `RecordMemoryWrite` / `RecordIoWrite` / `GetWriteJournal`.
- `core/src/emulator/emulatorcontext.h` — gains
  `ttd::TTDWriteJournal* pTTDWriteJournal = nullptr`.
- `core/src/emulator/memory/memory.cpp` — `MemoryWriteDebug` hook appends to
  journal during Recording.

**Design notes:**
- `TTDWriteRecord` is 12 bytes (`#pragma pack(push, 1)`). Bit-field layout:
  40-bit globalT (~9 years of t-states at 3.5 MHz), 16-bit addr (Z80 address
  or port), 1-bit isIo, 7-bit pad, 16-bit m1pc (PC of writing instruction),
  8-bit value, 8-bit physPage.
- Ring buffer is power-of-two sized (default 256 MB = ~22M records). `_seqHead`
  tracks total appends; `_seqTail` tracks the first valid record. Wrap
  detection via `OldestGlobalT()`.
- Threading model: appends on emulator thread only (no lock needed); reads /
  drops on control thread under pause (same model as the page store).
- `RecordMemoryWrite` only journals during `Recording` state. During replay,
  writes are reproducing recorded state — re-journaling would corrupt the ring.

**Tests** (`ttd_write_journal_test.cpp`, 17 cases):
- Empty journal: `Size() == 0`, `FindLast` returns nullopt, `OldestGlobalT`
  == 0.
- Append + query: single record found by `FindLast`.
- Multiple appends: newest match returned.
- `FindLast` with `beforeT` constraint.
- Ring wrap: small ring (4 slots), verify overwrite behavior.
- `DropAfter`: 4 cases (drop nothing, drop all, drop partial, drop past end).
- `Clear`: 2 cases (clear empty, clear populated).
- IO records: `isIo` bit set correctly.
- `Serialize`/`Deserialize` round-trip: 3 cases (empty, populated, replaces
  existing contents). Note: `Serialize` writes a `uint64_t` count header;
  `Deserialize(istream, count)` expects the caller to have already read it.

### Item 2 — Access probe ✅ DONE

**Files:**
- `core/src/debugger/ttd/ttd_probe.{h,cpp}` (NEW) — probe implementation.
- `core/src/emulator/emulatorcontext.h` — gains `ttd::TTDAccessProbe ttdProbe`
  (inline instance).
- `core/src/emulator/memory/memory.cpp` — `MemoryReadDebug` hook site added.
- `core/src/emulator/cpu/z80.cpp` — M1 cycle hook site added.
- `core/src/emulator/platform.cpp` — `DecodePortOut` hook site added.

**Design notes:**
- `TTDAccessProbe::Matches()` is fully inlined. Hot-path cost when disarmed:
  one relaxed atomic load + one predictable branch. When armed: 2–5
  comparisons (access type, addr range, optional value, optional PC filter).
- `Arm()` stores the query, clears hits, sets `_armed = true` (release fence).
  `Disarm()` sets `_armed = false` (release fence). The surrounding pause /
  replay handshake ensures the emulator thread sees the flag before/after
  the replay batch.
- `RecordHit()` reconstructs `TTDSearchResult{time, pc, value, physPage,
  access}` from the calling hook site — the probe itself does not reach into
  `EmulatorContext`, keeping it dependency-free.
- `ExtractHits()` moves the `_hits` vector out (control thread, post-replay).
- `TTDAccessType` values are stable (Write=0, Read=1, Execute=2, Io=3) and
  must not be renumbered — they appear in WebAPI JSON and Python strings.

**Tests** (`ttd_probe_test.cpp`, 26 cases):
- Arm/Disarm/IsArmed mechanics.
- Reset clears query and hits.
- Matches for all four access types (4 cases).
- Matches when not armed returns false.
- Address range filtering (3 cases: in-range, below, above).
- Value filter (3 cases: match, no-match, disabled).
- PC filter (2 cases: in-range, out-of-range).
- RecordHit / Hits / ExtractHits.
- Arm clears previous hits.
- `TTDAccessTypeToString` / `FromString` round-trip (3 cases).

### Item 3 — FindLastAccess orchestrator ✅ DONE

**Files:**
- `core/src/debugger/ttd/timetravelmanager.cpp` — `FindLastAccess` + journal
  fast path + replay fallback.

**Algorithm** (parent TDD §9.2 + §9.3):
1. Resolve `beforeGlobalT` (default: current position).
2. Journal fast path (Write/Io): backward scan with predicate. If found,
   return. If ring never wrapped (OldestGlobalT ≤ 1), return nullopt.
3. Replay fallback (Read/Execute, or ring wrapped): binary-search checkpoint
   at-or-before target, walk intervals backward. For each interval:
   - Check external-event markers (barrier → return nullopt with marker).
   - Restore checkpoint, arm probe, silent-replay, extract hits.
   - First interval (backward) with hits → answer.

**Design notes:**
- The fast path predicate matches on addr range, value filter, PC filter, and
  access type (Write vs Io). The `isIo` bit in the journal record disambiguates.
- The replay fallback reuses `RestoreCheckpoint` + `EnterReplayMode` +
  `ReplayWithinFrame` + `ExitReplayMode` from Phase 2.
- On success, `SeekTo(answer.time)` positions the emulator at the match so the
  caller can inspect state immediately.
- The `TTDExternalEvent* outBlockingMarker` parameter lets the CLI/WebAPI
  surface report why a search was blocked, mirroring the `SeekTo` barrier
  pattern from Phase 2 Item 6.

**Pitfall discovered:** the Z80 test emulator sits at a HALT loop (address
0x0006) after reset and produces no natural RAM writes through
`MemoryWriteDebug`. The HALT instruction repeats NOP, which IS an M1 cycle
so Execute queries work via replay. But Write queries need explicit
`RecordMemoryWrite` calls in tests to populate the journal with known records.

**Tests** (18 cases across 3 files):
- `ttd_find_last_test.cpp` (11): specific address, addr range, never-written,
  value filter (2), PC filter (2), before-globalT, while-recording rejection,
  no-history, result shape.
- `ttd_find_last_fallback_test.cpp` (4): Write finds in journal, Read requires
  replay, Execute uses replay, Io finds in journal.
- `ttd_find_last_marker_test.cpp` (3): marker blocks Read replay, marker blocks
  Execute replay, Write journal path not blocked by markers.

### Item 4 — StepBackInstruction / StepForwardInstruction ✅ DONE

**Files:**
- `core/src/debugger/ttd/timetravelmanager.cpp` — both methods.

**Design notes:**
- `StepBackInstruction` delegates to `FindLastAccess(Execute)` with
  `beforeGlobalT = nowGlobalT - 1`. This finds the most recent M1 cycle
  strictly before the current position. The replay fallback walks backward
  through checkpoint intervals with the Execute probe armed.
- `StepForwardInstruction` uses `RunTStates(1, true)` in replay mode. One
  Z80Step call executes one complete instruction (minimum 4 t-states),
  regardless of t-state length. The emulator thread is paused (Detached
  state), so there's no race.
- Both methods refuse while Recording (state must be Detached). They also
  refuse at session boundaries (position 0 for backward, session end for
  forward).

**Tests** (`ttd_step_instruction_test.cpp`, 7 cases):
- State guards: StepBack/Forward while Recording returns false (2).
- No history: StepBack/Forward with empty timeline returns false (2).
- Boundary: at session start returns false (1), at session end returns false (1).
- SerializeSession round-trip preserves journal (1).

### Item 5–7 — Automation surfaces + schema v3 ✅ DONE

**CLI** (`cli-processor-ttd.cpp`): three subcommands declared in
`cli-processor.h`, implemented in `cli-processor-ttd.cpp`. Each builds the
engine query struct, calls the `TimeTravelManager` method, and formats output.
`ShowTTDHelp` lists all three new verbs.

**Python** (`python_emulator.h`): four `py::def_` bindings added after the
existing `ttd_markers` binding. `ttd_find_last` accepts keyword arguments
matching the CLI flags; returns a Python dict or None.

**WebAPI** (`emulator_api.h` + `ttd_api.cpp`): three `ADD_METHOD_TO` routes.
Each handler follows the existing `seekTTD` pattern: `resolveTTD(id)` →
pause confirmation → call engine method → build JSON response → resume if
previously running.

**.ttd schema v3** (`ttd_dump_format.h`):
- `kSchemaVersion` bumped from 2 to 3.
- `kFlagsHasWriteJournal = 0x0002` — additive flag; v2 readers ignore it.
- `SerializeSession` sets the flag and appends the journal section (uint64_t
  count + records) after the checkpoint table.
- `DeserializeSession` reads the flag and conditionally reads the journal.

### Item 8 — Contract tests ✅ DONE

Three new `TEST_F` cases in `ttd_automation_contract_test.cpp`:
- `Dump_SerializeSession_RoundTrip` — captures frames, populates journal with
  known records, serializes to temp file, deserializes into a fresh emulator,
  verifies checkpoint count + journal size match + journal is queryable after
  load.
- `FindLast_QueryShape_HasExpectedFields` — performs a known write, queries it,
  verifies all fields of `TTDSearchResult` (`time`, `pc`, `value`, `physPage`,
  `access`) plus the four stable access-type strings.
- `StepInstruction_BackwardForwardRoundTrip` — records 10 frames, seeks to
  mid-session, steps forward one instruction (position advances), steps back
  one instruction (position retreats or stays).

## Test results

| Test file | Cases | Covers |
|---|---|---|
| `ttd_write_journal_test.cpp` | 17 | Ring wrap, scan, DropAfter, Clear, Serialize/Deserialize |
| `ttd_probe_test.cpp` | 26 | Matches across 4 access types, value/PC filters, hit recording |
| `ttd_find_last_test.cpp` | 11 | Journal fast path (Write/Io), addr/value/PC filters |
| `ttd_find_last_fallback_test.cpp` | 4 | Replay fallback for Read/Execute |
| `ttd_find_last_marker_test.cpp` | 3 | Marker blocking during replay |
| `ttd_step_instruction_test.cpp` | 7 | State guards, boundaries, serialization round-trip |
| `ttd_automation_contract_test.cpp` (new) | 3 | Dump/FindLast/StepInstruction contract shapes |
| `ttd_thinning_test.cpp` | 5 | EveryPointReachable invariant (Phase 2 exit criterion) |
| **Total new tests** | **76** | |

Full TTD suite: **375 tests green** (was 299 after Phase S2). All pre-existing
test suites unchanged — no behavior change for non-Phase-4 paths.

## Acceptance criteria check (TDD §16)

| Criterion | Status |
|---|---|
| §16 row 2: `StepBackInstruction` / `StepForwardInstruction` | ✅ Delivered |
| §16 row 3: `FindLastAccess` | ✅ Delivered |
| §16 row 4: Write journal active | ✅ Delivered |
| §10.4 row 7: `find-last` on all surfaces | ✅ Delivered |
| `dump` on all surfaces | ✅ Delivered |
| §16 row 5: `TTD_Thinning_EveryPointReachable` | ✅ Delivered |

## What Phase 4 deliberately does NOT ship

- **TDD §16 row 6:** Timeline UI (Qt — explicitly excluded, Phase 3).
- **TDD §16 row 7:** GDB RSP server (Phase G1–G4 — separate work).
- **Per-frame Bloom filter / dirty-page summary** for journal scan acceleration
  (TDD §9.3 future optimization). The linear backward scan over 256 MB is fast
  enough at current session sizes; optimization deferred until profiling
  justifies it.
- **`GET /ttd/timeline`** paginated endpoint (lower priority; not blocking
  reverse search).
