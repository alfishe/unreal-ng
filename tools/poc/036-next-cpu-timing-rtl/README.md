# 036 — next-cpu-timing-rtl

Feature: `docs/inprogress/2026-10-07-zx-next` (the Open list of `TODO.md`). Builds on POC 028 (the whole zxnext core in GHDL).

The CPU-side timings of the Next (contention, the Z80N instructions under contention, the DMA on the contended clock, the NR #08
switch) were set from reading the RTL and checked against MAME, which is a poor reference. Here the RTL itself runs Z80 programs:
the T80 of the core is released, the SRAM is a real memory loaded with a test ROM, and every instruction start (M1) and port cycle is
logged in 28 MHz clocks from the INT. The same ROM runs in this emulator; the two traces are diffed.

## Goals

1. Is the time of every instruction start the same as on the RTL, from the INT through the paper area, for code and data in
   contended and uncontended memory, port cycles to contended and uncontended ports, on the 48K, 128K and +3 timings?
2. `Chg8kBan` / `Chg8kB_2`: what does a `NEXTREG` (`ED 91` / `ED 92`) cost when fetched from contended memory (TODO: ours ~31 lines
   longer than MAME)?
3. The `dma` / `zilogDMA` border blocks: when does each DMA write to port #FE happen (ZXN and Z80 DMA modes, the burst and
   continuous modes), and how long does the transfer hold the CPU?
4. NR #08 bit 6 (contention off): from which T does a write stop the waits (ours: the end of the line's contention window)?

## Requirements

- The RTL of core 3.02.03 (`cores/zxnext/src` of the Next FPGA repository), read only; patched copies stay in `scratch/` (GPL).
- The machine is configured by an injected NR #03 write (machine type and timing) and a reset, so the boot ROM never runs; the test
  ROM is SRAM bytes 0-#3FFF (ROM 0 of the 48K map), RAM banks 0-7 at SRAM #40000-#5FFFF (`mmu_A21_A13`, zxnext.vhd 2964).
- Each program starts from an EI / HALT at a frame's INT, so both runs share the origin; times are reported in T = 8 clocks of 28 MHz
  from the falling edge of the CPU's INT line.
- This emulator runs the same ROM through `NextRtlTiming_Test` (env-driven dump), the trace is the instruction start `t` of each
  M1 and the `t` of each port write.

## Constraints

- No change to the RTL beyond POC 028's test hooks (the injected NextREG write; `t_hold` is never raised here).
- GHDL 6 (mcode), one frame (69 888 T = 559 104 clocks) costs minutes; programs run one frame after the sync, at most two.
- The machine is shared (load 30-80): runs are queued, a few in parallel.

## Acceptance criteria / expected results

- Expected: the instruction starts match the RTL within 0 T for plain Z80 code on the 48K timing (contention was checked against the
  classic tables); the +3 WAIT form, NEXTREG under contention and the DMA are the likely differences.
- Every difference is either fixed in the emulator (with a regression test that pins the RTL numbers) or explained by the RTL
  (written into `analysis.md`).
- Done when the diff of every program is empty on all three timings, or every remaining line is explained.
