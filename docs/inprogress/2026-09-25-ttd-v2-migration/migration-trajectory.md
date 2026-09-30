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
  notification enum ([branch-merge-strategy.md](branch-merge-strategy.md) §2). **Done** (checked 2026-09-28):
  the id table and the audio-activity enum are on master; the three branches
  are merged.
- **Capture gap**: RAM page 255 on 4 MB machines (widen the page cache
  sentinel). **Done 2026-09-27**: `ttd::PhysPage` (16 bit, `kPhysPageNone =
  0xFFFF`, `ttdphyspage.h`) in Memory's bank cache, probe, queries and
  coverage; coverage "no page" bucket moved to page field 256 (section v2, v1
  sections skipped → replay); `phys_page` input validated 0..255 and "no
  page" reported as null on all automation surfaces. The same work found and
  fixed a second cache defect: `SetROMPageToBank` / `DefaultBanksFor48k` left
  the cache stale on RAM→ROM switches (ATM/Profi/Scorpion), so ROM accesses
  were filed under the previous RAM page. Tests: `ttdpage255_test.cpp` (ATM3),
  `BankPageCacheAgreesWithTheMappedBank` across 7 models.
- **Suspected bugs, each first reproduced by a test**: stale `_prevPageCache`
  after resume (B1), stale write journal after load (B2), empty-journal find-last
  (B3), non-atomic failed load (B5), unbounded sizes in the journal/coverage
  loaders. **Done** in `a2d265df` + `86813dcb` (current-state §10). B4 (turbo
  timestamps) was confirmed on the way and stays planned in V3.
- **Recommended later** (write-journal follow-ups; none blocks V1):
  - *Journal coverage window* (V5) instead of the all-or-nothing "gapless"
    flag.
  - ~~Report journal completeness in the session status~~ - **done
    2026-09-28**: `write_journal_complete`, `write_journal_wrapped` and
    `write_journal_gap {reason, frame, tinframe}` on WebAPI `/ttd/status`,
    CLI, Lua, Python and the MCP status summary; `TTDSessionInfo` carries them.
  - ~~Warn in the UI/API when journaling, TTD or debug mode is switched off
    during a recording~~ - **done 2026-09-28**: B9 (`e175ff42`) now refuses
    those switches during a user recording; the gaps still possible (recorded
    without the journal, a change on a stopped session, a run between stop and
    live resume, a loaded incomplete file, a debugger's live history) get a log
    warning where they happen and `Journal incomplete` in the Qt TTD panel
    (cause in the tooltip), besides the status fields.
  - ~~Free the pre-allocated 64 MB journal when journaling is switched off
    while no session exists~~ - **done 2026-09-28**.
  - ~~Fix B4 before relying on find-last on turbo machines~~ - **done
    2026-09-28**, with a finer unit than the 3.5 MHz tact proposed here (which
    puts several 8x instructions on one position): positions count T-states
    at the model's top clock (L = LCM of its clock ratios: Scorpion/ATM 7.10 2,
    ZX-Evo 4, ZX Next 8), exact and monotonic through a switch; plain T-states
    on models without turbo. Co-processors (GS, NeoGS) keep their own clocks
    and are not part of L. 40-bit globalT at L=8 lasts ~10 h of recording.
- **Feature-flag side effect** (perf review F3): enabling the `timetravel`
  feature without recording disables fast tape / fast disk; move the override to
  recording start ([requirements.md](requirements.md) FR-18). **Done** in `005771c8`: only a
  recording engages the lock, and it holds through replay and Detached.
- **Analyzer fixes**: Python knows flag bit 3 (bookmarks) (done 2026-09-28). (Done 2026-09-25:
  the analyzer compares the stored piece CRC, and the false "the C++ writer
  stores 0" comments in `ttd_format.py`, `ttd.ksy` and `timetravelmanager.cpp`
  are corrected.) C++ integrity behaviour (eager check at load,
  error vs zero-fill on mismatch) waits for the investigation
  ([integrity-and-versioning.md](integrity-and-versioning.md)).
