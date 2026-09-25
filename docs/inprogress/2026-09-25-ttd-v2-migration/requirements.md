# TTD v2 requirements

Requirements, quality criteria and acceptance criteria for TTD v2. The design
that meets them is [target-architecture.md](target-architecture.md); the order
of work is [migration-trajectory.md](migration-trajectory.md); the v1 baseline
they are measured against is [current-state.md](current-state.md).

Each requirement has an id so tests, benchmarks and reviews can refer to it.
**MUST** = required for v2 acceptance; **SHOULD** = target, a miss needs a
written justification; **MAY** = allowed, not required.

---

## 1. Definitions

| Term | Definition |
|---|---|
| **Frame overhead** | `(frame time with TTD recording − frame time with TTD off) / frame time with TTD off`, same machine, same configuration, same workload, Release build, **reported separately for each recording mode** (no journal / journal / journal + coverage). "Frame time" is the full emulated frame: CPU, devices, sound render, video render |
| **Position** | Any of: a frame number plus a T-state offset inside that frame; a bookmark (named time mark); a recorded external event (tape/disk/reset marker) |
| **Seek** | From any state of the emulator (live, paused, at another position) to a position, until the machine is exactly the recorded machine at that position and ready to run |
| **Exact** | Every byte of CPU state, port latches, memory regions and device state equal to what a straight run produced at that position |
| **Configuration** | Machine model + RAM size + ROM set + set of attached peripherals + the emulator settings that exact replay depends on (frame length, CPU clock / turbo, audio core rate, decimator quality) |
| **Reference workloads** | The benchmark workloads of §5.3: recorded input sessions that run identically on every engine version |
| **Engine version** | A TTD implementation under test: v1 (today), v2, and later versions |

## 2. Functional requirements

### 2.1 Coverage

- **FR-1 (MUST)** v2 supports **every creatable clone configuration and every
  peripheral** through the device registry (small state) and memory regions
  plus the file container (large memory). No configuration may record while
  silently leaving any state out.
- **FR-2 (MUST)** Every device and every model latch that affects emulation is
  declared and serialized: port latches not in the common chipset struct,
  device registers, device-owned RAM (GeneralSound, NeoGS, MoonSound wave SRAM),
  RTC/CMOS, keyboard matrix, joystick, mouse. A declared item without a
  serializer refuses recording (as today); in addition, **an undeclared item
  must be caught by a test** (FR-3).
- **FR-3 (MUST)** A generic **state-completeness test** per creatable model: for
  every port the model's decoder handles, write a non-default value after a
  checkpoint, seek back, and require that paging, memory map, device state and
  a full machine hash equal the recording. The same test covers **devices
  attached, detached or replaced at runtime** (FR-4): record with a device, change
  the device set after a checkpoint, seek back, and require the recorded device
  set and state.
- **FR-4 (MUST)** Devices can be attached, detached or replaced at runtime (GS
  card personality switch, sound device change); the change is recorded and a
  seek across it restores the device set of the target position, or reports
  that it cannot.
- **FR-5 (MUST)** Memory regions of any size up to at least 4 MB machine RAM +
  1 MB device RAM per device, with no per-page-index limitation (fixes the v1
  page-255 case).

### 2.2 Restore and seek

- **FR-6 (MUST)** Seek to any position (frame + T-state, bookmark, event marker)
  inside the recorded range is exact.
- **FR-7 (MUST)** A restore that cannot be exact (missing device state, layout
  version mismatch, damaged data, configuration fingerprint mismatch for
  replay-based operations) **reports it** on every surface: C++ result, WebAPI,
  MCP, CLI, Python, Lua, DeZog, GDB stub, Qt. Silent fallback to live state or
  zero-filled memory is forbidden.
- **FR-8 (MUST)** Step back / forward (frame, instruction, T-state), reverse
  continue, find-last-access and resume-recording-from-here keep working with
  the same semantics as v1.
- **FR-9 (MUST)** After *resume recording from here*, the new recording is
  exact from that point (the v1 stale-cache risk must be covered by a test that
  compares memory).

### 2.3 Persistence

- **FR-10 (MUST)** A session file contains everything needed to restore and
  replay: machine and device state, memory regions, input journal, external
  events, bookmarks, write journal and coverage (optional), configuration
  fingerprint, region and device tables.
