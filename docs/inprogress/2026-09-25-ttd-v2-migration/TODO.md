# TODO — TTD v1 → v2 migration

Status: **Phase 0 done, Phase 1 next.** Roadmap (what each phase does, why, how it is checked): [README.md](README.md); step details: [migration-trajectory.md](migration-trajectory.md); requirements: [requirements.md](requirements.md). Until 2026-10-01 the steps were numbered V0…V6 ([README §5](README.md#5-former-step-names)).

Awaiting user decisions (migration-trajectory §6): default memory budget / disk mode, storage-mode switching, integrity and versioning mechanism ([integrity-and-versioning.md](integrity-and-versioning.md), open, due in Phase 4, Step 1). None of them blocks Phase 1. Settled 2026-09-28: option B vs A (all three branches merged before Phase 1, so A happened; Phase 1 now fixes the GS whole-RAM checkpoint blob); the MoonSound port-claim model is design debt tracked in the [MoonSound TODO](../2026-09-13-moonsound/TODO.md), not a Phase 1 prerequisite.

## Phase 0 — Preparation (done)

- [x] Step 0 of the merge strategy: `PeripheralId` table + notification enum on master - **done** (checked 2026-09-28): ids 5 GS, 9 ProfiPaging, 10 MoonSound, 11 GS-LW, 12 NeoGS in `ttdserializable.h`, `ttd.ksy` and the analyzer (later ids 13-15 appended the same way); audio-activity enum GS = 6, MoonFM = 7, MoonPCM = 8 in `notifications.h`; the branch fast-forward is moot (profi, generalsound, moonsound merged)
- [x] Step 1 — Make v1 honest - **done 2026-09-28** (B7-B10, FR-3 with TR-DOS, FR-4, analyzer bookmarks; ~~page-255 gap~~ done 2026-09-27, suspected bugs with tests — ~~B1 B2 B3 B5~~ fixed `a2d265df`/`86813dcb`, ~~B4~~ fixed 2026-09-28 (top-clock TTD time), ~~F3 feature-flag side effect~~ done `005771c8`, analyzer fixes)
- [x] Step 2 — Benchmark harness - **done 2026-09-29** ([results](v0b-benchmark-results.md)): harness `core/src/debugger/ttd/bench/`, matrix `TTDMatrix/*` in core-benchmarks, compare script `tools/verification/ttd-bench/`, CI gate `TTDBench_Test` replaces `TTD_Capture_Cost_Gate_Test`, v1 baselines in `testdata/ttd/bench/`
- [x] Step 3 — Merge the feature branches - **done**: `profi`; `generalsound` (GS RAM as a region moves into Phase 1); `moonsound` (`e18f3a29`; left over: automation (PLAN #11), port-claim unification (design debt, MoonSound TODO), wave memory as a region (Phase 1))
- [x] ~~Step 4 — Checkpoints inside a frame~~ - **dropped** (Step 2, 2026-09-29): seek p99 ≤ 3.5 ms on the heaviest turbo configuration over a 10-minute session. The part above 5 ms is drawing the full frame of the position after the seek (intended), which in-frame checkpoints do not shorten

## Phase 1 — Memory that costs only what changes

Design: [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md).

- [ ] Step 1 — Memory regions (machine RAM = region 0, region table, registry API)
- [ ] Step 2 — Chain length limit per piece (replaces the 50-frame key frame)
- [ ] Step 3 — Delta base for changed pieces only
- [ ] Step 4 — Copy-on-write page reference table
- [ ] Step 5 — Device memory as regions: General Sound RAM and lightweight upload store, MoonSound wave memory, NeoGS memory, device EEPROMs
  - [ ] Device EEPROMs (2026-09-30): the ZX-Evo AVR's 4 KiB EEPROM (`EvoAvr`) and the Scorpion SMUC's 2 KiB LC16 serial EEPROM (`SMUCNvram`), the latter together with its serial-link state (mode, shift register, address, page buffer). Today neither is in a checkpoint: a seek back past a guest write keeps the newer contents. Not added to the v1 blobs, which store every device blob whole in every checkpoint (+4 / +2 KiB per frame for data the guest writes rarely); in a region only a change costs anything
- [ ] Step 6 — Restore only the pieces that differ (a seek decodes all memory today: 765–785 µs on ZX-Evo)
- [ ] Step 7 — Encode a changed piece once (today: compressed as XOR and as full, the full almost never wins)

## Phase 2 — Device state with versions

- [ ] Step 1 — Device table with layout versions
- [ ] Step 2 — Unchanged device state shared
- [ ] Step 3 — Degraded restores reported on every surface
- [ ] Step 4 — Sound devices on the device contract

## Phase 3 — Everything a replay needs, in the file

- [ ] Step 1 — Input and external events in the file - **pulled forward** by the offline-analysis program: not saved today (verified 2026-09-29); see [ttd-offline-analysis.md](../2026-09-28-debugger-family/ttd-offline-analysis.md) O-1
- [ ] Step 2 — Configuration fingerprint
- [ ] Step 3 — Reset and debugger-edit markers
- [ ] Step 4 — Emulated clock for RTC / CMOS
- [ ] Step 5 — No writes outside the session while replaying
- [ ] Step 6 — Isolation beyond port reads (DMA, interrupt vectors)
- [ ] Step 7 — Media identity per session (2026-09-28): storage in TTD v2 through the unified media manager (PLAN #58, [technical design](../2026-09-28-storage-manager/technical-design.md)): media identity per session, the journaled session layer (manager phase M7). Requirements: roadmap [§6 ST-1…ST-6](../2026-09-21-roadmap/01-roadmap-and-machine-state.md). TTD v1 stays media-agnostic (port-level recording)

## Phase 4 — Memory budget

- [ ] Step 1 — Integrity and versioning decision written (prerequisite)
- [ ] Step 2 — Real memory accounting
- [ ] Step 3 — Budget, releasing the oldest frames
- [ ] Step 4 — Clean stop when TTD or debug mode is switched off

## Phase 5 — Versioned file and disk mode

- [ ] Step 1 — Chunked container with the decided integrity mechanism
- [ ] Step 2 — Disk mode
- [ ] Step 3 — Format description and analyzer for chunks
- [ ] Step 4 — Write-journal coverage window
- [ ] Step 5 — Stream ids reserved for branched histories (FR-23)

## Phase 6 — Cleanup

- [ ] Retire the proof-of-concept readers, TDD truth pass, re-enable the long seek test, move this folder to DONE

## Across phases

- [ ] Branch readiness (FR-22 … FR-24, proposed 2026-09-29): no linear-timeline assumption in Phases 1–4; stream ids for branches reserved in Phase 5, Step 5. Branches themselves: PLAN #76 ([design](../2026-09-29-model-what-if/design.md))
