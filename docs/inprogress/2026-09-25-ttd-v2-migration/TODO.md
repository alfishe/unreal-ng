# TODO — TTD v1 → v2 migration

Status: **partly implemented** (2026-09-27 re-audit: PeripheralId table unique, Scorpion `#1FFD` `2013b47b`, CRC compare `2d132fc2`, exact restore `8db7841f`; profi/generalsound/moonsound merged — merge steps moot). Remaining = PLAN #40-V0 + #40. Plan and rationale in
[README.md](README.md) and [migration-trajectory.md](migration-trajectory.md).

Awaiting user decisions (migration-trajectory §6): option B vs A, MoonSound
port-claim model, default memory budget / disk mode, storage-mode switching,
integrity and versioning mechanism
([integrity-and-versioning.md](integrity-and-versioning.md), open, due before V4).
Requirements: [requirements.md](requirements.md).

- [ ] Step 0: `PeripheralId` table + notification enum on master
- [ ] V0: make v1 honest (~~page-255 gap~~ done 2026-09-27, suspected bugs with tests — B3 confirmed, F3 feature-flag side effect, analyzer fixes)
- [ ] V0b: benchmark harness with v1 as first engine (parallel with V0)
- [ ] Merge `profi`
- [ ] V1: memory regions, per-piece chain cap, dirty-only cache, COW reference blocks
- [ ] V1b (conditional): checkpoints inside a frame, only if V0b shows PR-5 failing
- [ ] Merge `generalsound` (GS RAM as region)
- [ ] Finish + merge `moonsound` (automation, port-claim unification, wave SRAM as region)
- [ ] V2: device state v2
- [ ] V3: determinism inputs
- [ ] Integrity and versioning decision written (before V4)
- [ ] V4: memory budget
- [ ] V5: container v2 + disk mode (format becomes versioned)
- [ ] Media (2026-09-28): storage in TTD v2 through the unified media manager (PLAN #58, [technical design](../2026-09-28-storage-manager/technical-design.md)): media identity per session, the journaled session layer (manager phase M7). Requirements: roadmap [§6 ST-1…ST-6](../2026-09-21-roadmap/01-roadmap-and-machine-state.md). TTD v1 stays media-agnostic (port-level recording)
- [ ] V6: cleanup, TDD truth pass, move folder to DONE
