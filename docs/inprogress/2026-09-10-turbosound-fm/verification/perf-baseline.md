# TurboSound Performance Baseline (P0 record)

Per-frame cost of the current legacy TurboSound device on Pentagon 128,
measured with `core-benchmarks`
(`core/benchmarks/emulator/sound/turbosound_frame_benchmark.cpp`).
Purpose: the denominator for the P2 gate (interface adoption must not
change turbo frame cost) and the reference point for the TSFM chip-core
budget (design §12.6: core ≤ 130 µs/frame, output stage ≤ 60 µs/frame).

## Configuration

* Model: PENTAGON (71680 T/frame), fresh emulator per benchmark
* Sound: default core rate 44.1 kHz, HQ DSP enabled (`soundhq` default),
  TurboSound attached
* Frame driven via `MainLoop::RunFramePublic()` (no pacing), 10 warm-up
  frames, 1000/2000 iterations
* `player-load` scenario: hand-assembled Z80 driver at `0x8000` writing
  64 register/data pairs (128 port writes) + 128 status polls per frame
  to `#FFFD`/`#BFFD`, `ei/halt` frame sync — the write/poll shape of the
  TFM Compiler player (`sub_628A` / the outi pair loop at `0x62DF`).
  Data values keep bit 7 clear so the legacy device's register-data
  status reads never park the loop. The sustained register traffic of a
  6-channel tune is of this order (the real player cannot be driven on
  the legacy device — see player-entry-points.md — so the load is
  synthetic and calibrated by protocol shape, not captured traffic).

## Results (2026-09-12, Mac Studio, Release, clang)

| Benchmark | Time/frame | Notes |
|---|---|---|
| `BM_TurboSoundFrame_Idle` | **826 µs** | machine halted; HQ AY synthesis of 2 chips dominates |
| `BM_TurboSoundFrame_PlayerLoad` | **1035 µs** | +64 reg/data pairs per frame |
| `BM_TurboSoundFrame_PlayerLoad_Turbo` | **123 µs** | turbo mode (sound synthesis skipped) — **P2 gate reference** |

Reading:

* The HQ output stage of the two legacy AY chips costs ~826 µs/frame on
  this host — far above the TSFM output-stage budget (60 µs), which is
  why design §6.2 replaces per-chip FIR chains with one shared HQ loop
  and slave decimators for P6.
* The driver loop adds ~209 µs/frame (≈3.3 µs per register pair). That is
  mostly Z80 instruction emulation of the outi/poll loop (~100 T per
  pair ≈ 9 % of the frame), not the port write itself — port-write cost
  is not a factor for the TSFM budget.
* The P2 gate compares against **123 µs/frame (turbo, player-load)**:
  after `ITurboSoundDevice` adoption this number must stay within noise.
