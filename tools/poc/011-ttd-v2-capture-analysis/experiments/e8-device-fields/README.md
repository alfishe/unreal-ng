# E8 — Which device-state bytes change from frame to frame, and why

Part of the [TTD v2 experiments](../README.md). Design under test: [Phase 2 — device state with versions](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-2-device-state-tdd.md), §5.2.

## Goal

v1 stores every device's whole state in every frame. Phase 2 stores a device's state only when it changed, and only the bytes that changed. Before building it: how many bytes per frame that leaves on real sessions, which fields change in a frame where "nothing happens", and which of them only count time passing (so they can be derived from the machine's clock instead of stored).

## Method

- **Input:** one-minute sessions (3,000 frames) recorded by the benchmark with `UNREAL_TTD_BENCH_KEEP_SESSIONS`, on 11 configurations, with the shipped configurations' cards (NeoGS, MoonSound, TurboSound FM on most machines):

  ```bash
  UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_FRAMES=3000 UNREAL_TTD_BENCH_SEEKS=0 UNREAL_TTD_BENCH_OVERHEAD=0 \
  UNREAL_TTD_BENCH_KEEP_SESSIONS=scratch/e8 ./cmake-build-agent-release/bin/core-benchmarks \
      --benchmark_filter='TTDMatrix/v1/(PENTAGON/(idle|game|demo-eyeache)|ATM3/idle|TSCONF/idle|SPRINTER/idle|SCORPION/idle|PROFI/idle|ATM710/idle|48K/idle|PLUS3/idle)/'
  ```
- **[`run.py`](run.py)** decodes every device blob of every checkpoint with the project's analyzer and compares it with the previous checkpoint's. Per device: the frames in which it changed and the bytes per frame stored three ways (the zstd-compressed XOR with the previous state; the changed byte ranges, 3 bytes of header per range; the smaller), plus 2 bytes of version reference per change. The v1 column is the wrapped, compressed blob v1 stores in every checkpoint; with the classic General Sound card fitted it includes the card's RAM, which the engine keeps as a region (the 11 sessions have NeoGS, not the classic card). It lists the byte runs that change in at least 10% of the frames with their step from frame to frame.
- **Fields.** The runs of TurboSound FM, MoonSound and NeoGS were mapped to their fields by reading the serializers (offsets below). Each was classed as **time** (a clock, an origin or a position that advances because emulated time passes), **running** (real state of hardware or firmware that runs) or **float** (a floating-point resampling phase).
- **Derived.** [`derive-time-fields.json`](derive-time-fields.json) lists the time fields; `run.py --derive` leaves them out of the comparison, which is what remains when the devices keep an anchor time and compute these fields from the clock (the way the Z84C15 CTC already does).

## Results

| Session (3,000 frames) | v1, B/frame | Only changes, B/frame | Time fields derived, B/frame | Per minute | What is left (B/frame) |
|---|---|---|---|---|---|
| 48K_idle | 528 | 143 | 88 | 0.26 MB | TSFM 61, NeoGS 27 |
| ATM3_idle | 2,380 | 181 | 102 | 0.31 MB | TurboSound 66, NeoGS 27, MoonSound 9 |
| ATM710_idle | 1,254 | 255 | 174 | 0.52 MB | Atm2Kbc 73, TurboSound 65, NeoGS 27, MoonSound 9 |
| PENTAGON_demo-eyeache | 993 | 246 | 126 | 0.38 MB | TSFM 100, NeoGS 16, MoonSound 10, Covox 1 |
| PENTAGON_game | 993 | 248 | 129 | 0.39 MB | TSFM 102, NeoGS 16, MoonSound 10, Covox 1 |
| PENTAGON_idle | 961 | 206 | 92 | 0.28 MB | TSFM 66, NeoGS 16, MoonSound 9 |
| PLUS3_idle | 627 | 138 | 82 | 0.25 MB | TSFM 63, NeoGS 19 |
| PROFI_idle | 757 | 145 | 90 | 0.27 MB | TSFM 61, NeoGS 27, BetaDisk 2 |
| SCORPION_idle | 1,061 | 210 | 98 | 0.29 MB | TSFM 61, NeoGS 27, MoonSound 9 |
| SPRINTER_idle | 13,224 | 151 | 120 | 0.36 MB | TurboSound 68, SprinterVideoRam 24, NeoGS 16, SprinterPld 7, BetaDisk 3, SprinterFastRam 1 |
| TSCONF_idle | 1,850 | 248 | 134 | 0.40 MB | TSFM 66, TsConfPaging 39, NeoGS 16, MoonSound 9, EvoSdCard 3 |

- **Storing only changes** is 4–13 times smaller than v1 (88 times on the Sprinter, most of it the video RAM blob, which the engine keeps as a region).
- **Deriving the time fields** halves it again. Phase 2's target (at most 0.5 MB per minute) is met on every configuration but the ATM710 (0.52): its keyboard controller (an MCS-51 running its firmware) changes 73 bytes per frame; not analyzed yet.
- **The Z84C15 does not appear at all** on the Sprinter: its CTC keeps an anchor time and derives the count, so its state does not change while it counts.

### Fields that change in an idle frame (Pentagon 128 at the BASIC prompt)

| Device | Time fields (derivable from the clock) | Running state | Float |
|---|---|---|---|
| TurboSound FM (2,008 B; 62 bytes change) | `_samplePhase` (ours), `fmClockPhase` × 2 (ours), ymfm `m_env_counter`, `m_total_clocks`, `m_prepare_count` × 2 (vendored): 15 bytes | AY tone outputs, noise counter and LFSR, envelope output × 2: 18 bytes | the four SSG decimator phases (f64): 28 bytes; they also drive the AY tick count, and the render cursor offset (1 byte) |
| MoonSound (4,366 B; 29 bytes change) | `_tstateOrigin`, `_lastChipTime` (ours); libopl4 (in-house) `masterPos`, `hostTicks`, `hostRemainder`, `fmTicks`, `outSteps`, `windowTicks`, FM `_egCnt`, `_lfoPm`, `_lfoAm`, PCM `_egCnt`: 25 bytes, all functions of the chip time | the FM rhythm-noise LFSR: 4 bytes | — |
| NeoGS (21,536 B; 22 bytes change) | `_timerStrobeAt`, `_nextDacCrystal`, `_runner.now()`, `_frameStartZxTacts`, `_nextTimerCrystal`, VS10xx `_now` (all ours): 16 bytes | the card Z80 in its idle loop: AF, PC, MEMPTR, R: 6 bytes | — |

What follows for Phase 2 (recorded in the TDD, §5.2.3):

- MoonSound and NeoGS keep an anchor in the device: the code is ours (libopl4 is an in-house library), and it removes 25 of 29 and 16 of 22 changing bytes.
- TurboSound FM's own two time fields become anchors; ymfm's counters (vendored) become engine-side time fields or stay. The float decimator phases and the AY state they drive stay as running state: what remains on TSFM is real.
- **Side finding (NeoGS):** `_runner.now()` gains each frame's instruction overshoot (about 39 ticks per frame on average), so the card clock drifts ahead of the host instead of staying around it; `_nextTimerCrystal` then steps one timer period extra now and then. To check separately (timing, not TTD).

The sessions are not committed (they are 3–58 MB each); `run.py` reruns on any recorded session.
