# TODO — TTD v1 → v2 migration

Status: **planning done, nothing implemented.** Plan and rationale in
[README.md](README.md) and [migration-trajectory.md](migration-trajectory.md).

Awaiting user decisions (migration-trajectory §6): option B vs A, MoonSound
port-claim model, default memory budget / disk mode, storage-mode switching,
integrity and versioning mechanism
([integrity-and-versioning.md](integrity-and-versioning.md), open, due before V4).
Requirements: [requirements.md](requirements.md).

- [ ] Step 0: `PeripheralId` table + notification enum on master
- [ ] V0: make v1 honest (page-255 gap, suspected bugs with tests, F3 feature-flag side effect, analyzer fixes)
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
- [ ] V6: cleanup, TDD truth pass, move folder to DONE