- **FR-11 (MUST)** Files are versioned, and the compatibility rules are
  written down and enforced by fixtures. The exact rules (which old files a
  reader opens, what it may skip, how device layout changes are handled) are
  **open**: [integrity-and-versioning.md](integrity-and-versioning.md) §2.
- **FR-12 (MUST)** Damaged data is detected and reported with its location, and
  never restored silently. The mechanism (checksum coverage, granularity,
  eager vs lazy, failure behaviour) is **open**:
  [integrity-and-versioning.md](integrity-and-versioning.md) §1.
- **FR-13 (MUST)** Crash safety in disk mode: after a crash or power loss the
  file opens up to the last complete unit of data (mechanism: same
  investigation, I-5).
- **FR-14 (SHOULD)** Loading a session into a different configuration opens it
  for inspection (state, memory, disassembly) and reports which operations are
  not exact there.

### 2.4 Resource control

- **FR-15 (MUST)** A configurable memory budget bounds TTD's process memory.
  Reaching it either spills to disk (disk mode) or releases the oldest history
  (memory mode); the earliest reachable position is reported.
- **FR-16 (MUST)** Reported memory use is the real total (page and region
  pieces, device blobs, journals, coverage, caches), within ±5%.
- **FR-17 (MUST)** Turning TTD, debug mode or the write journal on or off
  during a recording never corrupts the recorded history.
- **FR-18 (MUST)** Enabling the TTD feature without recording does not change
  emulation (today it disables fast tape and fast disk loading — perf review
  F3); only an active recording may alter emulator behaviour, and only where
  documented.
- **FR-19 (MUST)** **Capture happens at a synchronized frame boundary.** Devices
  that run behind the CPU and catch up lazily (GeneralSound's Z80, the TurboSound
  / TSFM render cursor, MoonSound) are brought to the frame boundary before
  their state is captured, and are left consistent with it after a restore.
  Today this holds by call order (`MainLoop::OnFrameEnd` runs the sound frame end
  before `OnFrameBoundary`); v2 makes it an explicit, tested rule so a refactor
  of the frame-end sequence cannot break it.

## 3. Performance requirements

All numbers on the reference host class (Apple Silicon or equivalent x86-64,
Release build), reference workloads of §5.3.

### 3.1 Recording overhead

- **PR-1 (MUST)** Frame overhead **≤ 15%** in every mode (without and with the
  write journal and coverage index), for every reference configuration. For
  comparison, v1 measures +27% (no journal) and +28% (journal) on Pentagon +
  TSFM.
- **PR-2 (SHOULD)** Frame overhead ≤ 10% without the write journal.
- **PR-3 (MUST)** No periodic spikes: the p99 per-frame capture time is at most
  3× the median for the same workload (v1 re-stores all RAM every 50 frames:
  5.4 ms on a 4 MB machine).
- **PR-4 (MUST)** **Cost follows change**: with no memory written in a frame,
  capture time on a 4 MB configuration is within 20% of a 128 KB configuration
  with the same devices. With N dirty 4 KB pieces, capture time grows with N,
  not with installed memory.

### 3.2 Seek and restore

- **PR-5 (MUST)** Seek to any position (frame + T-state offset, bookmark or
  event marker) completes in **≤ 5 ms at p99**, measured over random positions
  of a 10-minute session, for every reference configuration. **Lower is
  better**; results are always reported, not only pass/fail.
- **PR-6 (SHOULD)** Seek p50 ≤ 1 ms; frame-aligned restore (no replay) p99 ≤ 2 ms.
- **PR-7 (MUST)** Seek time does not grow with session length (flat from 1 to
  60 minutes within measurement noise), in memory mode, and in disk mode for
  positions already in memory.
- **PR-8 (SHOULD)** Disk mode, position evicted to disk: p99 ≤ 20 ms on an SSD.
- Design implication: a seek to a T-state offset replays up to one frame. On
  configurations where one frame of emulation alone approaches the budget
  (turbo models, heavy device sets) PR-5 may require checkpoints inside a frame.
  Whether that is needed is **measured first** (BM-5 on turbo configurations,
  from the V0b harness); if PR-5 fails there, the conditional step V1b of the
  trajectory adds them. Until then v2 keeps one checkpoint per frame.

### 3.3 Storage efficiency

- **PR-9 (MUST)** v2 is **more efficient than v1** on every reference workload
  in each of: bytes per frame in memory, bytes per frame in the file, capture
  time, restore time. No metric may regress by more than 5%; the sum must
  improve.
