# DONE — TurboSound FM (2×YM2203) (2026-09-10)

**Status:** complete — all implementation phases (P0–P8) landed and merged to
master (`d92f302c` … `c7ae65a7`). The README header line "no production code yet"
predates the implementation and is superseded by this marker.

## What landed
- `SoundChip_TurboSoundFM` (`soundchip_turbosoundfm.{h,cpp}`, 436 + 621 lines):
  2 × YM2203 via ymfm with three local TTD-determinism patches, chip core always
  running on the T-state clock (busy flag/timers survive turbo and sound-off),
  suppressible output stage, per-chip word queues (`tsfm/fm_word_queue.h`,
  `tsfm/ym2203_engine.h`).
- `ITurboSoundDevice` slot abstraction (`iturbosounddevice.h`); legacy TurboSound
  adopted it; `SoundManager` selects AY vs FM from `[SOUND] TurboSound = AY | FM`.
- Full TTD: 1 142 B payload, checkpoint/seek/replay with core-hash equality
  (`ttdcheckpoint` PeripheralId 4).
- Output stage with HQ/LQ bit-identity across all core rates 44.1–192 kHz;
  `AudioSourceType::FM1/FM2` mixer + recording sources; Audio Settings FM trim.
- Follow-on work in master: click elimination (`e1c2023b`), device-state reports
  `GET /state/audio/fm[/{chip}]` on WebAPI/CLI/Lua/Python/MCP with the full FM
  register/envelope tree (`d377ab4a` — closes the P7 deferred reporting row),
  HUD audio-activity notifications incl. separate AY/TS vs FM tracking
  (`024e4468`, `c7ae65a7`).

## Evidence
- Phases P0–P7 each marked "Done 2026-09-12 … gate green" in
  [implementation-plan.md](implementation-plan.md); gate evidence quotes
  17/17 chip tests, `CoreHashSameInTurboAndSoundOff` over 300-frame player
  sessions, full suite 2 844 passed / 0 failed at P7.
- Tests in master: `core/tests/emulator/sound/tsfm/` (core, output, soundmanager,
  volume replay, player harness, render diag), `core/tests/debugger/ttd/ttdtsfm_test.cpp`,
  `_helpers/tsfmplayerharness.{h,cpp}`, benchmark
  `core/benchmarks/emulator/sound/turbosound_frame_benchmark.cpp`.
- Listening/tuning verification: [verification/volume-balance-report.md](verification/volume-balance-report.md),
  `materials/volume/` (test ROM + reference WAV); issue register
  [ISSUES.md](ISSUES.md) — 4 fixed, remaining entries cosmetic/by-design.

## Follow-ups
- Open ISSUES.md entries kept as small tracked items: #5 sub-audio coupling
  (by design), #6 int16 mix headroom → wide-mix/limiter design (tracked
  separately, PLAN.md T4), #7 LQ→HQ stale decimator history (minor), #8 SSG pan
  asymmetry (cosmetic), #10 chip-to-chip FM level (not modelled).
- Still deferred from P7: `AYLogRecord.flags` FM classification in the AY log
  analyzer, video-recording FM native taps.
- H1 loudness estimate remains trim-adjustable (`TSFM_FmTrimDb`).
