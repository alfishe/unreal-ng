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
- [ ] V0b: benchmark harness with v1 as first engine (parallel with V0)
- [x] Merge `profi` (merged; moot for this plan)
- [ ] V1: memory regions, per-piece chain cap, dirty-only cache, COW reference blocks
- [ ] V1b (conditional): checkpoints inside a frame, only if V0b shows PR-5 failing
- [x] Merge `generalsound` (merged before V1; GS RAM as a region moves into V1)
- [x] Merge `moonsound` (`e18f3a29`, before V1). Left over: automation (PLAN #11), port-claim unification (design debt, MoonSound TODO), wave SRAM as a region (V1)
- [ ] V2: device state v2
- [ ] V3: determinism inputs
- [ ] Integrity and versioning decision written (before V4)
- [ ] V4: memory budget
- [ ] V5: container v2 + disk mode (format becomes versioned)
- [ ] Media (2026-09-28): storage in TTD v2 through the unified media manager (PLAN #58, [technical design](../2026-09-28-storage-manager/technical-design.md)): media identity per session, the journaled session layer (manager phase M7). Requirements: roadmap [§6 ST-1…ST-6](../2026-09-21-roadmap/01-roadmap-and-machine-state.md). TTD v1 stays media-agnostic (port-level recording)
- [ ] V6: cleanup, TDD truth pass, move folder to DONE
