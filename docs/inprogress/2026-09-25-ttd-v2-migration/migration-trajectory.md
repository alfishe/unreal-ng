# TTD v1 → v2 migration trajectory

How to get from [current-state.md](current-state.md) to
[target-architecture.md](target-architecture.md) while landing the three feature
branches ([branch-merge-strategy.md](branch-merge-strategy.md)). Each step is
shippable on its own: master builds, all tests pass, and TTD works after every
step.

---

## 1. The question: merge the branches first, or TTD v2 first?

The starting proposal was: finalize `profi`, `generalsound` and `moonsound`,
merge them, and only then do the full TTD v2 migration. Three options were
weighed:

| | A. All branches, then v2 | **B. Branches, with memory regions pulled forward** | C. v2 first, then rebase branches |
|---|---|---|---|
| Sequence | profi → GS → MS → V0…V6 | V0 → profi → V1 → GS → MS → V2…V6 | V0…V6 → rebase all three |
| GS on master | copies up to 512 KB of card RAM into every checkpoint until v2 lands | stores only changed 4 KB pieces from day one | same as B, but months later |
| MoonSound TTD | merged incomplete (wave SRAM not captured); Tier B retrofitted later | Tier B built on regions on the branch, merged complete | same as B |
| Branch drift | lowest | low (V0 + V1 are small) | **highest**: branches stay open through all of v2 |
| Rework | GS/MS TTD code written for v1 blobs, then rewritten | written once | written once, but rebasing hurts |
| Risk | low per step; the 512 KB/frame regression is real | low | high |

**Recommendation: B.** It is A with one change: the *memory region* part of v2
(small, well bounded) lands on master before GS and MS merge, because both of
them need it and neither can do TTD properly without it. Everything else in v2
(device table, determinism inputs, memory budget, new file container) comes
after all three branches are in, as originally proposed. Doing it after means
the device-state rules are tightened once, with every device present.

## 2. Steps

```
V0  make v1 honest                (master)
V0b benchmark harness, v1 as first engine   (parallel with V0 and the profi merge)
    profi merge
V1  memory regions + cost ∝ change (master)
V1b checkpoints inside a frame    (only if V0b shows PR-5 failing on turbo models)
    generalsound merge (GS RAM as a region)
    moonsound finish on branch + merge (wave SRAM as a region)
V2  device state v2
V3  determinism inputs
V4  memory budget
V5  container v2 + disk mode       <- the format cut; versioned from here on
V6  cleanup
```

Relative size: S = a focused day or two of agent work, M = several days,
L = a week or more. Every step ends with the standard gate: build, full
`core-tests`, zero warnings, and a re-record of `testdata/ttd` whenever the file
content changes ([testdata/ttd/README.md](../../../testdata/ttd/README.md)).

Each step lands on master as an ordinary series of commits and is all-or-nothing:
if a problem is found after the step has landed, the step is reverted as a
whole (and the fixtures re-recorded), not left half-applied. Until V5 the file
format is amended in place, so a revert needs no compatibility work.

### V0 — make v1 honest (S–M, master, before any merge)

Fix what is wrong today, so the later steps build on a correct base and have
tests that would catch regressions.

- **Step 0 of the merge strategy**: the `PeripheralId` table and the
  notification enum ([branch-merge-strategy.md](branch-merge-strategy.md) §2).
- **Capture gap**: RAM page 255 on 4 MB machines (widen the page cache
  sentinel).
- **Suspected bugs, each first reproduced by a test**: stale `_prevPageCache`
  after resume (B1), stale write journal after load (B2), empty-journal find-last
  (B3), non-atomic failed load (B5), unbounded sizes in the journal/coverage
  loaders.
- **Feature-flag side effect** (perf review F3): enabling the `timetravel`
  feature without recording disables fast tape / fast disk; move the override to
  recording start ([requirements.md](requirements.md) FR-18).
- **Analyzer fixes**: Python knows flag bit 3 (bookmarks). (Done 2026-09-25:
  the analyzer compares the stored piece CRC, and the false "the C++ writer
  stores 0" comments in `ttd_format.py`, `ttd.ksy` and `timetravelmanager.cpp`
  are corrected.) C++ integrity behaviour (eager check at load,
  error vs zero-fill on mismatch) waits for the investigation
  ([integrity-and-versioning.md](integrity-and-versioning.md)).
- **Tests**: `TTD_Corpus_Test` compares RAM too, and gets a resume-then-compare
  case; a test for page 255; the generic state-completeness test
  ([requirements.md](requirements.md) FR-3).
- **Comments**: remove the stale ones listed in current-state §10.

Exit: the bit-flip experiment catches every page-payload flip; the new tests
fail on the old code and pass on the new.

### V0b — benchmark harness (M, parallel with V0)

