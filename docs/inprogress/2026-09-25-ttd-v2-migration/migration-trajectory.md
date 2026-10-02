# TTD v1 → v2 migration trajectory

Step notes for the [roadmap](README.md): how to get from
[current-state.md](current-state.md) to
[target-architecture.md](target-architecture.md), phase by phase. Each step is
shippable on its own: master builds, all tests pass, and TTD works after every
step. Until 2026-10-01 the steps were numbered V0…V6 ([README §5](README.md#5-former-step-names)).

> **Note (2026-10-02).** From Phase 1 on, the step notes below describe the
> plan before 2026-10-02, when v2 was to change v1 in place. They are kept as
> history. v2 is now a new engine next to v1
> ([engine-approach-and-naming.md](engine-approach-and-naming.md)); the current
> phases and steps are in [README §2](README.md#2-phases), the mapping from the
> step names used here to the current ones in
> [README §5](README.md#5-former-step-names), and the decisions that override
> these notes in [engine-decisions.md](engine-decisions.md). Phase 0 below is
> current.

---

## 1. The question: merge the branches first, or TTD v2 first?

The starting proposal was: finalize `profi`, `generalsound` and `moonsound`,
merge them, and only then do the full TTD v2 migration. Three options were
weighed:

| | A. All branches, then v2 | **B. Branches, with memory regions pulled forward** | C. v2 first, then rebase branches |
|---|---|---|---|
| Sequence | profi → GS → MS → Phases 0–6 | Phase 0, Step 1 → profi → Phase 1 → GS → MS → Phases 2–6 | Phases 0–6 → rebase all three |
| GS on master | copies up to 512 KB of card RAM into every checkpoint until v2 lands | stores only changed 4 KB pieces from day one | same as B, but months later |
| MoonSound TTD | merged incomplete (wave SRAM not captured); Tier B retrofitted later | Tier B built on regions on the branch, merged complete | same as B |
| Branch drift | lowest | low (Phase 0, Step 1 + Phase 1 are small) | **highest**: branches stay open through all of v2 |
| Rework | GS/MS TTD code written for v1 blobs, then rewritten | written once | written once, but rebasing hurts |
| Risk | low per step; the 512 KB/frame regression is real | low | high |

**Recommendation: B.** It is A with one change: the *memory region* part of v2
(small, well bounded) lands on master before GS and MS merge, because both of
them need it and neither can do TTD properly without it. Everything else in v2
(device table, determinism inputs, memory budget, new file container) comes
after all three branches are in, as originally proposed. Doing it after means
the device-state rules are tightened once, with every device present.

## 2. Phases and steps

The roadmap with the purpose and checks of each phase is in [README.md](README.md); this section holds the step details.

```
Phase 0  Preparation (done)
         Step 1 Make v1 honest · Step 2 Benchmark harness · Step 3 Merge the feature branches
         Step 4 Checkpoints inside a frame (dropped: not needed)
Phase 1  Memory that costs only what changes
Phase 2  Device state with versions
Phase 3  Everything a replay needs, in the file
Phase 4  Memory budget
Phase 5  Versioned file and disk mode   <- the format cut; versioned from here on
Phase 6  Cleanup
```

Relative size: S = a focused day or two of agent work, M = several days,
L = a week or more. Every step ends with the standard gate: build, full
`core-tests`, zero warnings, and a re-record of `testdata/ttd` whenever the file
content changes ([testdata/ttd/README.md](../../../testdata/ttd/README.md)).

Each step lands on master as an ordinary series of commits and is all-or-nothing:
if a problem is found after the step has landed, the step is reverted as a
whole (and the fixtures re-recorded), not left half-applied. Until Phase 5 the file
format is amended in place, so a revert needs no compatibility work.

### Phase 0 — Preparation

Done. Make today's engine correct and measurable before changing it, and land the three feature branches.

#### Phase 0, Step 1 — Make v1 honest (done 2026-09-28)

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
  timestamps) was confirmed on the way and **fixed 2026-09-28**: TTD time
  counts T-states at the model's top clock (current-state §10, B4).
- **Recommended later** (write-journal follow-ups; none blocks Phase 1):
  - *Journal coverage window* (Phase 5) instead of the all-or-nothing "gapless"
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

#### Phase 0, Step 2 — Benchmark harness (done 2026-09-29)

- Extend `core-benchmarks` (`core/benchmarks/debugger/ttd/`) with an engine
  selector (v1 first), the configuration matrix and replayable workloads
  (requirements §5), JSON output and a comparison script.
- Replace the load-sensitive `TTD_Capture_Cost_Gate_Test` with the CI-sized
  subset (BR-7, BR-8).
- Measure seek (BM-5) on turbo configurations (ATM, Scorpion turbo) — this
  decides whether Phase 0, Step 4 is needed.

Exit: a v1 baseline JSON for the full matrix is stored; the comparison script
prints it against itself with zero differences in byte counts.

**Done 2026-09-29**, results and the Phase 0, Step 4 decision (not needed) in
[v0b-benchmark-results.md](v0b-benchmark-results.md).

#### Phase 0, Step 3 — Merge the feature branches (done)

##### `profi`

Checklist in [branch-merge-strategy.md](branch-merge-strategy.md) §3.1. Clean
merge; config fixes (`MoonSound=0`, explicit `GSType`).

##### `generalsound`

Checklist §3.2. The one TTD change on the branch: GS RAM and the lightweight
upload store become regions (Phase 1 API). The missing z80ex fields (perf review
G6) are resolved: since 2026-09-27 the GS coprocessor runs on unreal-z80 and
the blob carries its complete boundary state. Fixtures re-recorded (they now
include a GS card).

##### `moonsound`

Checklist §3.3: strip dead weight, unify port claiming with master's
self-decoding API, automation (P2-2, designed first), Tier B wave SRAM as a
region, GS overlap resolution.

#### Phase 0, Step 4 — Checkpoints inside a frame (dropped)

**Not needed** (Phase 0, Step 2 measured seek p99 ≤ 3.8 ms on every configuration of the
full matrix, and 2.51 / 3.46 ms on the two heaviest turbo configurations over 10-minute
sessions; [v0b-benchmark-results.md](v0b-benchmark-results.md) §3).
Only if Phase 0, Step 2 shows seek p99 above 5 ms on a turbo or heavy configuration
(requirements PR-5). Adds extra checkpoints at fixed T-state intervals inside
long frames, so a seek replays at most one interval. Skipped otherwise.

### Phase 1 — Memory that costs only what changes

Technical design: [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md).

- **Step 1 — Memory regions.** `TTDRegionDesc` + region table in the session
  header; machine RAM becomes region 0; device regions register through the
  registry.
- **Step 2 — Chain length limit per piece.** Replaces the global key frame
  (all RAM stored again every 50 frames).
- **Step 3 — Delta base for changed pieces only.** `_prevPageCache` refreshed
  only for dirty pieces, rebuilt explicitly on seek / resume.
- **Step 4 — Copy-on-write page reference table.** The reference table in
  copy-on-write blocks of 16 pages (8 pages since D2, measured in E3).
- **Step 5 — Device memory as regions.** General Sound RAM and the lightweight
  upload store, MoonSound wave memory, NeoGS memory, and the device EEPROMs:
  the ZX-Evo AVR's 4 KiB EEPROM and the Scorpion SMUC's 2 KiB LC16 EEPROM, the
  latter with its serial-link state in the device blob (v1 captures neither;
  storing them whole in every v1 checkpoint would cost 4 / 2 KiB per frame for
  data written rarely).
- **Step 6 — Restore only the pieces that differ.** A live slot map per region;
  a seek decodes a piece only if its slot differs from what live memory holds
  or it was written since.
- **Step 7 — Encode a changed piece once.** Compress the XOR difference first;
  the full piece only when the difference is large.
- Benchmarks: `OnFrameBoundary` on ATM3 (4 MB) with 0/1/16 dirty pages must no
  longer scale with installed RAM; no per-50-frame spike. The capture-cost gate
  counts region pieces and device blobs, not only machine RAM.
- Format: still "amended in place" (the file is not yet versioned); fixtures
  re-recorded.

Exit: 4 MB ZX-Evo frame capture cost within noise of a 128K machine for the same
dirty-page count.

### Phase 2 — Device state with versions

- **Step 1 — Device table with layout versions.** Device table in the session
  header; per-blob layout versions checked in release builds.
- **Step 2 — Unchanged device state shared.** Unchanged blobs shared in memory
  and marked "same as previous" in the file.
- **Step 3 — Degraded restores reported everywhere.** The restore report is
  enforced: a degraded restore is reported through WebAPI (`ttd/seek`,
  `ttd/status`), MCP, CLI, Python/Lua, DeZog and the Qt widget.
- **Step 4 — Sound devices on the device contract.** Sound devices (TurboSound
  slot, GS, MoonSound) go through the declare / implement contract;
  `UpdatePeripheral` becomes part of the registry API.

Exit: every device registered on every creatable model appears in the contract
test; a deliberately corrupted blob produces a degraded result on every surface.

### Phase 3 — Everything a replay needs, in the file

- **Step 1 — Input and external events in the file.** Input journal and
  external-event markers saved and loaded. Pulled forward by the
  offline-analysis program ([ttd-offline-analysis.md](../2026-09-28-debugger-family/ttd-offline-analysis.md)
  O-1): not saved today (verified 2026-09-29).
- **Step 2 — Configuration fingerprint.** In the header: frame length, CPU
  clock / turbo, audio core rate, decimator quality, `soundhq`/`screenhq`, ROM
  signature, device table; replay-type operations report "not bit-exact" on
  mismatch.
- **Step 3 — Reset and debugger-edit markers.** `HardwareReset` /
  `DebuggerEdit` markers actually emitted.
- **Step 4 — Emulated clock for RTC / CMOS.** RTC/CMOS reads served from an
  emulated clock recorded in the session (Profi RTC, ATM CMOS). On the classic
  machines the port-read journal already hands a replay the recorded clock
  reads; the emulated clock is still needed so a live run is reproducible and
  the value is not the host's.
- **Step 5 — No writes outside the session while replaying** (requirements
  FR-20): while history is re-executed, no device writes to a medium outside
  the session - disk, SD and HDD images, the session write map, write-through,
  flash and CMOS persistence. One gate in the media layer, keyed by the replay
  state; a test proves an image unchanged after replaying writes to it.
- **Step 6 — Isolation beyond port reads** (FR-21): journal DMA transfers into
  RAM (TSConf, ZX Next, NeoGS ZX-DMA) and device-supplied interrupt vectors
  (check TSConf and Sprinter; the classic clones do not drive IM2 vectors) at
  the same CPU-input boundary as the port-read journal, which is off on those
  configurations until then.
- **Step 7 — Media identity per session**, through the unified media manager
  (PLAN #58, [technical design](../2026-09-28-storage-manager/technical-design.md)):
  media identity per session and the journaled session layer (manager phase
  M7). TTD v1 stays media-agnostic (port-level recording).
- ~~Turbo timebase: journal timestamps and the replay clamp correct when
  `z80.t` exceeds the nominal frame (B4)~~ - done in Phase 0, Step 1 (2026-09-28).

Exit: a loaded session replays inside a frame with the recorded input; a
session loaded into a different audio rate reports it.

### Phase 4 — Memory budget

- **Step 1 — Integrity and versioning decision** (prerequisite): the
  investigation ([integrity-and-versioning.md](integrity-and-versioning.md)) is
  concluded, because crash safety and streaming (I-5) shape how disk mode and
  eviction work together; Phase 5 then implements an already-decided design.
- **Step 2 — Real memory accounting** (page payloads, regions, journal,
  coverage, caches) in `ttd/status` and the Qt widget.
- **Step 3 — Budget.** Configurable; in memory-only mode the oldest frames are
  released when it is reached, and the earliest reachable frame is reported
  everywhere.
- **Step 4 — Clean stop.** Turning TTD / debug mode off mid-recording stops the
  recording cleanly instead of corrupting it (perf review F2).

Exit: a one-hour recording on ZX-Evo stays within the budget; seeks to released
frames fail with a clear message.

### Phase 5 — Versioned file and disk mode (the format cut)

- **Step 1 — Chunked container** (target-architecture §7) with the integrity
  mechanism decided in Phase 4, Step 1: cue table, footer, crash recovery,
  session UUID.
- **Step 2 — Disk mode.** Background writer appends sealed chunks; evicted
  pieces fetched back on seek; "save" = finalize.
- **Step 3 — Format description and analyzer.** `ttd.ksy` and the Python
  analyzer rewritten for chunks (unknown streams skipped per the decided
  rules), `validate` checks exactly what the C++ reader checks and decodes
  every stream.
- **Step 4 — Write-journal coverage window** (recommended with the format cut).
  The journal records the `globalT` intervals it was actually writing - from
  recording start or journal switch-on to switch-off, plus the lower edge left
  by ring wrap-around - and the file stores them instead of the single
  "complete" flag (dump flag bit 4). find-last then:
  - answers from the journal inside a covered interval (hit, or a trusted "no
    match" for that part of the query);
  - replays only the uncovered parts (before switch-on, after switch-off, older
    than the wrapped ring edge);
  - so switching journaling back on mid-session is useful again, and a wrapped
    ring no longer forces a full replay of the whole history.
  Keep the Phase 0 journal tests (`ttdmanager_test.cpp` TimeTravelManagerJournal,
  `ttdwritejournale2e_test.cpp`, `ttddumpformat_test.cpp`) and add on/off/on
  and ring-wrap cases.
- **Step 5 — Stream ids reserved for branched histories** (FR-23, PLAN #76).
- Fixtures re-recorded; `testdata/ttd/README.md` updated.
- **From here on the format is versioned** under the decided compatibility
  rules; no more "amend in place".

Exit: kill the emulator mid-recording in disk mode → the file loads up to the
last complete unit; the bit-flip experiment reports every flip the decided
mechanism covers.

### Phase 6 — Cleanup

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
| Phase 0, Step 1 | A suspected bug turns out to be real in more places than reading suggested | Every item starts with a failing test; scope grows only with evidence |
| Phase 0, Step 2 | Benchmarks too noisy to gate (like today's capture-cost gate) | Byte counts gate exactly; timings use min-of-N and loose CI tolerances, strict numbers only in the full run |
| Phase 1 | Regions change the capture path every recording depends on | Corpus test compares memory; v1 vs v1+regions JSON comparison must show no byte or time regression on non-region configurations |
| Phase 0, Step 4 | Intra-frame checkpoints multiply capture cost on exactly the heaviest configurations | Interval chosen from BM-5/BM-2 data; only enabled where needed |
| GS merge | 512 KB region churn during module upload; lazy-sync ordering | FR-19 test; upload workload in the benchmark matrix |
| MoonSound | Port-claim unification touches every model's I/O path | Port-trace and full-decode tests on every creatable model; per-IN/OUT cost in the benchmark |
| Phase 2 | Enforcing the restore report exposes existing silent failures as user-visible errors | Land with the per-device contract tests; triage each new report before release |
| Phase 3 | Emulated RTC changes behaviour for software that reads the clock | Only the TTD recording path uses the recorded clock base; live runs unchanged |
| Phase 4 | Eviction releases pieces still referenced by shared blobs / CoW blocks | Reference-count invariants checked in debug builds; long-session soak test |
| Phase 5 | Crash-recovery scan of a very large file on a slow disk takes minutes | Cue table checkpoints written periodically, not only at finalize; scan measured on a 10 GB file |

## 4. Core-performance review findings covered

TTD-related findings of the
[core-performance review](../2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md)
and where this plan handles them:

| Finding | Content | Handled in |
|---|---|---|
| F2 | Turning debug mode / TTD off mid-recording corrupts history | Phase 4 (FR-17) |
| F3 | Enabling the `timetravel` feature disables fast tape / disk loading | Phase 0, Step 1 (FR-18) |
| G6 | GS RAM copied and compressed into every checkpoint | GS merge (region) |
| G6 (gap) | z80ex `noint_once`, `reset_PV_on_int`, `int_vector_req` not in the GS blob | **resolved 2026-09-27** (unreal-z80: `Z80CpuRegisters` boundary state in the blob) |
| M7 | MoonSound wave SRAM not captured | MoonSound merge (region) |

## 5. What each step changes for users

| Step | Visible change |
|---|---|
| Phase 0, Step 1 | Fewer silently wrong seeks (ZX-Evo, resume); fast loaders keep working with the TTD feature enabled |
| Phase 0, Step 2 | Published v1 performance baseline |
| Phase 1 | ZX-Evo / large-RAM recording much cheaper; no periodic hitch |
| GS / MS merges | GS and MoonSound fully time-travelable |
| Phase 2 | Seeks that could not restore a device say so |
| Phase 3 | Saved sessions replay exactly, including keyboard input; RTC-reading software replays deterministically |
| Phase 4 | Memory use shown truthfully and capped |
| Phase 5 | Long sessions stream to disk; crash-safe files; files survive format evolution |

## 6. Open decisions for the user

1. ~~**Option B vs A** (§1)~~ — **settled by events (2026-09-28)**: `profi`,
   `generalsound` and `moonsound` all merged before Phase 1, so the sequence that
   happened is A. Its known cost is live on master: the GS checkpoint blob
   carries the whole card RAM (`SoundChip_GeneralSound::TTDStateSize()` = fixed
   state + `_ram.size()`, up to 512 KB), and MoonSound captures Tier A only
   (wave SRAM is not in TTD). Phase 1 is now the fix for both, no longer a merge
   prerequisite.
2. **MoonSound port-claim model** (§3.3 of the merge strategy) — no longer a
   merge blocker and **not needed for Phase 1**, but still open as design debt.
   MoonSound merged with its own mechanism, so master has two: self-decoding
   devices (`RegisterSelfDecodingDevice`, `PortDevice::tryClaimOut/In`; Covox;
   tried from the model decoders) and the full-decode observer
   (`RegisterFullDecodeLowBytePort`, `NotifyFullDecodeIn/Out` called from
   `Z80::in/out`; MoonSound). Decide: one mechanism, or two with a written
   precedence rule. Tracked in [MoonSound TODO](../2026-09-13-moonsound/TODO.md).
3. **Default memory budget and whether disk mode is on by default** (Phases 4/5):
   needs measurements on ZX-Evo + GS + MoonSound sessions after Phase 1.
   **Direction (user, 2026-09-29):** history memory as linked blocks — one
   64 MB block at the start, more up to the configured limit — with every
   budget and status counted in memory, never in time; spill to disk and / or
   memory-mapped blocks ([target-architecture.md §6.1](target-architecture.md#61-memory-as-linked-blocks-direction-2026-09-29)).
   Open: the default limit, the spill trigger and mechanism.
4. **Integrity and versioning mechanism** (before Phase 4 starts): open
   investigation, [integrity-and-versioning.md](integrity-and-versioning.md).
5. **Switching storage mode during a session** (Phases 4/5): fixed at session start,
   or memory → disk switching that keeps only what is still in memory.