- **PR-10 (MUST)** Unchanged state costs (almost) nothing: a frame in which
  nothing changed costs ≤ 64 bytes of checkpoint data in the file, regardless
  of configuration (v1: device blobs alone are 309–646 B per frame).
- **PR-11 (SHOULD)** Pentagon 128 + AY idle ≤ 50 MB per hour in the file
  without the write journal; 4 MB ZX-Evo + GS + MoonSound active ≤ 1 GB per hour.
- **PR-12 (MUST)** Opening a session to first seek ≤ 2 s for a 1 GB file
  (cue-table driven; payloads load lazily).

## 4. Quality requirements

- **QR-1 (MUST)** Zero compiler warnings on all supported compilers; ASan and
  UBSan clean on the full TTD test set.
- **QR-2 (MUST)** Cross-platform byte-identical files: a session recorded on
  macOS, Linux or Windows loads and restores exactly on the others.
- **QR-3 (MUST)** Deterministic output: recording the same workload twice
  produces files that differ only in timestamps and session UUID.
- **QR-4 (MUST)** Corruption robustness: a fuzz test (random bit flips,
  truncation, oversized length fields in every stream) never crashes, never
  allocates unbounded memory, and reports every flip that the CRCs cover.
- **QR-5 (MUST)** One source of truth for the format: `ttd.ksy`, the C++
  reader/writer and the Python analyzer agree; the analyzer's `validate`
  decodes every stream and checks every CRC; a conformance test runs the
  analyzer on files written by the C++ writer.
- **QR-6 (MUST)** A fixture corpus per model family (Pentagon, 48K, 128K, +3,
  Scorpion, ProfROM Scorpion, ATM, ZX-Evo, Profi) and per large peripheral (GS,
  MoonSound, TSFM), recorded by `record_fixtures.py` from the project root, gated
  by a corpus test that restores and replays each exactly, comparing memory as
  well as CPU and devices.
- **QR-7 (MUST)** Tests follow the project rules: no sleeps, under 50 ms per
  test unless justified in a comment, scratch files in `scratch/` with unique
  names.
- **QR-8 (MUST)** The public API keeps every v1 WebAPI route, CLI command,
  Python/Lua binding and MCP action; changes are additive. New status fields are
  documented in the API reference and `.recipe/`.
- **QR-9 (MUST)** Documentation truth: the parent TDD, this folder and the
  code comments describe the same design; stale v1 statements are removed.

## 5. Benchmarks and measurement

The benchmark suite is part of v2, not an afterthought: it proves PR-1…PR-12,
and it keeps comparing v1, v2 and future versions.

### 5.1 Comparable across engine versions

The harness is the existing `core-benchmarks` target (Google Benchmark,
`core/benchmarks/debugger/ttd/`), extended with an engine selector, the
configuration matrix and JSON output (`--benchmark_format=json`), plus a
comparison script under `tools/verification/`.

- **BR-1 (MUST)** Every benchmark runs against an **engine version** chosen at
  run time or build time (v1, v2, later). v1 stays buildable as a baseline
  engine until v2 is accepted; after that, v1 results are kept as frozen
  reference data produced by the same harness.
- **BR-2 (MUST)** Workloads are **replayable recordings** (start snapshot +
  input journal + configuration), not live runs, so every engine version
  executes exactly the same emulation.
- **BR-3 (MUST)** Results are written as machine-readable JSON: engine version,
  git commit, build type, host, configuration, workload, metrics. A comparison
  script prints a table of any two or more result files, with differences in
  percent.

### 5.2 Metrics

| Id | Metric | Reported as |
|---|---|---|
| BM-1 | Frame overhead (PR-1) | percent, per mode (off / no journal / journal / journal + coverage) |
| BM-2 | Capture time per frame | p50 / p99 / max µs |
| BM-3 | Captured stream size | bytes per frame, **split by stream**: machine RAM, each device region, device blobs, references, checkpoint core, write journal, input journal, coverage |
| BM-4 | Resident memory | bytes per frame of history, and total vs the budget |
| BM-5 | Seek latency to random positions | p50 / p95 / p99 / max, split into restore and replay; frame-aligned, T-state offset, bookmark |
| BM-6 | Restore time by component | CPU + chipset, devices, memory regions |
| BM-7 | Session save and load | seconds per GB; time to first seek |
| BM-8 | Cost vs dirty pieces | capture time at 0, 1, 4, 16, 64 dirty 4 KB pieces, per memory size |

