# 08 - A hardware-level line timing model (public documentation only)

Feeds: **FULL** (maximum accuracy: the graphics engine's own per-line pipeline).

## Goal

From the public documentation (FT81x programmer guide §2.5.7, datasheet §4.3.1, as
summarized in `docs/inprogress/2026-10-01-tsconf-vdac2/ft812-behavior-spec.md` §5): what the
per-line render budget is, what happens when a line overflows it, and what a cycle- or
pixel-accurate line pipeline would cost on the CPU and on the GPU next to the current
per-line renderer.

## The documented model

- The picture is built line by line into a line buffer, no frame buffer; a line is drawn
  during the previous line's scan-out.
- The engine fetches **one display list command per system clock** (commands in called
  subroutines count each time); each primitive's span is filled at **16 pixels per clock**
  (NEAREST; 8 for paletted and text formats; BILINEAR 4 / 2).
- The budget of a line "depends on `REG_PCLK` and `REG_HCYCLE` but is never less than 2048
  internal clock cycles"; with VDAC2's `PCLK` = 1 a line lasts `HCYCLE` = 1344 system
  clocks in mode 7. What an overflowing line shows is not documented (TO VERIFY on a card).
- eve-emu already computes this cost per line (commands + fill clocks, `EveGetLineCost`)
  and counts overflows; it draws the line completely.

## Method

- Line cost vs budget on the captures: `eve-replay --costs` (reads `EveGetLineCost` for every
  line of every drawn frame).
- `clocked-model.cpp`: a micro-benchmark of the scheduling alone, for an R-Type-like line
  (151 commands: 120 state commands, a full-width background at 16 px/clock, 30 paletted
  sprites of 48 px at 8 px/clock): a state machine stepped once per clock (stops at the
  exact clock and pixel where the budget runs out) against the closed form (one step per
  command, the same cut point computed arithmetically).
- The hardware's shape on the GPU: one thread per line walking the line pixel by pixel
  (`metal-render --line-threads`, kernel `RenderLineSeq`) against one thread per pixel.

```
tools/poc/021-eve-accel/08-hardware-timing-model/run.sh   # stopped by the user after the first part
tools/poc/021-eve-accel/build/clocked-model
```

## Results

Line cost on rtype-boot (779 680 lines, 1032 frames): mean 124 commands + 163 fill clocks;
worst line 621 clocks of a 1344-clock budget (46 %); **no line overflows**; cost/budget:
22 % of lines under 1/8, 42 % 1/8-1/4, 33 % 1/4-3/8, 4 % 3/8-1/2. (R-Type's author reports
a worst line of 1247 on the card's own count; the play capture was not measured before the
session was stopped.)

Scheduling cost (one core, load ~100):

| Model | per line | at 47 600 lines/s (806 lines x 59 Hz) |
|---|---|---|
| clock-stepped, used clocks only (395 of 1344) | 1 513 ns (3.8 ns per clock) | 7.2 % of a core |
| clock-stepped, every clock of the line (1344) | ~5 100 ns (extrapolated) | ~24 % of a core |
| closed form (exact same cut point) | 155 ns | 0.7 % of a core |

The pixel work is the same in all models (eve-emu's span renderer) and comes on top.

GPU (heaviest play frame, 216 ops): one thread per pixel ~0.75-1.0 ms; **one thread per
line 24.6 ms** - 25-30x slower, and slower than one CPU core (10.6 ms).

## Analysis

- Overflow emulation does not need clock stepping: the cost of each command and span is
  known when it is executed, so the point where the budget runs out (command and pixel) can
  be computed in closed form and the rest of the line treated as the card does (to be
  measured). The extra cost is a few integer operations per command: under 1 % of a core.
- A literal clock-by-clock model of the engine costs 7-24 % of a core in bookkeeping alone
  and adds no information over the closed form, as long as the documented model (1 command
  per clock, N pixels per clock) holds.
- On a GPU the hardware's own shape (a sequential pipeline per line) is the wrong shape: 768
  threads cannot fill the GPU. The per-pixel kernel can apply the cut point instead: a
  per-line prefix of op costs (one small pass, or computed with the op list) tells each
  pixel which ops of its line the chip still had time for.

## Conclusions

- For the FULL profile the line budget and overflow cut are cheap on both CPU and GPU in
  closed form; real time is not at risk. What is missing is a card measurement of what an
  overflowing line looks like and of the budget overhead below `HCYCLE` (spec V7).
- Windows / Linux: nothing platform-specific.
