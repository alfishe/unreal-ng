# Phase 1 — TTD Recorder (Checkpoint Core)

Started: 2026-07-19
Completed: 2026-07-19
Reference: `docs/emulator/design/debugger/time-travel-debug/implementation-plan.md` §3.A1
Parent TDD: §6, §7, §10.4

Cross-cutting decisions applied: see [`decisions.md`](decisions.md).

## Outcome

All 7 Phase 1 items landed. Per-frame checkpoint capture works end-to-end:
at every `MainLoop::OnFrameEnd`, the recorder snapshots CPU + chipset + all
audited peripherals (AY, tape, FDC, Covox) and interns dirty RAM pages
into the COW page store. The session lifecycle invalidates on every
documented teleport (Reset / Load* / speed change / ROM reload). The
`GET /ttd/status` endpoint exposes session state to automation.

**131 TTD tests green** (50 from Sprint 0 + 81 new in Phase 1).

## Items delivered

| # | Item | Commit | Tests |
|---|------|--------|-------|
| 1 | TTDSerializable interface + Z80/chipset capture (full Z80State copy incl. MEMPTR/Q/prefix; port latches from EmulatorState) | `42156975` | covered by Item 4d |
| 2 | Dirty tracker + MemoryWriteDebug hook + never-touched sentinel logic | `d6adeda4` (item 3 batch) | `TTD_DirtyTracker_*` (10) |
| 3 | COW page store (intern/refcount/release, budget, thinning) | `d6adeda4` | `TTD_PageStore_*` (15) |
| 4 | TTDManager skeleton + OnFrameBoundary capture + EmulatorContext/MainLoop wiring | `6011f4ae` | `TTDManager_Test.*` (12) |
| 5a | AY/TurboSound TTDSerializable + manager wiring | `0672a9d4` | `TTD_AY_Serializer_*` (10) |
| 5b | Tape TTDSerializable + round-trip test | `3a1cf8a8` | `TTD_Tape_Serializer_*` (6) + manager integration (1) |
| 5c | WD1793+FDD TTDSerializable (hardest audit) | `270dd5a7` | `TTD_WD1793_Serializer_*` (7) + `TTD_FDD_Serializer_*` (5) + manager (1) |
| 5d | Covox TTDSerializable + round-trip test | `c7f21a7e` | `TTD_Covox_Serializer_*` (6) |
| 6 | Session lifecycle invalidation hooks (Reset/Load*/SetSpeedMultiplier/LoadROM) | `17cdab47` | `TTD_SessionLifecycle_*` (8) |
| 7 | `GET /ttd/status` WebAPI endpoint + OpenAPI spec | `ac0c5daa` | `TTD_StatusString_*` (3) + `TTD_StatusEndpoint_*` (6) |

## Phase-specific design decisions

### TTDSerializable contract (parent TDD §6.4)
Three-method interface: `TTDStateSize()` / `TTDSaveState(uint8_t*)` /
`TTDLoadState(const uint8_t*)`. No allocation in save (caller provides the
buffer); no allocation in load (caller sizes from `TTDStateSize`).
Runs every frame on the emulator thread — must be cheap and side-effect-free.

### Cursor-packed serialization (Phase 1 house style)
All peripheral serializers use the same pattern:
- Anonymous-namespace `put_u8` / `put_u16` / `put_u64` / `get_u*` / `get_i8`
  helpers in the .cpp, doing alignment-safe `memcpy` into a cursor.
- `size_t` fields serialized as `uint64` for 32/64-bit portability.
- `static_assert` on the constant total size so future field additions
  don't silently drift.
- Serializer doc comments enumerate every captured field with its byte
  offset, plus an explicit "excluded by design" list.

### WD1793 / FDD serialization audit (item 5c — hardest)
- 251 bytes total per FDC subsystem checkpoint (143 controller + 4×27 FDDs).
- `_operationFIFO` (queue of `FSMEvent` containing `std::function`) is
  **not** serializable — cleared on load. Per TDD §12.2 acceptable for v1
  because read-only workloads (most demos) idle the FDC at frame boundaries.
  `fifoDepth` is captured as a diagnostic byte only.
- Pointer fields (`_rawDataBuffer`, `_idamData`, `_sectorData`,
  `_writeTrackTarget`, `_diskImage`, `_selectedDrive`) are not restored
  directly — they're re-derived: pointers reset to nullptr (re-established
  by next command setup), `_selectedDrive` re-resolved from `_drive`
  index + `coreState.diskDrives[]`.
- Pre-existing virtual methods `reset()`, `portDeviceInMethod()`,
  `portDeviceOutMethod()` lacked `override` — surfaced by
  `-Winconsistent-missing-override` once TTDSerializable methods added
  `override`. Fixed alongside.

### Session invalidation contract (item 6)
Six entry points in `emulator.cpp` call `InvalidateSession("<reason>")`:
`Reset`, `SetSpeedMultiplier`, `LoadSnapshot`, `LoadTape`, `LoadDisk`,
`LoadROM`. Per parent TDD §4.2. Hooks are placed *before* the actual state
mutation so the timeline is dropped while it's still valid.

### Status endpoint (item 7)
- `GET /api/v1/emulator/{id}/ttd/status` returns the full `TTDSessionInfo`
  payload plus a `ttd_available` capability flag.
- The `"idle"|"recording"|"detached"` state-string mapping lives in
  `ttd::TTDSessionStateToString` in core (not in the webapi adapter) so
  future Lua/Python/CLI bindings (§10.4) can reuse the same canonical
  spellings.
- `ttd_available=false` with `state="idle"` when the manager is missing
  (minimal build) — doubles as a capability probe.

## What Phase 1 deliberately does NOT ship

- **No restore path.** Checkpoints are captured but cannot be applied back
  to the live emulator yet. That's Phase 2 Item 1.
- **No silent-replay mode.** The `_context->ttdReplayActive` flag and the
  suppression plumbing (breakpoints, MessageCenter, audio mute-at-host,
  analyzer suppression, recording-off) do not exist yet. Phase 2 Item 2.
- **No seek / step-back / step-forward.** Phase 2 Items 3–4.
- **No resume-from-past truncation.** Phase 2 Item 5.
- **No input journal.** Per TDD §5.1, lands as Phase 2 Item 3 (needed by
  silent replay for keyboard-driven workloads). External-event markers for
  unjournaled nondeterminism (tape control, disk writes) land later.
- **No divergence-corpus tests.** The harness exists (Sprint 0 item 0.4);
  the corpus wiring (BASIC idle, scroller, contention-sensitive multicolor,
  AccuracyCoinZX, TR-DOS loader) is Phase 2 exit work, not Phase 1.
- **No `ttd_capture_benchmark` regression gate.** The benchmark exists;
  wiring it as a pass/fail CI gate is part of Phase 2 exit.

## Known nits carried forward

- **Class rename not yet applied.** `decisions.md` mandates
  `TTDManager → TimeTravelManager`. The rename was deferred to keep
  Phase 1 commits focused; it lands as the first Phase 2 commit.
- **WD1793_Test.FSM_CMD_Write_Track** still segfaults (pre-existing,
  reproduces on `master` without any TTD changes — see `decisions.md`).
