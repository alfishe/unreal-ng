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
- **Tests**: `soundchip_moonsound_test.cpp` (device, TTD, canaries, core-rate
  renegotiation). The disk- and guest-binary-driven tests (MFM samples 1–4,
  demo disk, MoonService) were removed on 2026-09-18; their regressions are
  covered by libopl4's `opl4fmtests.cpp`.
- Design and investigation record: integration, core TDD and TTD TDD, the
  verification findings log (§2.1–§2.8) and
  [2026-09-18-2045-opl4-output-stage-harshness.md](2026-09-18-2045-opl4-output-stage-harshness.md).

## Remaining (value order)
1. **P2-2: automation** — ~~state reports~~ done 2026-09-28:
   `DeviceState::MoonSound()` / `MoonSoundFm()` / `MoonSoundPcm()` on every
   surface (WebAPI `/state/audio/moonsound[/fm|/pcm]`, CLI `state audio
   moonsound`, Lua/Python `audio_moonsound_state`, MCP `audio_moonsound`,
   `audio_opl4_fm`, `audio_opl4_pcm`), read through libopl4's side-effect-free
   `Opl4::PeekFm/PeekPcm`. Left: control (enable + mixer gain) through the
   P2-3 settings surface, and MoonSound rows in the static port map.
2. **D2 hardware check** — record a high FM sine on a real ZXM-MoonSound to
   settle whether the HoldDrop reducer (`Authentic`) is real.
3. **Port-claim unification (design debt)** — master has two "a device claims
   a port outside the table" mechanisms: self-decoding devices
   (`RegisterSelfDecodingDevice`, `tryClaimOut/In`, Covox) and MoonSound's
   full-decode observer (`RegisterFullDecodeLowBytePort`, tapped in
   `Z80::in/out`). Pick one mechanism or write the precedence rule; also sets the
   per-IN/OUT cost on every model. See TTD v2
   [migration-trajectory.md](../2026-09-25-ttd-v2-migration/migration-trajectory.md) §6 item 2.
4. **TTD Tier B** — wave SRAM as a memory region, with TTD v2 V1 (PLAN #40).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — MoonSound (T2, item #11).
- Triage item: `../2026-09-14-automation-triage-gaps/recommendations.md` P2-2.
