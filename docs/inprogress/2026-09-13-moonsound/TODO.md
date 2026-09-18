# TODO — MoonSound (OPL4) integration (2026-09-13)

**Status:** not started — full design set, zero implementation code (no OPL4/
YMF278B device in `core/src`; verified 2026-09-16).

## Progress
- Complete design documentation:
  [opl4-unreal-ng-integration.md](opl4-unreal-ng-integration.md) (integration
  architecture), [opl4-core-tdd.md](opl4-core-tdd.md) + 
  [opl4-ttd-integration-tdd.md](opl4-ttd-integration-tdd.md) (TTD-complete
  device design following the TSFM playbook).
- Precondition landed: the TSFM track proved the pattern this design reuses
  (device split core/output, TTD-deterministic engine patching, FM-only taps,
  device-state reports) — see
  [`../2026-09-10-turbosound-fm/DONE.md`](../2026-09-10-turbosound-fm/DONE.md).

## Remaining (value order)
1. **P2-2: write the automation section into the design** (DeviceState::
   Moonsound report, control/inspection endpoints) — required *before*
   implementation per the triage program.
2. **Device implementation** — YMF278B FM + PCM halves, config gating, port
   decode (SMUC-era machines), per the TDD.
3. **TTD integration** — peripheral registry payload, checkpoint/seek tests.
4. Audio pipeline: mixer sources, recording taps, HUD activity.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — MoonSound (T2, item #11).
- Triage item: `../2026-09-14-automation-triage-gaps/recommendations.md` P2-2.