- Extend `core-benchmarks` (`core/benchmarks/debugger/ttd/`) with an engine
  selector (v1 first), the configuration matrix and replayable workloads
  (requirements §5), JSON output and a comparison script.
- Replace the load-sensitive `TTD_Capture_Cost_Gate_Test` with the CI-sized
  subset (BR-7, BR-8).
- Measure seek (BM-5) on turbo configurations (ATM, Scorpion turbo) — this
  decides whether V1b is needed.

Exit: a v1 baseline JSON for the full matrix is stored; the comparison script
prints it against itself with zero differences in byte counts.

### Merge `profi` (S)

Checklist in [branch-merge-strategy.md](branch-merge-strategy.md) §3.1. Clean
merge; config fixes (`MoonSound=0`, explicit `GSType`).

### V1 — memory regions, cost proportional to change (M, master)

- `TTDRegionDesc` + region table in the session header; machine RAM becomes
  region 0; device regions register through the registry.
- **Per-piece chain cap** replaces global key frames for RAM.
- `_prevPageCache` refreshed only for dirty pieces, rebuilt explicitly on
  seek/resume.
- Reference table in copy-on-write blocks of 16 pages.
- Benchmarks: `OnFrameBoundary` on ATM3 (4 MB) with 0/1/16 dirty pages must no
  longer scale with installed RAM; no per-50-frame spike. The capture-cost gate
  counts region pieces and device blobs, not only machine RAM.
- Format: still "amended in place" (the file is not yet versioned); fixtures
  re-recorded.

Exit: 4 MB ZX-Evo frame capture cost within noise of a 128K machine for the same
dirty-page count.

### V1b — checkpoints inside a frame (conditional, M)

Only if V0b shows seek p99 above 5 ms on a turbo or heavy configuration
(requirements PR-5). Adds extra checkpoints at fixed T-state intervals inside
long frames, so a seek replays at most one interval. Skipped otherwise.

### Merge `generalsound` (M)

Checklist §3.2. The one TTD change on the branch: GS RAM and the lightweight
upload store become regions (V1 API), and the missing z80ex fields
(`noint_once`, `reset_PV_on_int`, `int_vector_req`; perf review G6) go into the
GS blob. Fixtures re-recorded (they now include a GS card).

### Finish and merge `moonsound` (L, mostly on the branch)

Checklist §3.3: strip dead weight, unify port claiming with master's
self-decoding API, automation (P2-2, designed first), Tier B wave SRAM as a
region, GS overlap resolution.

### V2 — device state v2 (M)

- Device table in the session header; per-blob layout versions checked in
  release builds.
- Unchanged blobs shared in memory and marked "same as previous" in the file.
- The restore report is enforced: a degraded restore is reported through WebAPI
  (`ttd/seek`, `ttd/status`), MCP, CLI, Python/Lua, DeZog and the Qt widget.
- Sound devices (TurboSound slot, GS, MoonSound) go through the declare /
  implement contract; `UpdatePeripheral` becomes part of the registry API.

Exit: every device registered on every creatable model appears in the contract
test; a deliberately corrupted blob produces a degraded result on every surface.

### V3 — determinism inputs (M)

- Configuration fingerprint in the header (frame length, CPU clock / turbo,
  audio core rate, decimator quality, `soundhq`/`screenhq`, ROM signature,
  device table); replay-type operations report "not bit-exact" on mismatch.
- Input journal and external-event markers saved and loaded.
- `HardwareReset` / `DebuggerEdit` markers actually emitted.
- RTC/CMOS reads served from an emulated clock recorded in the session (Profi
  RTC, ATM CMOS).
- Turbo timebase: journal timestamps and the replay clamp correct when
  `z80.t` exceeds the nominal frame (B4).

Exit: a loaded session replays inside a frame with the recorded input; a
session loaded into a different audio rate reports it.

### V4 — memory budget (M)

- Real memory accounting (page payloads, journal, coverage, caches) in
  `ttd/status` and the Qt widget.
- Configurable budget; in memory-only mode the oldest frames are released when
  it is reached, and the earliest reachable frame is reported everywhere.
- Turning TTD / debug mode off mid-recording stops the recording cleanly
  instead of corrupting it (perf review F2).
- **Prerequisite for starting V4**: the integrity and versioning investigation
  is concluded, because crash safety and streaming (I-5) shape how disk mode and
  eviction work together; V5 then implements an already-decided design.

Exit: a one-hour recording on ZX-Evo stays within the budget; seeks to released
frames fail with a clear message.

### V5 — container v2 and disk mode (L) — the format cut

- **Prerequisite** (checked before V4 starts): the integrity and versioning
  decision is written ([integrity-and-versioning.md](integrity-and-versioning.md)).
- Chunked container (target-architecture §7) with the decided integrity
  mechanism, cue table, footer, crash recovery, session UUID.
