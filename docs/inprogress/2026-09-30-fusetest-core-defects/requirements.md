# Three core defects found by fusetest: requirements

**Date:** 2026-09-30 · **Research:** [research.md](research.md) · **Design:** [tdd.md](tdd.md) · **Status:** [TODO.md](TODO.md)

## 1. Background

FUSE's timing test program fusetest ([tools/verification/contention/fusetest](../../../tools/verification/contention/fusetest/README.md))
runs on unreal-ng in `FuseTest_Test`. It printed four lines that FUSE 1.6.0 passes. The rule for hardware facts is a
consensus of independent sources, RTL and schematics first, emulators second; [research.md](research.md) checked
each claim that way (the MiSTer core, the Next's VHDL, the 128K service manual and a PAL readout, real-hardware
reports) and all three hold.

| Line | Machines | What the hardware does | What unreal-ng did |
|:--|:--|:--|:--|
| Floating bus | 48K, 128K | An IN from a port with no device returns the byte the ULA fetches at the end of the I/O cycle, after every wait. A port whose high byte is in contended memory (#40xx-#7Fxx) is held after IORQ too, so the byte comes up to 12 T later | Looked the byte up at IORQ, before the late waits |
| 0x3ffd / 0x7ffd read | 128K, grey +2 | The paging latch's decode (IORQ & (RD \| WR) & !A15 & !A1) does not tell a read from a write: an IN from the #7FFD decode writes the bus byte into the latch (bits 0-5), unless the lock bit is set | Latched on OUT only |
| 0xbffd read | 128K, grey +2 | A read of #BFFD leaves the AY off the bus (BDIR = BC1 = 0): the floating bus | The AY answered with its register |

The fourth line was the +2A / +3's #BFFD read, which unreal-ng already answers with the AY register like the
hardware; the test runner leaves the sound slot empty (`SoundCardScope`), so fusetest saw no AY there.

## 2. Requirements

| ID | Requirement |
|:--|:--|
| R1 | The floating bus of an IN is the byte on the bus at the end of the cycle: after the ULA's waits after IORQ. An IN without those waits returns what it returns today (the lookup is calibrated to it; Arkanoid / Sidewize sync and the Butler floating-bus tests) |
| R2 | On the 128K and the grey +2, an IN from the #7FFD decode writes the byte the CPU took (a device's, or the floating bus) into #7FFD, bits 0-5, through the normal write path: nothing while the lock bit is set. The +2A / +3 and every clone keep write-only decoding |
| R3 | On the 128K and the grey +2 a read of #BFFD is not answered by the AY / TurboSound slot: the port stays undecoded. The +2A / +3 keep reading the register there |
| R4 | Machines the fixes do not concern pay nothing measurable: `BM_HostFrame_*` and an I/O-read benchmark within noise in an A/B |
| R5 | The standalone CPU library extracted from this core (unreal-z80) can model R1: its port-read callback sees the clock after the late waits, and its test suites pass |

## 3. Acceptance criteria

| ID | Criterion | Checks |
|:--|:--|:--|
| AC1 | fusetest passes every line on the 48K, 128K and +3 with no known deviations (`FuseTest_Test`; the AY fitted on the 128K and +3) | R1-R3 |
| AC2 | `IN A,(C)` from #40FF at INT + 43046 (48K) / 43584 (128K) returns the attribute at #5A0F and takes 24 T | R1 |
| AC3 | On the 128K and +2 an IN from #7FFD / #3FFD in the border writes #3F; a locked latch ignores it; other ports leave it; the +3 has no read-cycle latch | R2 |
| AC4 | On the 128K and +2 a #BFFD read is undecoded with an AY fitted; on the +3 it returns the selected register | R3 |
| AC5 | Full build without warnings, every core test passes | all |
| AC6 | A/B of `BM_HostFrame_*` and `BM_PortIn` within noise for every machine | R4 |
| AC7 | unreal-z80: the IN ordering change made, all its suites pass (units, ZEX, z80test, T-trace, interrupt/contention harness, FUSE internal T-states, z80ex differential) | R5 |