### 5.3 Configuration matrix

- **BR-4 (MUST)** Base configurations: 48K, 128K, Pentagon 128/512/1024,
  +3, Scorpion 256, ProfROM Scorpion, ATM Turbo 2+, ZX-Evo (4 MB), Profi 1024,
  each with its default peripherals only.
- **BR-5 (MUST)** Peripheral sets on top of a base (at least Pentagon 128 and
  ZX-Evo): none; AY; TurboSound; TSFM; GS (128 KB and 512 KB); MoonSound; Covox;
  BetaDisk active; mouse; **any combination selectable** from the command line.
- **BR-6 (MUST)** Workload kinds per configuration: idle (BASIC prompt),
  game, demo with heavy memory churn, music player per sound device, disk
  loading. At least one workload per large peripheral that writes its RAM
  (GS module upload, MoonSound sample upload).

### 5.4 Running

- **BR-7 (MUST)** A CI-sized subset (≤ 2 minutes) runs with loose regression
  gates on every change to TTD code; the full matrix runs on demand.
- **BR-8 (MUST)** Gates compare against stored baselines with explicit
  tolerances and are robust to machine load (min-of-N timing, byte counts
  exact); the current load-sensitive `TTD_Capture_Cost_Gate_Test` is replaced.
- **BR-9 (SHOULD)** Results for every accepted version are kept in the repo
  (`docs/` or `testdata/`), so trends are visible over time.

## 6. Acceptance criteria

v2 is accepted when all of the following hold, with evidence linked from this
folder's `DONE.md`:

1. **Coverage**: the state-completeness test (FR-3) passes for every creatable
   model; the contract test covers every registered device, including sound
   devices; GS, MoonSound and Profi RTC are restored exactly.
2. **Exactness**: the corpus test (QR-6) restores and replays every fixture
   exactly, comparing memory, CPU and devices; resume-from-here is covered.
3. **Performance**: benchmark results for the full matrix meet PR-1, PR-3,
   PR-4, PR-5, PR-7, PR-9, PR-10 and PR-12; SHOULD targets are reported with
   justification where missed.
4. **Comparison**: a published comparison table v1 vs v2 for the matrix (BR-3),
   showing no metric regressing more than 5% and the overall improvement.
5. **Robustness**: the fuzz test (QR-4) passes; ASan/UBSan clean (QR-1); a
   kill-during-recording test in disk mode passes (FR-13).
6. **Persistence**: a file written by the first v2 release still loads in the
   accepted version (FR-11); cross-platform check (QR-2) done.
7. **Surfaces**: degraded-restore reporting visible on WebAPI, MCP, CLI,
   Python, Lua, DeZog and Qt (FR-7); memory reporting accurate (FR-16).
8. **Gates**: the full `core-tests` suite passes, zero warnings, the CI
   benchmark subset (BR-7) is in place.
9. **Documentation**: parent TDD, `ttd.ksy`, analyzer, `testdata/ttd/README.md`
   and `.recipe/` updated (QR-5, QR-9).

## 7. Traceability to the trajectory

| Step ([migration-trajectory.md](migration-trajectory.md)) | Requirements it delivers |
|---|---|
| V0 make v1 honest | FR-3 (first version), FR-9, FR-18, QR-4 (current format) |
| V0b benchmark harness | BR-1…BR-9 with v1 as first engine; PR-5 measured on turbo configurations |
| V1b intra-frame checkpoints (conditional) | PR-5 on turbo configurations |
| V1 memory regions, cost ∝ change | FR-5, PR-3, PR-4, part of PR-9/PR-10 |
| GS / MoonSound / Profi merges | FR-1, FR-2, FR-4 for those devices |
| V2 device state | FR-2, FR-4, FR-7, FR-19, PR-10 |
| V3 determinism inputs | FR-10 (journals, fingerprint), FR-14 |
| V4 memory budget | FR-15, FR-16, FR-17 |
| V5 container + disk mode | FR-11, FR-12, FR-13, PR-8, PR-12, QR-2, QR-5 |
| V6 cleanup | QR-9, acceptance |

The benchmark harness (§5) starts in V0b with v1 as its first engine, so every
later step is measured against the same baseline.
