# 036 — conclusions

Acceptance met: the diff of every program is empty on all three timings (21 runs, `measurements-2026-10-26-rtl-traces.md`); the
one remaining constant, the 2 T of the time origin, is explained by the T80's INT sampling (`analysis.md`).

The goals:

1. Instruction starts: equal to the RTL's after six fixes in the emulator, none of which MAME shows:
   - the Next's port contention by the high byte (mem_contend from the address alone);
   - the Z80N's own internal T-states contended (`Z80nCpuAccessInternal` `Contend` hook);
   - the +3 WAIT window (`UlaContention::SetWindowedWait`: 3 T earlier, the last cell 1 T shorter, hc 0 1 T later than the 128K's);
   - the DMA: the bus goes over after the instruction that follows the enabling one, 1 T in and 4 T out (3 T for a burst wait);
   - the DMA's port cycles take no port contention, its T-states without a strobe take the address's (`NextDma::HoldBus`);
   - the DMA on the +3 samples WAIT half a T before the CPU (`UlaContention::DmaDelayAt`).
2. `Chg8kBan` / `Chg8kB_2`: a `NEXTREG` fetched from contended memory costs what this emulator gives, contention of its extra
   T-states included (`nreg5` on all three timings). MAME's ~31 lines shorter green border is MAME's; the item is settled by the RTL.
3. The `dma` / `zilogDMA` border blocks: each write to #FE is at the RTL's T in the border and the paper, continuous, burst and the
   Z80 mode; the CPU is held 1536 + 5 T for 256 bytes in the border (`dmacont`). The block heights follow from that.
4. NR #08 bit 6: the switch at the end of the line's window, as ours had it.

Regression: `NextRtlTiming_Test.PinnedCasesTakeTheRtlsTime` pins the RTL's durations of 21 runs (`nextrtltiming_pins.h`, made by
`pins.py`); the mutation checks of each fix fail it.
