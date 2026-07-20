# Time-Travel Debugging — Cross-cutting Decisions

Decisions recorded here apply forward to **every** sprint and phase of the
TTD / GDB work. New sprints do not re-litigate them; they reference this file.

## Naming conventions

### Class name

- `TTDManager` (TDD §10.2) renamed to **`TimeTravelManager`**.
  - Reason: the abbreviation was non-descriptive.
  - Parallel to the `RecordingManager` / `DebugManager` suffix pattern.
  - Consistent with the `timetravel` feature flag.

### File names

- TDD §10.1 file names follow: `ttdmanager.h/.cpp` → **`timetravelmanager.h/.cpp`**.
- The `ttd/` folder under `core/src/debugger/` keeps the short prefix —
  it parallels `breakpoints/`, `disassembler/`, etc., and the short form
  reads better in paths than `time-travel/` (no hyphen to quote-shell-escape).

### User-facing verbs

- Lowercase short prefix `ttd_` retained in user-facing automation verbs:
  - `emu.ttd_start()` (Lua / Python bindings)
  - `/api/v1/.../ttd/status` (WebAPI)
  - `ttd start` (CLI)
- Reason: verb prefix matches the `ttd` feature alias; not a class name.
  Users typing commands see a short token; class names in code stay descriptive.

### Namespace

- All TTD-only C++ symbols live in `namespace ttd { ... }`.
- Parallel to other debugger subsystems.

## Flag-layer decisions

Two distinct gates — do not conflate them:

| Layer | Gate | Where defined | When evaluated | Effect when off |
|---|---|---|---|---|
| Runtime | `timetravel` feature flag (alias `ttd`) | `FeatureManager::setDefaults()` in source; user may override via a local `features.ini` (file is user-owned, **not** versioned — codebase handles absence gracefully) | Every frame boundary (cached bool) | TTD recorder does nothing; zero overhead |
| Build-time | `ENABLE_GDB_AUTOMATION` CMake option | Root `CMakeLists.txt` | CMake configure | GDB RSP server target not built; no code compiled |

### Resolved ambiguity

GDB TDD §6.4 says "Feature flag: gdbserver". This is interpreted as the
**build-time** gate, **not** a runtime flag, because:

1. GDB TDD §2 lists gdbserver among the "compile-time gates" alongside the
   existing four automation transports (`ENABLE_LUA_AUTOMATION`,
   `ENABLE_PYTHON_AUTOMATION`, `ENABLE_WEBAPI_AUTOMATION`,
   `ENABLE_CLI_AUTOMATION`).
2. The GDB RSP server is a heavy networked subsystem that should not ship
   in emulator binaries that don't need it (videowall, screen-viewer).
3. A runtime flag for a server that isn't compiled in makes no sense.

The `timetravel` runtime flag, by contrast, is explicitly registered in
`FeatureManager` per parent TDD §10.3, because TTD recording must be
toggleable at runtime (e.g. "start capturing from this point") even after
the binary is built.

## Payload migration policy

When adding new fields to a MessageCenter payload, the new payload class
inherits from the existing one (e.g. `EmulatorStateChangePayload : public
SimpleNumberPayload`). This means:

- Legacy observers that read only the base-class fields keep working unchanged.
- New observers `dynamic_cast` to the derived class to read the new fields.
- Posting sites upgrade in-place; no observer migration is required unless
  an observer actually wants the new field.

Verified by Sprint 0: all 7 `NC_EMULATOR_STATE_CHANGE` and
`NC_EXECUTION_BREAKPOINT` post sites were upgraded in-place; **zero**
existing observers required changes.

## Run-control claim semantics

- **Advisory owner token**, not a mutex. Never held during emulator work.
- Lives in `EmulatorContext` (per-instance), guarded by a mutex for
  take/release only.
- Default-constructed `UUID()` (all zeros) is the **unclaimed sentinel**.
- Do NOT use `UUID::isNil()` to check for unclaimed — that method has
  inverted semantics in the current codebase (returns true for non-nil).
  Compare against a default-constructed `UUID` directly.
- Pause and read-only queries are always allowed, regardless of claim.
- Run-affecting operations (Resume / Step / Seek / state-write) are
  refused when the target is GDB-paused and the caller does not hold the claim.
- Enforcement at call sites lands in Phase 2 (TTD seek) and G1 (GDB stub).
  Sprint 0 ships the mechanism only.

## Machine-state hash conventions

- **Algorithm**: 64-bit FNV-1a. Public-domain, deterministic, fast enough
  for per-frame capture at all realistic RAM sizes (~310 MiB/s).
- **Padding**: `CaptureSnapshot` must `memset(0)` the snapshot struct
  before writing fields, so byte-wise hashing produces reproducible output
  regardless of struct padding.
- **Exclusions**: snapshot deliberately excludes all host-side state
  (MemoryInterface pointers, trace cursors, `isDebugMode`, `prev_pc` /
  `m1_pc` / `nextpc` / `last_branch`, decoded opcode cache). Verified by
  `MachineStateHash_Test.Snapshot_HostSideFieldsExcluded`.
- **Upgrade path**: if FNV-1a ever becomes a bottleneck (it won't, per
  Sprint 0 numbers — RAM digest is 2% of a 50 Hz frame budget at 128 KB),
  swap to xxHash or hardware CRC32 behind the same `HashBytes()` API.
  No caller changes required.

## Pre-existing test failures (carry forward)

These failures reproduce on `master` independently of any TTD work. When
running the full test suite after a sprint, ignore these unless the failure
mode changes:

- `BreakpointManager_test.portInBreakpoint`
- `AtomicStepping_Test.RunUntilNextScreenPixel_FromFrameStart`
- `AtomicStepping_Test.CompoundStepping_ScanlineThenFrame`
- `KeyboardInjection_Integration_test.TypeNumbers_In48KBASIC`
- `KeyboardInjection_Integration_test.Type48K_PrintHello`
- `TRDOSIntegration_test.RealExecution_MinimalProof_JumpToTRDOSEntry`
- `WD1793_Integration_Test.TRDOS_FORMAT_FullOperation`
- `WD1793_Test.FSM_CMD_Write_Track` (SIGABRT)
- `DebugKeyboardManager_test.ReleaseKey_ClearsKey` (segfault)

All relate to full-emulator integration paths (timing-sensitive keyboard
injection, TR-DOS boot, FDC state machine, breakpoint port logic). None
overlap with the TTD / GDB surface area.
