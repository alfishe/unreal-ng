# TODO — TTD v1 → v2 migration

Status: **partly implemented** (2026-09-27 re-audit: PeripheralId table unique, Scorpion `#1FFD` `2013b47b`, CRC compare `2d132fc2`, exact restore `8db7841f`; profi/generalsound/moonsound merged — merge steps moot). Remaining = PLAN #40-V0 + #40. Plan and rationale in
[README.md](README.md) and [migration-trajectory.md](migration-trajectory.md).

Awaiting user decisions (migration-trajectory §6): default memory budget / disk
mode, storage-mode switching, integrity and versioning mechanism
([integrity-and-versioning.md](integrity-and-versioning.md), open, due before V4).
None of them blocks V1. Settled 2026-09-28: option B vs A (all three branches
merged before V1, so A happened; V1 now fixes the GS whole-RAM checkpoint blob);
the MoonSound port-claim model is design debt tracked in the
[MoonSound TODO](../2026-09-13-moonsound/TODO.md), not a V1 prerequisite.
Requirements: [requirements.md](requirements.md).

- [x] Step 0: `PeripheralId` table + notification enum on master - **done** (checked 2026-09-28): ids 5 GS, 9 ProfiPaging, 10 MoonSound, 11 GS-LW, 12 NeoGS in `ttdserializable.h`, `ttd.ksy` and the analyzer (later ids 13-15 appended the same way); audio-activity enum GS = 6, MoonFM = 7, MoonPCM = 8 in `notifications.h`; the branch fast-forward is moot (profi, generalsound, moonsound merged)
- [x] V0: make v1 honest - **done 2026-09-28** (B7-B10, FR-3 with TR-DOS, FR-4, analyzer bookmarks; ~~page-255 gap~~ done 2026-09-27, suspected bugs with tests — ~~B1 B2 B3 B5~~ fixed `a2d265df`/`86813dcb`, ~~B4~~ fixed 2026-09-28 (top-clock TTD time), ~~F3 feature-flag side effect~~ done `005771c8`, analyzer fixes)
- [x] V0b: benchmark harness with v1 as first engine - **done 2026-09-29** ([v0b-benchmark-results.md](v0b-benchmark-results.md)): harness `core/src/debugger/ttd/bench/`, matrix `TTDMatrix/*` in core-benchmarks, compare script `tools/verification/ttd-bench/`, CI gate `TTDBench_Test` replaces `TTD_Capture_Cost_Gate_Test`, v1 baselines in `testdata/ttd/bench/`
- [x] Merge `profi` (merged; moot for this plan)
- [ ] V1: memory regions, per-piece chain cap, dirty-only cache, COW reference blocks
  - [ ] Device EEPROMs as regions (2026-09-30): the ZX-Evo AVR's 4 KiB EEPROM (`EvoAvr`) and the Scorpion SMUC's 2 KiB LC16 serial EEPROM (`SMUCNvram`), the latter together with its serial-link state (mode, shift register, address, page buffer). Today neither is in a checkpoint: a seek back past a guest write keeps the newer contents. Not added to the v1 blobs, which store every device blob whole in every checkpoint (+4 / +2 KiB per frame for data the guest writes rarely); in a region only a change costs anything
- [x] ~~V1b (conditional): checkpoints inside a frame~~ - **not needed** (V0b, 2026-09-29): seek p99 ≤ 3.5 ms on the heaviest turbo configuration over a 10-minute session. The part above 5 ms is drawing the full frame of the position after the seek (intended), which in-frame checkpoints do not shorten
- [x] Merge `generalsound` (merged before V1; GS RAM as a region moves into V1)
- [x] Merge `moonsound` (`e18f3a29`, before V1). Left over: automation (PLAN #11), port-claim unification (design debt, MoonSound TODO), wave SRAM as a region (V1)
- [ ] V2: device state v2
- [ ] V3: determinism inputs — first slice pulled forward by the offline-analysis program: input events and external events saved in the file (not saved today, verified 2026-09-29); see [ttd-offline-analysis.md](../2026-09-28-debugger-family/ttd-offline-analysis.md) O-1
- [ ] Integrity and versioning decision written (before V4)
- [ ] V4: memory budget
- [ ] V5: container v2 + disk mode (format becomes versioned)
- [ ] Media (2026-09-28): storage in TTD v2 through the unified media manager (PLAN #58, [technical design](../2026-09-28-storage-manager/technical-design.md)): media identity per session, the journaled session layer (manager phase M7). Requirements: roadmap [§6 ST-1…ST-6](../2026-09-21-roadmap/01-roadmap-and-machine-state.md). TTD v1 stays media-agnostic (port-level recording)
- [ ] Branch readiness (FR-22 … FR-24, proposed 2026-09-29): no linear-timeline assumption in V1-V4; stream ids for branches reserved in V5. Branches themselves: PLAN #76 ([design](../2026-09-29-model-what-if/design.md))
- [ ] V6: cleanup, TDD truth pass, move folder to DONE