- **Tests**: `TTD_Corpus_Test` compares RAM too (**done 2026-09-28**: every
  4 KB sub-page a checkpoint holds, after each restore and for the 25
  replayed checkpoints; a flipped byte fails it), and gets a resume-then-compare
  case (present since `8db7841f`); a test for page 255 (done 2026-09-27); the generic state-completeness test
  ([requirements.md](requirements.md) FR-3): first version **done 2026-09-28**
  (`ttdstatecompleteness_test.cpp`, every creatable model, every port in the model's port
  map). It found B10 (the #7FFD paging lock survived a seek), fixed the same day. Also
  done 2026-09-28: a second pass with TR-DOS paged in (the Beta-128 rows decode and
  must be restored) and FR-4: the one runtime device change, the General Sound card
  type switch, is refused during a user recording and drops a stopped session or a
  debugger's live history (`gs-card-switch`); a seek whose device set differs from
  the checkpoint logs it instead of restoring silently.
- **Comments**: remove the stale ones listed in current-state §10 (**done**: re-checked
  2026-09-28, the listed comments in `timetravelmanager.*`, `ttdserializable.h` and the
  page store header were already corrected; only the PoC reader `tools/poc/010-ttd-gui`
  still says the writer stores CRC 0).

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

**Done 2026-09-29**, results and the V1b decision (not needed) in
[v0b-benchmark-results.md](v0b-benchmark-results.md).

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

**Not needed** (V0b measured 2.5–3.5 ms p99 on the heaviest turbo configurations
over 10-minute sessions; [v0b-benchmark-results.md](v0b-benchmark-results.md) §3).
Only if V0b shows seek p99 above 5 ms on a turbo or heavy configuration
(requirements PR-5). Adds extra checkpoints at fixed T-state intervals inside
long frames, so a seek replays at most one interval. Skipped otherwise.

### Merge `generalsound` (M)

Checklist §3.2. The one TTD change on the branch: GS RAM and the lightweight
upload store become regions (V1 API). The missing z80ex fields (perf review
G6) are resolved: since 2026-09-27 the GS coprocessor runs on unreal-z80 and
the blob carries its complete boundary state. Fixtures re-recorded (they now
include a GS card).

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
  RTC, ATM CMOS). On the classic machines the port-read journal already hands
  a replay the recorded clock reads; the emulated clock is still needed so a
  live run is reproducible and the value is not the host's.
- Replay write gate (requirements FR-20): while history is re-executed, no
  device writes to a medium outside the session - disk, SD and HDD images, the
  session write map, write-through, flash and CMOS persistence. One gate in the
  media layer, keyed by the replay state; a test proves an image unchanged after
  replaying writes to it.
- Isolation beyond IN (FR-21): journal DMA transfers into RAM (TSConf, ZX
  Next, NeoGS ZX-DMA) and device-supplied interrupt vectors (check TSConf and
  Sprinter; the classic clones do not drive IM2 vectors) at the same CPU-input
  boundary as the port-read journal, which is off on those configurations until
  then.
- ~~Turbo timebase: journal timestamps and the replay clamp correct when
  `z80.t` exceeds the nominal frame (B4)~~ - done in V0 (2026-09-28).

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
- **Write-journal coverage window** (recommended with the format cut). The
  journal records the `globalT` intervals it was actually writing - from
  recording start or journal switch-on to switch-off, plus the lower edge left
  by ring wrap-around - and the file stores them instead of the single
  "complete" flag (dump flag bit 4). find-last then:
  - answers from the journal inside a covered interval (hit, or a trusted "no
    match" for that part of the query);
  - replays only the uncovered parts (before switch-on, after switch-off, older
    than the wrapped ring edge);
  - so switching journaling back on mid-session is useful again, and a wrapped
    ring no longer forces a full replay of the whole history.
  Keep the V0 journal tests (`ttdmanager_test.cpp` TimeTravelManagerJournal,
  `ttdwritejournale2e_test.cpp`, `ttddumpformat_test.cpp`) and add on/off/on
  and ring-wrap cases.
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
| G6 (gap) | z80ex `noint_once`, `reset_PV_on_int`, `int_vector_req` not in the GS blob | **resolved 2026-09-27** (unreal-z80: `Z80CpuRegisters` boundary state in the blob) |
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

1. ~~**Option B vs A** (§1)~~ — **settled by events (2026-09-28)**: `profi`,
   `generalsound` and `moonsound` all merged before V1, so the sequence that
   happened is A. Its known cost is live on master: the GS checkpoint blob
   carries the whole card RAM (`SoundChip_GeneralSound::TTDStateSize()` = fixed
   state + `_ram.size()`, up to 512 KB), and MoonSound captures Tier A only
   (wave SRAM is not in TTD). V1 is now the fix for both, no longer a merge
   prerequisite.
2. **MoonSound port-claim model** (§3.3 of the merge strategy) — no longer a
   merge blocker and **not needed for V1**, but still open as design debt.
   MoonSound merged with its own mechanism, so master has two: self-decoding
   devices (`RegisterSelfDecodingDevice`, `PortDevice::tryClaimOut/In`; Covox;
   tried from the model decoders) and the full-decode observer
   (`RegisterFullDecodeLowBytePort`, `NotifyFullDecodeIn/Out` called from
   `Z80::in/out`; MoonSound). Decide: one mechanism, or two with a written
   precedence rule. Tracked in [MoonSound TODO](../2026-09-13-moonsound/TODO.md).
3. **Default memory budget and whether disk mode is on by default** (V4/V5):
   needs measurements on ZX-Evo + GS + MoonSound sessions after V1.
4. **Integrity and versioning mechanism** (before V4 starts): open
   investigation, [integrity-and-versioning.md](integrity-and-versioning.md).
5. **Switching storage mode during a session** (V4/V5): fixed at session start,
   or memory → disk switching that keeps only what is still in memory.
