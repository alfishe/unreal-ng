# ULA snow: requirements

**Date:** 2026-09-29 · **Plan:** follow-up 2 of PLAN #61 ([m1-contention TODO](../2026-09-28-m1-contention/TODO.md)) ·
**Research:** [research.md](research.md) · **Design:** [tdd.md](tdd.md) · **Status:** [TODO.md](TODO.md)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | unreal-ng shows ULA snow and the double effect on the 48K, 128K and +2 as real machines do |
| G2 | A test that shows it visually, with the expected picture next to the live one, so a person sees at a glance whether a machine or an emulator matches |
| G3 | An analytic check with numbers that the oracle and the co-emulation harness can compare |
| G4 | No cost on machines and programs that never put `I` into slow memory |

## 2. Requirements

| ID | Requirement |
|:--|:--|
| R1 | Snow happens only on the 16K / 48K / 128K / +2 (Ferranti ULA), only while `I` points into slow memory (#40-#7F; on the 128K / +2 #C0-#FF too when an odd page is there) |
| R2 | Every refresh cycle counts: the M1 of each opcode and prefix, the HALT's M1s, the interrupt acknowledge |
| R3 | Snow: when the refresh's T4 falls on the first tick of the ULA's first fetch burst, the first cell of that 16-pixel group shows the bytes at the pixel and attribute addresses with bits 6..0 replaced by `R` bits 6..0 |
| R4 | Double: when T4 falls on the first tick of the second burst, the group's second cell shows the first cell's bytes |
| R5 | The floating bus returns the byte the ULA actually fetched, snow and double included |
| R6 | The absolute tick is anchored on hardware data: the floating-bus phase (Butler 36/37) and the Snow Hold photos |
| R7 | The model's open points (`R` before or after the increment; the 128K #C0-#FF bank) are settled by hardware data or written down as open |
| R8 | The visual test prints through the ROM, loads like a user (tape, TR-DOS), shows expected and live side by side, and prints a verdict with numbers |
| R9 | The analytic check reads the snowed bytes through the floating bus at chosen refresh phases and compares them with an oracle table |
| R10 | Tests on the invariant in `core-tests`: the event conditions, the rendered pixels of a known program, the floating bus; no snow on the +2A / +3 or the clones |

## 3. Acceptance criteria

| ID | Criterion |
|:--|:--|
| AC1 | Snow Hold in unreal-ng renders the picture of the three hardware photos: the same ladder columns in all three bands |
| AC2 | The same program on the +2A and the Pentagon renders no snow |
| AC3 | Programs that never set `I` into #40-#7F render byte-identical frames to before (the contention fingerprints and screen tests unchanged) |
| AC4 | The visual test shows no difference between expected and live on unreal-ng's 48K and 128K; the analytic check reports all values as expected |
| AC5 | The co-emulation harness runs the analytic check on every emulator it finds; the results are in the README |
| AC6 | Full build with zero warnings, all of `core-tests` pass |
