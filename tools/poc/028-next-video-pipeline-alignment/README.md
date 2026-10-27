# 028 — next-video-pipeline-alignment

Feature: ZX Next copper picture latency ([TODO.md](../../../docs/inprogress/2026-10-07-zx-next/TODO.md), "Copper picture latency of the
registers that are fetch-stage inputs"; counted chains in `scratch/cosim-copper/LATENCY.md`, `ScreenNext::PixelLatencyClocks`).

Status: **concluded 2026-10-12** - [measurements](measurements-2026-10-12-register-sweep.md), [analysis](analysis.md),
[conclusions](conclusions.md), [proposal](proposal.md). Run: `run.sh <cores/zxnext/src>` (GHDL, VHDL-2008), the differential second set (sprites, LoRes, palette values, tilemap bits): `run-diff.sh <cores/zxnext/src>` (+ `run-sprite-fill.sh`).

## Goals

1. Where does a pixel of each layer (ULA, Layer 2 in its three resolutions, tilemap, sprites) leave the output relative to the clock at
   which its address / attribute inputs are consumed, in 28 MHz clocks?
2. From that: the clocks from a copper MOVE of a fetch-stage register (NR #16 / #17 / #71 / #18 Layer 2 scroll and clip, NR #26 / #27 /
   #68 bit 2 / #1A ULA scroll and clip, NR #2F-#31 / #1B tilemap scroll and clip, NR #19 sprite clip, NR #12 / #13 / #70 Layer 2 bank and
   mode) to the first changed output pixel.
3. Why the reading of the RTL leaves Layer 2 five pixels behind the ULA while the board has them aligned: which stage of the count
   (the SRAM data timing of `layer2_pixel_qq`, the `sc` phase, the `hc_eff = hc + 1` lookahead) is mis-modelled.

## Requirements

- The reference is the RTL only (`cores/zxnext/src`: `zxnext.vhd`, `video/*.vhd`, `device/copper.vhd`). No other emulator is evidence.
- A simulation of the video path with real signal timing: a testbench around `zxula_timing`, `zxula`, `layer2`, `tilemap`, `sprites`
  and the mixing / output stages of `zxnext.vhd` (extracted or the whole core with the vendor primitives stubbed), run in GHDL or NVC.
- Each fetch-stage register changed at a known clock, the first changed output pixel found by diffing two runs.
- Layer 2 in 256x192, 320x256 and 640x256 modes, ULA in standard and Timex hi-res, tilemap in 40 / 80 columns.

## Constraints

- No change to `core/` or the apps from the POC; the result is adopted by re-implementing it in `ScreenNext::PixelLatencyClocks`
  (it needs the changed register's old and new value already: that signature is done) with its own tests.
- Vendor primitives (BRAM, SRAM controller) are modelled as the minimum that keeps their documented latency.
- Raw waveforms and logs in `scratch/2026-10-12-poc-028/`, only the tables in the reports.

## Acceptance criteria

- Go: for every register listed under Goal 2 a clock count with the simulation's trace as evidence, and the Layer 2 / ULA alignment
  explained (the simulated Layer 2 and ULA pixel x leave on the same clock, or the offset is shown to be real).
- The counts reproduce the already verified ones (fallback / enables 22, palette values 16, layer order 24, ULANext 20) within one clock.
- No-go: the testbench cannot be built without the vendor netlists; then the registers keep zero latency and this POC is closed as
  `abandoned` with the reason.
