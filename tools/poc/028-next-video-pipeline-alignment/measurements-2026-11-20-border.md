# Measurements 2026-11-20: the border colour written by the CPU (port #FE), scenario 54

Feature: [TODO.md](../../../docs/inprogress/2026-10-07-zx-next/TODO.md), "Border colour changes of the CPU (#FE) inside a line";
adopted as `ScreenNext::BorderEffectHalfPixel` (core/src/emulator/video/next/screennext.cpp), tests `NextBorderEffect_Test`,
`NextBorderMachine_Test` (core/tests/emulator/video/next/nextborder_test.cpp).

## Setup

As [the register sweep](measurements-2026-10-12-register-sweep.md), with two more hooks (`tbh.vhd`, `patch_rtl.py`):

- `t_fe_we` / `t_fe_val`: a load of `port_fe_reg` (zxnext.vhd 3588-3596, the register the CPU's `port_fe_wr` strobe loads on a CLK_28
  edge). The write is assigned a quarter clock after an edge (`fe_write`), so the register loads on the next rising edge: count28 + 1
  of the event line. A write assigned on an edge's own time step races that edge and is lost (the first runs lost every restore).
- `t_pent`: the ULA's `i_timing_pentagon` (the border taken every CLK_14, not at the cell load).
- The event line also carries the count28 of the CPU clock's last rising edge (`p_cpu_clk`), so each load has its CPU clock phase.

Scenario 54: the border 0 -> 7 in the right border of a paper line (`g_h0` 256 / 280 / 312, 32 one-clock offsets each), restored two
lines later. Measure: `analyze.diff` against a reference run (`g_ref=1`, the same writes of the old value).

```
tools/poc/028-next-video-pipeline-alignment/run.sh   # builds the tree; then, in its out dir:
./tb_zxnext -gg_scenario=54 -gg_h0=280 [-gg_pent=1] -gg_log=fe.txt -gg_events=efe.txt
./tb_zxnext -gg_scenario=54 -gg_h0=280 -gg_ref=1 [-gg_pent=1] -gg_log=fer.txt -gg_events=efer.txt
```

## Results (clocks from the line's `hc` = 0)

The CPU clock (3.5 MHz, 8 clocks) rises at clocks = 3 mod 8 and falls at 7 mod 8. `port_fe_border` follows `port_fe_reg` on the
first falling edge after the load (zxnext.vhd 3600-3605), so loads in 1120..1126 change it at 1127, 1127..1134 at 1135, and so on.

| port_fe_border changes at | first changed output, 48K / 128K timing | Pentagon timing |
|---|---|---|
| 1127 | 1152 (hc 288) | 1144 (hc 286) |
| 1135 | 1152 | 1152 (hc 288) |
| 1143 | 1184 (hc 296) | 1160 (hc 290) |
| 1151 | 1184 | 1168 (hc 292) |
| 1263 (h0 312) | 1312 (hc 328) | 1288 (hc 322) |

- Pentagon: the output follows `port_fe_border` by 17 clocks, a CPU T-state (4 sub-pixels) a step.
- 48K / 128K: the same 17 clocks, then the next cell boundary: the outputs are multiples of 32 clocks from `hc` = 0 (the ULA's
  `sload` at `hc(3:0)` = 4 / 12, zxula.vhd 368-380, its cell leaving 16 clocks after the load). Cells are 8 pixels, cell-aligned
  with the paper.
- The paper's first pixel leaves at clock 64 (`hc` 16): a pixel leaves the output 16 pixels after its `hc`.

## Adopted

For a write at the CPU's IORQ T-state (`op_noprefix.cpp` OUT: the write lands at T2, the IORQ T-state), `clocks` = 8 x T-states from
the line's first paper T-state (the copper's convention: that T-state is the ULA's counter zero, nextcopper.h):

    out = clocks + 3 (the CPU edge after the counter's T-state boundary) + 4 (to the falling edge; the half CPU clock) + 17
    48K / 128K: out rounded up to a multiple of 32
    image sub-pixel = 64 + (out - 64) / 2   (64: the paper's left edge in the 640-wide grid)

The half CPU clock scales with the CPU speed (2 / 1 / at least 1 at 7 / 14 / 28 MHz); the 3-clock edge offset is the 3.5 MHz one.
Not measured: the outer border outside the 320 x 256 grid (drawn with the colour of each line's start and end), and the Timex
hi-res border (port #FF bits 5:3), whose writes do not split a line.