- Disk mode: background writer appends sealed chunks; evicted pieces fetched
  back on seek; "save" = finalize.
- `ttd.ksy` and the Python analyzer rewritten for chunks (unknown streams
  skipped per the decided rules), `validate` checks exactly what the C++ reader
  checks and decodes every stream.
- Fixtures re-recorded; `testdata/ttd/README.md` updated.
- **From here on the format is versioned** under the decided compatibility
  rules; no more "amend in place".

Exit: kill the emulator mid-recording in disk mode → the file loads up to the
last complete unit; the bit-flip experiment reports every flip the decided
mechanism covers.

### V6 — cleanup (S)

- Retire or update `tools/poc/010-ttd-gui` (its reader expects schema 3);
  delete the duplicate `tools/poc/01-ttd-compression` /
  `010-ttd-compression` copy.
- Truth pass over the parent TDD (`docs/emulator/design/debugger/time-travel-debug/`):
  one meaning of v1/v2, storage modes as built, integrity as built.
- Re-enable or port `DISABLED_TTD_Seek_LongDuration_Test`.
- Move this folder to `DONE.md`.

## 3. Risks per step

| Step | Main risk | Defence |
|---|---|---|
| V0 | A suspected bug turns out to be real in more places than reading suggested | Every item starts with a failing test; scope grows only with evidence |
| V0b | Benchmarks too noisy to gate (like today's capture-cost gate) | Byte counts gate exactly; timings use min-of-N and loose CI tolerances, strict numbers only in the full run |
| V1 | Regions change the capture path every recording depends on | Corpus test compares memory; v1 vs v1+regions JSON comparison must show no byte or time regression on non-region configurations |
| V1b | Intra-frame checkpoints multiply capture cost on exactly the heaviest configurations | Interval chosen from BM-5/BM-2 data; only enabled where needed |
| GS merge | 512 KB region churn during module upload; lazy-sync ordering | FR-19 test; upload workload in the benchmark matrix |
| MoonSound | Port-claim unification touches every model's I/O path | Port-trace and full-decode tests on every creatable model; per-IN/OUT cost in the benchmark |
| V2 | Enforcing the restore report exposes existing silent failures as user-visible errors | Land with the per-device contract tests; triage each new report before release |
| V3 | Emulated RTC changes behaviour for software that reads the clock | Only the TTD recording path uses the recorded clock base; live runs unchanged |
| V4 | Eviction releases pieces still referenced by shared blobs / CoW blocks | Reference-count invariants checked in debug builds; long-session soak test |
| V5 | Crash-recovery scan of a very large file on a slow disk takes minutes | Cue table checkpoints written periodically, not only at finalize; scan measured on a 10 GB file |

## 4. Core-performance review findings covered

TTD-related findings of the
[core-performance review](../2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md)
and where this plan handles them:

| Finding | Content | Handled in |
|---|---|---|
| F2 | Turning debug mode / TTD off mid-recording corrupts history | V4 (FR-17) |
| F3 | Enabling the `timetravel` feature disables fast tape / disk loading | V0 (FR-18) |
| G6 | GS RAM copied and compressed into every checkpoint | GS merge (region) |
| G6 (gap) | z80ex `noint_once`, `reset_PV_on_int`, `int_vector_req` not in the GS blob | GS merge checklist |
| M7 | MoonSound wave SRAM not captured | MoonSound merge (region) |

## 5. What each step changes for users

| Step | Visible change |
|---|---|
| V0 | Fewer silently wrong seeks (ZX-Evo, resume); fast loaders keep working with the TTD feature enabled |
| V0b | Published v1 performance baseline |
| V1 | ZX-Evo / large-RAM recording much cheaper; no periodic hitch |
| GS / MS merges | GS and MoonSound fully time-travelable |
| V2 | Seeks that could not restore a device say so |
| V3 | Saved sessions replay exactly, including keyboard input; RTC-reading software replays deterministically |
| V4 | Memory use shown truthfully and capped |
| V5 | Long sessions stream to disk; crash-safe files; files survive format evolution |

## 6. Open decisions for the user

1. **Option B vs A** (§1). B is recommended; A is acceptable if GS ships with
   `GSRamSize=128` or GS disabled on Pentagon configs until V1.
2. **MoonSound port-claim model** (§3.3 of the merge strategy): one mechanism
   (extend self-decoding devices) vs two with a precedence rule.
3. **Default memory budget and whether disk mode is on by default** (V4/V5):
   needs measurements on ZX-Evo + GS + MoonSound sessions after V1.
4. **Integrity and versioning mechanism** (before V4 starts): open
   investigation, [integrity-and-versioning.md](integrity-and-versioning.md).
5. **Switching storage mode during a session** (V4/V5): fixed at session start,
   or memory → disk switching that keeps only what is still in memory.
