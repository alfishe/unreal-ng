# 036 — measurements 2026-10-26: the RTL traces against this emulator

Core 3.02.03 in GHDL 6 (`run.sh`, testbench `tb_cpu.vhd`), this emulator through `NextRtlTiming_Test.TraceOfATestRom` (`ours.sh`).
Each run: one program of `gen.py` on one timing (NR #03 #91 48K, #A2 128K, #B3 +3), from the INT that starts the body to the body's
end or the next INT. Compared by `dur.py`: every instruction's duration (from one opcode fetch to the next), independent of the time
origin. The body starts 2 T earlier after its INT here than on the RTL in every run (`analysis.md`, the time origin).

## Before the fixes

| Program | What it runs | Durations differing (48K / 128K / +3) |
|---------|--------------|----------------------------------------|
| `mix` (#8100) | contended / uncontended memory, ports #FE, #40FF, #40FE, #80FF, stack in bank 2, 256 iterations through the paper area | port high-byte waits missing / same / 372 (+3 window) |
| `mix5` (#6000) | the same, code in bank 5 | the same causes |
| `mixir` | `mix` with I = #40 (IR in contended memory after M1) | the Z80N internal T-states uncontended |
| `nreg2`, `nreg5` | `mix` plus `NEXTREG #57,#01` / `NEXTREG #57,A` (Chg8kBan), code in bank 2 / bank 5 | the NEXTREG's extra T-states uncontended |
| `dmacont`, `dmaburst`, `zdmacont` | 256 bytes #8000 to port #FE, zxnDMA continuous / burst, Z80 DMA mode, in the border | the transfer one instruction early, no hand-over: the NOP after the OTIR 1551 T against 1545 |
| `dmapaper` | 256 bytes #4000 to port #FE, then #8000 to #4000, in the paper area | block 1: 2367 / 2411 / 1837 against 1809 / 1845 / 1836; block 2: 1798 / 1830 / 1829 against 1804 / 1828 / 1830 |
| `nr08` | contention switched off by `NEXTREG #08,#40` mid-line and back through the ports, the contended loop around it | none |

## After the fixes

All 21 runs: mix, mix5 (48K / 128K / +3), mixir (48K / 128K), nreg2-48, nreg5 (48K / 128K / +3), dmacont / dmaburst / zdmacont-48,
dmapaper (48K / 128K / +3), nr08 (48K / 128K / +3): **0 durations differ** up to the body's end or the frame's next INT (where the
two paths part by the 2 T of the origin: the INT is taken one instruction apart).

The DMA in the paper area by byte (`dmapaper`, the RTL's read strobe ends):

| Timing | Block 1 (bank 5 to #FE) | Block 2 (#8000 to bank 5) |
|--------|-------------------------|----------------------------|
| 48K | paper: 8 T a byte (2 waits on the read), border 6 T; the NOP holding the CPU 1809 T | 8 T a byte in the paper (the write waits); 1804 T |
| 128K | the same pattern on the 128K's line; 1845 T | 1828 T |
| +3 | first byte of a line 6 waits, then 2 a byte, none past the window; 1836 T | 3 or 6 waits on the line's first contended write, then 2; 1830 T |

The pinned numbers (instruction durations around the paper area's start, the DMA NOPs, the NR #08 switches) are in
`core/tests/emulator/machines/next/nextrtltiming_pins.h` (`pins.py`), checked by `NextRtlTiming_Test.PinnedCasesTakeTheRtlsTime`.
