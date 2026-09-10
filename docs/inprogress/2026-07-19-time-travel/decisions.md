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

## Session serialization (.ttd format)

Established in Phase S1 (2026-07-23). Applies forward to every change
that touches the on-disk recording format. See
[`phase-S1-session-serialization.md`](phase-S1-session-serialization.md)
for the full Phase S1 write-up.

### Format choice: Kaitai Struct (.ksy) is canonical

The `.ttd` binary format is specified by a Kaitai Struct YAML schema at
`core/src/debugger/ttd/ttd.ksy`. Kaitai Struct was selected over DFDL,
Protobuf, Msgpack, and FlatBuffers. Rationale recorded in phase-S1 §2.
In short: Kaitai is the standard in the retro/emulator community
(NES/SNES/GBA/DS formats all use it), MIT-licensed, generates parsers
for Python/C++/Java/JS/Go/Rust from a single schema, and keeps
mmap-friendly POD reads.

### Single source of truth — three parsers must conform

The C++ writer (`SerializeSession`), the C++ reader (`DeserializeSession`),
and the hand-written Python parser (`tools/verification/ttd-analyzer/src/
ttd_format.py`) all conform to the same `.ksy`. **When the format changes:**

1. Edit `ttd.ksy` and bump `meta.schema-version` (additive-only within
   a major version).
2. Edit the matching C++ constants in `ttd_dump_format.h`.
3. Edit the writer/reader in `timetravelmanager.cpp`.
4. Edit the hand-written Python parser in `ttd_format.py`.
5. Regenerate the fixture (run
   `TTD_Dump_Format_Test.DISABLED_WriteFixtureFile_ForPythonConformance`)
   and re-run the Python analyzer against it.

The disabled-test fixture is the conformance safety net. **Never skip
this step when changing the format.**

### Versioning policy

- Schema version lives in **two places** that must always agree:
  - `meta.schema-version` in `ttd.ksy`
  - the `schema_version` field in every `.ttd` file header
- `MAX_SUPPORTED_SCHEMA_VERSION` in C++ and Python caps the version a
  reader will accept. Unknown future versions are refused with a clear
  error, NOT silently interpreted.
- Within major version N, the format is **additive-only**: new fields
  land at the end of a record, never reorder or remove existing fields.
- Removing or reordering fields requires bumping the major version and
  rejecting old files cleanly. v1 readers do not attempt to interpret v2
  files.
- `cpu_state_size` / `chipset_state_size` in the header act as a
  secondary drift detector: a size mismatch means the producer was built
  from a different source revision; the reader refuses rather than risk
  silent misparse.

### Universal capability — no producer-specific serialization

`TimeTravelManager::SerializeSession(std::ostream&, std::string& err)` is
the single serialization entry point. It takes a generic `std::ostream`,
so any producer (core test, automation CLI, WebAPI wrapper) calls the
same code with a different stream type:

- `std::ostringstream` for in-process round-trip in tests.
- `std::ofstream` for the CLI's `ttd dump <path>` (future).
- `std::ostringstream` for the WebAPI's `POST /ttd/dump` (future; bytes
  returned as a download).

No producer-specific logic. Same bytes regardless of caller. This was
the explicit user requirement: "the most universal strategy suitable
both for core standalone and for automation/webapi wrapped mode".

### POD-with-padding: declare padding explicitly in the schema

The C++ `TTDCpuState` and `TTDChipsetState` structs are POD with natural
alignment — the writer emits `sizeof(Struct)` bytes verbatim, including
compiler-inserted padding. Schema-driven parsers must declare these
padding bytes explicitly (Kaitai convention: `_pad_*` fields named with
a leading underscore are "consumed but not exported").

The padding layout for `TTDCpuState` is documented inline in `ttd.ksy`
and in `phase-S1-session-serialization.md` §4. **When adding a field to
any POD struct that's serialized verbatim**, re-derive the padding
layout, update the schema, and confirm with a fixture regeneration.

A future hardening pass should add `static_assert(offsetof(...))` checks
for every padded field so the day a new field shifts the layout, the
build fails before the parser does.

### Hand-written parser default, Kaitai-generated as drop-in

The Python analyzer ships a hand-written parser (`ttd_format.py`) using
only the standard library. This is the default because the schema is
small and changes infrequently. The hand-written parser has the same
API surface a Kaitai-generated parser would have — to switch,
regenerate via `kaitai-struct-compiler ttd.ksy -t python` and drop the
hand-written file. No caller changes required.

### Analyzer exit codes (CI integration)

The analyzer CLI (`tools/verification/ttd-analyzer/src/main.py`)
standardizes exit codes so `validate` can be wired as a CI gate:

| Code | Meaning |
|------|---------|
| 0    | Success (or no integrity errors for `validate`) |
| 1    | Integrity errors found (only for `validate`) |
| 2    | File could not be parsed (bad magic, truncated, unsupported schema) |
| 3    | Bad CLI arguments |
| 4    | I/O error (could not write output file) |

### Side-phase numbering convention (S1, S2, ...)

Linear phase numbers (Phase 1, 2, 3, 4) imply blocking dependencies.
"Side-phases" — orthogonal capabilities that don't fit the linear
progression (e.g., session serialization, which is independent of the
UI work in Phase 3 and the reverse-search work in Phase 4) — use the
`S` prefix. This keeps the linear numbering stable when a side-capability
lands between two linear phases.

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
