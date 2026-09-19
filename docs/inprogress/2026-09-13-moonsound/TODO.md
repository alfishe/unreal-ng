# TODO — MoonSound (OPL4) integration (2026-09-13)

**Status:** implemented and integrated; automation/device-state reporting
still open (updated 2026-09-18).

## Progress
- **libopl4** (YMF278B FM + PCM, bus/timing model, render layer) lives in
  `core/src/3rdparty/opl4` (std-lib only, own `opl4tests`: 2205 checks). The
  research copy in `tools/poc/015-opl4-synthesis` is frozen and no longer
  built by core. The ymfm verification backend and co-simulation stayed in
  the POC; core links no ymfm OPL/PCM code.
- **Device** `SoundChip_Moonsound`: port decode, FM data-port read-back,
  split FM/PCM mixer sources, HUD activity nudge, live core-rate changes
  (44.1–192 kHz), HiFi render default.
- **TTD**: chip state in the peripheral registry, exact Authentic restore,
  bounded HiFi restore window.
- **Tests**: `moonsound_device_test.cpp` (device, TTD, canaries, core-rate
  renegotiation). The disk- and guest-binary-driven tests (MFM samples 1–4,
  demo disk, MoonService) were removed on 2026-09-18; their regressions are
  covered by libopl4's `opl4fmtests.cpp`.
- Design and investigation record: integration, core TDD and TTD TDD, the
  verification findings log (§2.1–§2.8) and
  [2026-09-18-2045-opl4-output-stage-harshness.md](2026-09-18-2045-opl4-output-stage-harshness.md).

## Remaining (value order)
1. **P2-2: automation** — `DeviceState` MoonSound report and
   control/inspection endpoints (nothing under `core/src/automation` or
   `emulator/state` yet).
2. **D2 hardware check** — record a high FM sine on a real ZXM-MoonSound to
   settle whether the HoldDrop reducer (`Authentic`) is real.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — MoonSound (T2, item #11).
- Triage item: `../2026-09-14-automation-triage-gaps/recommendations.md` P2-2.
