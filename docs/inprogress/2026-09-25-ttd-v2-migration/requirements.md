# TTD v2 requirements

> **Note (2026-10-02).** v2 is built as a new engine, `ttd::TimeTravelEngine`,
> next to v1 and checked against it byte for byte
> ([engine-approach-and-naming.md](engine-approach-and-naming.md)). Where this
> document and [engine-decisions.md](engine-decisions.md) (D1–D33) disagree, the
> decisions win. The phases named here are those of the current
> [roadmap](README.md#2-phases); §7 traces each requirement to them.

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
- **FR-5 (MUST)** Memory regions of any size, with no fixed cap per region:
  at least 4 MB machine RAM, and each device region as large as its device
  needs (NeoGS RAM and flash: about 4.5 MB), with no per-page-index limitation
  (the v1 page-255 case itself was fixed in Phase 0, Step 1, 2026-09-27).
  Corrected 2026-10-02 (D27): this read "+ 1 MB device RAM per device".

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
  the same semantics as v1 (resume-from-here stops truncating once branches
  land: FR-24).
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

### 2.5 Replay isolation

TTD v1 records every IN result of the main CPU (port-read journal, branch
`ttd-o1-journals`, [ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md)):
on the classic machines a replay already feeds the CPU recorded data and needs
no media file or host device. Two parts of a fully sealed replay are v2 work:

- **FR-20 (MUST)** **No writes outside the session while replaying.** While
  recorded history is re-executed (a seek, a reverse query, running forward
  through the past), no device writes to anything outside the emulated machine:
  disk images, SD and HDD images, the media manager's session write maps and
  write-through, flash persistence (NeoGS flash, Profi / ZX-Evo stores), CMOS
  and RTC stores. Writes the replayed program makes still reach the devices'
  in-machine state (their buffers, their registers), and the device's own
  output is logged as a fact; the host-side medium is never touched. One gate,
  keyed by the replay state (`EmulatorContext::ttdReplayActive` and the
  re-execution of the past), in the media layer (block devices, disk images,
  the session write map, flash persist) - not a flag each controller checks
  on its own. A test replays history that writes a disk and proves the image
  file and the session write map unchanged.
- **FR-21 (MUST)** **Isolation beyond IN.** Configurations whose outside world
  reaches memory or the CPU without an IN are journaled at the same boundary:
  DMA transfers into RAM (TSConf, ZX Next, NeoGS ZX-DMA - its host memory reads
  are served by the card), and an interrupt vector supplied by a device rather
  than the floating bus (to be checked for TSConf and Sprinter: the current
  classic clones do not drive IM2 vectors from outside). Until then the port-read
  journal is off on these configurations and the session reports why
  (`port_journal_off_reason`).

### 2.6 Branches and forks (proposed 2026-09-29)

From the [model what-if and branched history design](../2026-09-29-model-what-if/design.md)
(§8 checks it against this document). v2 does not build branches; it must not
close the door on them:

- **FR-22 (SHOULD)** The storage keeps sharing possible: a checkpoint may
  reference pieces and blobs of an earlier checkpoint that is not its
  predecessor (a branch's first checkpoint shares its fork point's). Reference
  counts, chain caps and copy-on-write reference blocks (Phase 1) already allow it;
  nothing in Phases 1–4 may assume a strictly linear timeline.
- **FR-23 (SHOULD)** The session file (Phase 4, Step 2) reserves stream ids for branch data
  (branch table, non-trunk checkpoints and events, the parent link of a forked
  session) and keeps the rule that readers skip unknown streams, so a reader
  without branch support opens a branched file as its trunk.
- **FR-24 (MUST)** No truncation (user, 2026-09-29): `ResumeRecordingFrom` keeps
  every route (QR-8) and starts a branch instead of deleting the future; history
  is discarded only by an explicit branch delete; stopping TTD leaves the
  machine free-running. Until branches land (PLAN #76 W1) v1 keeps truncating;
  nothing in Phases 1–4 may add new truncating paths.

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
  from the Phase 0, Step 2 harness); if PR-5 fails there, the conditional step Phase 0, Step 4 of the
  trajectory adds them. Until then v2 keeps one checkpoint per frame.
  **Measured** (Phase 0, Step 2): seek p99 ≤ 3.8 ms on every configuration of
  the full matrix, 3.46 ms on the heaviest 10-minute session, so Phase 0,
  Step 4 was dropped ([v0b-benchmark-results.md](v0b-benchmark-results.md) §3).

### 3.3 Storage efficiency

- **PR-9 (MUST)** v2 is **more efficient than v1** on every reference workload
  in each of: bytes per frame in memory, bytes per frame in the file, capture
  time, restore time. No metric may regress by more than 5%; the sum must
  improve. *Tightened 2026-10-02 by [D33](engine-decisions.md#g-quality-bar-after-every-phase):*
  bytes in memory, bytes in the file and counted capture work may not regress
  at all, in any case, for the same history kept; restore time may be slower
  than v1's, within PR-5 (and within 25% of E4's estimates once restore-only-
  differences is in).
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
   showing no metric regressing more than 5% and the overall improvement
   (*tightened by D33*: no regression in bytes or capture work at all; seek
   time within PR-5).
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

## 7. Traceability to the roadmap

Updated 2026-10-02 for the engine roadmap ([README §2](README.md#2-phases)).
Until then this table followed the in-place phases; [README §5](README.md#5-former-step-names)
maps their steps to the current ones. Rows marked *(assigned 2026-10-02)* give
a phase to requirements no phase covered before ([engine-decisions.md §D](engine-decisions.md#d-documents)).

| Phase / step ([roadmap](README.md#2-phases)) | Requirements it delivers |
|---|---|
| Phase 0, Step 1 — Make v1 honest | FR-3 (first version), FR-9 (v1), FR-18, QR-4 (current format) |
| Phase 0, Step 2 — Benchmark harness | BR-1…BR-9 with v1 as first engine; PR-5 measured on turbo configurations |
| Phase 0, Step 3 — Merge the feature branches (GS, MoonSound, Profi) | FR-1, FR-2, FR-4 for those devices |
| Phase 0, Step 4 — Checkpoints inside a frame (dropped) | PR-5 on turbo configurations (met without it: p99 ≤ 3.8 ms, [v0b §3](v0b-benchmark-results.md#3-the-phase-0-step-4-question-pr-5)) |
| Phase 1, Step 1 — Engine skeleton and verification | BR-1 (the engine as a second engine in the harness), D33 oracle; positions carry a branch (FR-22, FR-24) |
| Phase 1, Step 2 — Piece store | PR-3, PR-4, the memory part of PR-9; I-6 measured here: the share of the per-piece CRC in capture and seek *(assigned 2026-10-02)* |
| Phase 1, Step 3 — Regions and copy-on-write reference table | FR-5, FR-22; parent link for FR-23 |
| Phase 1, Step 4 — Live capture next to v1 | PR-4, PR-10 (memory part); PR-1 / PR-2 measured for the engine's capture (BM-1, BM-2) *(assigned 2026-10-02)* |
| Phase 1, Step 5 — Restore only the pieces that differ | PR-6, PR-7 in memory *(assigned 2026-10-02)*; PR-5 |
| Phase 1, Step 6 — Device memory as regions | FR-1, FR-2 and FR-5 for device memory |
| Phase 2 — Device state with versions | FR-2, FR-4, FR-7, FR-19, PR-10 |
| Phase 3, Steps 1–3 — Event stream, two replay modes, several CPUs | FR-10 (journals), FR-21 |
| Phase 3, Step 4 — Configuration fingerprint, media versions | FR-10 (fingerprint), FR-14 *(assigned 2026-10-02: open in another configuration, report what is not exact)* |
| Phase 3, Steps 5–6 — Emulated clock, no writes outside the session | FR-20; the emulated time base QR-3 needs |
| Phase 3, Step 7 — Write journal as a derived index | FR-10 (write journal); its default retention feeds PR-11 |
| Phase 4, Step 1 — Integrity and versioning decided | FR-11, FR-12; I-4 failure model of a partially damaged session; I-6 decided (keep or drop the in-memory CRC) *(assigned 2026-10-02)* |
| Phase 4, Step 2 — Written as it records | FR-13, FR-23, QR-2, QR-3 *(assigned 2026-10-02: deterministic file contents)*, PR-11 *(assigned 2026-10-02: measured on the file)* |
| Phase 4, Step 3 — Memory as a cache of the file | FR-15, FR-16, PR-7 in disk mode *(assigned 2026-10-02)*, PR-8 |
| Phase 4, Steps 5–6 — v1 files, format description and analyzer | PR-12, QR-4 (new format), QR-5 |
| Phase 5, Step 1 — The emulator runs on the engine | QR-8, FR-17; FR-8 through every surface *(assigned 2026-10-02)*; PR-1 / PR-2 met with v1 out of the frame *(assigned 2026-10-02)*; FR-7 and a partially damaged session (I-4) shown on every surface *(assigned 2026-10-02)* |
| Phase 5, Step 2 — History never cut short | FR-24; FR-9 on the engine: resume from here starts an exact branch *(assigned 2026-10-02)* |
| Phase 6 — Cleanup | QR-9, acceptance |
| Every step | QR-1, QR-6, QR-7, D33 |

The benchmark harness (§5) starts in Phase 0, Step 2 with v1 as its first engine, so every
later step is measured against the same baseline.
