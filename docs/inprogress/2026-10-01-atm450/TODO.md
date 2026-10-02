# TODO — ATM Turbo 2 v4.50 (ATM450)

**Status 2026-10-01: research + design complete, zero implementation.**
Everything needed to start is in this folder:

- [README.md](README.md) — scope, deliverables, effort, how to start
- [requirements.md](requirements.md) — R1-R8 + open questions OQ-1..OQ-4
- [tdd-plan.md](tdd-plan.md) — phases 0-5, ordered test list
- [cross-mapping.md](cross-mapping.md) — reference survey, variant comparison,
  behavior→reference→our-code table, prewire inventory (§4)

## Remaining work (nothing started)

- [ ] Phase 0: `data/configs/atm450/unreal.ini`, factory case, registry tests
      (`emulatormanager_test.cpp:574-576` flip)
- [ ] Phase 1: `portdecoder_atm450.{h,cpp}` + CUT suite T1.1-T1.10
- [ ] Phase 2: video wiring checks (mostly green already)
- [ ] Phase 3: boot tests (basic / TR-DOS / sys-ROM / game / RAM-at-0)
      — resolves OQ-1 (ROM page order of `data/rom/atm1.rom`)
- [ ] Phase 4: TTD paging audit (`pFDFD` in the blob), clock units, snapshots
- [ ] Phase 5: surfaces, Qt menu, `.recipe`/AGENTS docs
- [ ] Decide OQ-2 (palette on 450: unreal=no, ZXMAK2=`#7DFD`) before phase 1
      palette-related negative tests

Tracked as row #79 in [../PLAN.md](../PLAN.md). Suggested branch: `atm450`.
