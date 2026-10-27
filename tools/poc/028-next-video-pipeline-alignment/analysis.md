# Analysis

## The answers to the goals

1. **Where a pixel leaves relative to its inputs.** Table of the measurements. Three families:
   - registers on the output pipeline's own CLK_14 chain (the layer enables, the transparency, the palette selects, the layer order, the
     fallback colour): 22 clocks from the WAIT, half-pixel granularity. This is the count made from `zxnext.vhd` earlier, confirmed;
   - registers compared per pixel in a CLK_7 stage (the ULA, tilemap and sprite clip windows, Layer 2's palette offset): 22 clocks,
     4-clock pixels; Layer 2's own fetch registers (enable, scroll, clip): 26 clocks, 4-clock pixels (the SRAM read adds one pixel);
   - registers read where an 8-pixel group is fetched (the ULA's scroll X / Y: 56, the fine scroll: 70, the tilemap scroll: 53): a change
     takes effect at the next 32-clock cell boundary after the latency.
   ULANext (NR #43 bit 0, NR #42): 20, as counted from the RTL's pixel stage.
2. **The Layer 2 / ULA alignment.** They are aligned: pixel 0 of both leaves at the same position of the line. The hand count that put
   Layer 2 five pixels behind the ULA was wrong somewhere in the stage count of the SRAM path (the SRAM answers within the cycle in
   the top level, one pixel earlier than the count assumed); the simulation settles it without finding which stage.

## What was changed in the RTL to simulate it

Only what a simulator needs, nothing in the logic: the UNISIM use of the boot ROM, the VHDL-87 file declaration and an in-line power-on
pattern in `dpram2` (bank 5 and the palettes), `to_unsigned(-1, n)` of `fifop` (a bound check failure), `layer2_req_t`'s power-on value,
zero initial values for every other signal (the FPGA's registers power up zero), behavioural models of the three coregen RAMs
(`sdpram_16_9`, `sdpram_64_9`, `sdpram_128_8`), and the test hooks. The Z80 is held in WAIT so that the boot ROM does not write the
registers behind the test.

## Limits

- The change is applied one CLK_28 edge after the stimulus process's own write, as it takes the place of the NextREG write (calibrated on
  the ULA enable, whose chain is known): +/- 1 clock.
- A forced value stays until the next force: the registers behave as written once, not as a CPU's stream of NextREG writes
  (which pass the same register).
- Sprites are not simulated (the coregen line buffers and pattern RAM are not modelled).
