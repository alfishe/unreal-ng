# Memory Contention and Even M1: How the CPU Shares Memory with the Video

## Overview

On every ZX Spectrum and clone, the screen lives in the same RAM that the CPU uses. The CPU and the video
logic therefore both need that memory, and each machine settles the conflict in its own way. The chosen way
decides whether, and by how much, the CPU gets slower while the picture is drawn.

There are three answers:

1. **Contention.** The video logic has priority, and a CPU access to screen memory waits until the video logic
   lets go. This is the Sinclair way: Ferranti ULA (48K, 128K, +2) and Amstrad gate array (+2A, +3).
2. **Fixed time slots.** The memory is fast enough to serve two readers per CPU memory cycle, and a counter
   hands out the slots in a fixed order: video, CPU, video, CPU. The CPU always finds its slot, so it never
   waits. This is the way of the Soviet clones: Pentagon, Profi, ATM, Kay-1024 and the ZX-Evo in its default
   mode.
3. **Fixed slots, plus a fetch alignment.** The Scorpion uses fixed slots too, but an opcode fetch from RAM
   only lines up with the CPU's slot on one parity of the clock. The Scorpion inserts one wait state when a
   fetch from RAM would start on an odd T-state. This is called **Even M1**. It is not contention: it does
   not depend on the screen, only on the clock parity.

This document explains the mechanism, lists which machine does what, and describes how unreal-ng models it.
The research behind it, with every source, is in
[contention-by-machine.md](../../../inprogress/2026-09-28-m1-contention/contention-by-machine.md).

## Terms

| Term | Meaning |
|:--|:--|
| T-state (T) | One CPU clock period: 1/3.5 MHz, about 286 ns at normal speed. |
| M1 | The opcode fetch cycle. It is the first bus cycle of every instruction and of every prefix (`CB`, `DD`, `ED`, `FD`). It lasts 4 T: 2 T to read the opcode, 2 T in which the CPU refreshes the DRAM. |
| Memory read cycle | An ordinary data read (an operand, a `LD A,(HL)`). It lasts 3 T. |
| WAIT | A Z80 input. While it is active at the sampling edge, the CPU adds an extra T-state to the current memory or I/O cycle. |
| Contention | The video logic owns the memory for a while, and a CPU access waits until it lets go. The wait depends on the T-state and follows a repeating pattern such as 6,5,4,3,2,1,0,0. |
| Slot | A fixed share of memory time given to the CPU or to the video by a counter. |
| Even M1 | The Scorpion's wait: an opcode fetch from RAM that would start on an odd T-state waits one T. |
| DRAM | Dynamic RAM. It is addressed in two steps (row strobe RAS, column strobe CAS), and its data is valid only inside that strobe sequence. |

## Why ROM can be read at any T-state and RAM cannot

**ROM belongs to the CPU alone.** The video logic never reads it, so there is nobody to share it with. It is an
asynchronous chip with its own chip select: the CPU puts out the address and the read signal, and after the
chip's access time (a few hundred nanoseconds) the data on its outputs is valid and stays valid for as long
as the address is held. There are no slots, phases or strobes, so a fetch on any T-state gets correct data.

**RAM is dynamic and shared with the video.** The screen is in the same DRAM. On the Scorpion a counter clocked
at 7 MHz divides the time into slots: in one phase the RAS / CAS strobes are issued for the CPU, in the other
for the video. The DRAM delivers the CPU's data only inside the CPU's slot, that is, at one fixed phase of the
clock. Because this phase repeats with the parity of the T-state, only the parity of the moment when the
CPU wants the data matters.

**Why only M1 is affected.** The Z80 samples the data bus at different moments in different cycles (Zilog Z80
CPU User Manual, timing diagrams):

```
Memory read:  MREQ/RD active from the falling edge of T1 ...... to the falling edge of T3
              data latched on the falling edge of T3             window about 2 T
Opcode fetch: MREQ/RD active from the falling edge of T1 .. to the rising edge of T3
              data latched on the rising edge of T3              window about 1.5 T
              T3-T4: the refresh address is on the bus
```

- An ordinary **memory read** keeps its request up until the falling edge of T3 and latches the data there.
  Its window is long enough that the CPU's slot falls inside it at either parity.
- An **opcode fetch** is shorter: the data is latched on the rising edge of T3, half a clock earlier, and in
  T3-T4 the CPU already drives the refresh address. The slot covers this window at only one parity.

So the Scorpion's logic looks for "M1, and RAM selected, and the wrong phase" and inserts one WAIT. Data reads,
data writes, port accesses, the interrupt acknowledge and ROM fetches do not match that condition and never
wait.

> **How sure is this?** Who waits and who does not is confirmed by the Scorpion's own EPLD equations (SC15.1,
> 1996, reverse-compiled and posted on zx-pk.ru) and by programmers' experience ("on a Scorpion the T-state is
> always even on exit from HALT"). The explanation of why exactly M1 is our reading of those equations; no
> Scorpion document states it.

## Worked example

Code in RAM at `#8000`, starting on an even T-state:

| Instruction | T on a Pentagon | Next fetch would start on | Wait | T on a Scorpion |
|:--|--:|:--|--:|--:|
| `NOP` | 4 | even | 0 | 4 |
| `LD A,n` | 7 | odd | 1 | 8 |
| `INC HL` | 6 | even | 0 | 6 |
| `LD A,(IX+d)` | 19 | odd | 1 | 20 |
| `OUT (n),A` | 11 | odd | 1 | 12 |

The same code in ROM (for example inside the 48 BASIC ROM) runs at Pentagon speed. Every prefix M1 is 4 T,
so when the first fetch of a prefixed instruction is even, the second one is even too. For code that stays in
RAM, "round each instruction up to an even length" gives the same result as "align each fetch".

Compare the 48K: a `NOP` at `#4000` that starts 14335 T after the interrupt waits 6 T for the ULA and takes
10 T; the same `NOP` at `#8000` takes 4 T. On a Pentagon both take 4 T. On a Scorpion both take 4 T when they
start on an even T-state.

## Which machine does what

| Machine | Memory contention | I/O contention | Other CPU waits |
|:--|:--|:--|:--|
| 48K (Ferranti ULA) | `#4000`-`#7FFF`, 6,5,4,3,2,1,0,0, from 14335 T, also internal cycles | even ports and high byte `#40`-`#7F` | - |
| 128K, +2 (Ferranti ULA) | pages 1, 3, 5, 7 wherever mapped, from 14361 T | as the 48K, checked against the current mapping | - |
| +2A, +3 (gate array) | pages 4-7 in any slot, 1,0,7,6,5,4,3,2, memory cycles only | no | - |
| Pentagon 128 / 512 / 1024 | no | no | - |
| **Scorpion ZS-256** | **no** | **no** | **Even M1** on fetches from RAM (0 or 1 T), normal mode |
| Scorpion Turbo+ at 7 MHz | a different kind: every RAM access, read or write, waits for the next CPU slot (every 4 T in the paper, every 2 T in the border); an opcode fetch 1 T more | no | every I/O cycle 2 T |
| Profi | no | no | - |
| ATM Turbo 2+ | no | no | `IN (#FE)` waits for the keyboard controller; at 7 MHz every RAM access (fetch, read, write) waits 2 T from an even clock, 3 from an odd one, for the CPU's slot (`Atm710TurboOverlay`); ROM and I/O none, except the WD1793's ports (#1F / #3F / #5F / #7F): 1 T |
| ZX-Evo BaseConf | no in the Pentagon raster; a 48K-style pattern in its 48K / 128K rasters at 3.5 MHz | same condition | 14 MHz: a RAM read that misses the DRAM's two one-word caches waits 2 or 3 T; external I/O 3 T |
| ZX Spectrum Next | only in its 48K / 128K / +3 timing modes at 3.5 MHz | 48K / 128K modes | 1 wait per memory read at 28 MHz |

The research document gives the sources, the confidence of each row and more machines (Timex, Kay, Quorum,
Sprinter, Leningrad-1, Karabas-Pro).

## How unreal-ng models it

| Part | Where | What |
|:--|:--|:--|
| Contention rule | `ContentionRule` in `core/src/emulator/video/ulacontention.h`, chosen per model in `core/src/emulator/video/screen.cpp` | `Ula48`, `Ula128`, `GateArray` or `None` (every clone) |
| Even M1 flag | `config.even_M1`, set from the memory model in `core/src/emulator/config.cpp` (Scorpion and ProfScorp) | a property of the board, not an ini choice |
| Even M1 wait | `Z80Step` in `core/src/emulator/cpu/z80.cpp`, before an instruction's first M1 | if the fetch is from RAM (`PC >= #4000`, or RAM paged at `#0000`), the machine is not in turbo, and the current T-state is odd at the current clock rate: add one T |
| Tests | `EvenM1_Test` in `core/tests/emulator/cpu/z80_test.cpp`; `ContentionNegative_Test.ClonesNeverWait` in `core/tests/emulator/video/contention_test.cpp` | RAM fetch on an odd T waits, ROM never, RAM at `#0000` waits, turbo does not, other machines never |
| Turbo waits | host bus overlays (`core/src/emulator/memory/hostbusoverlay.h`), installed by the port decoder only while the machine runs in turbo, so every other machine and speed pays nothing: `EvoTurboOverlay` (`core/src/emulator/memory/atm/`, ZX-Evo at 14 MHz: the code and data cache words, the parity rule) and `ScorpionTurboOverlay` (`core/src/emulator/memory/scorpion/`, Scorpion Turbo+ at 7 MHz, the SC15.1 or SC15.3 slot rule). The overlays tell an opcode fetch from an operand read (`onReadM1`) and see the interrupt acknowledge; the decoders add the I/O waits. Both follow the `contention` feature | the rules and their derivation: `docs/inprogress/2026-09-29-machine-waits/` |
| Turbo wait tests | `EvoTurboOverlay_Test`, `ScorpionTurboOverlay_Test` in `core/tests/emulator/memory/`; `TurboTest_Test` runs the turbotest program (`tools/verification/contention/turbotest/`) on the Scorpion and the ZX-Evo | the worked examples of the research, simulated on the RTL (ZX-Evo) and on the logic chip's equations (Scorpion); turbotest's counts per frame against the research's per-instruction figures |
| Hardware probe | `tools/verification/contention/ctprobe` | times code at every T-state and compares with the expected tables; on a Scorpion it detects Even M1 and measures in 2 T-state steps (below) |

Not modeled: the ZX-Evo's optional Sinclair rasters. The Scorpion's turbo drops to 3.5 MHz while /INT is active, and its
other logic firmware, SC15.3, is the `[MISC] ScorpionTurboLogic` setting (both since 2026-10-01).

### Consequences for timing tools

A measuring engine that places code at an exact T-state (Jan Bobrowski's and Patrik Rak's `CODETIME`, used by
ctprobe and by Rak's Timing Test) needs delays of every length, odd ones included. On a Scorpion every
fetch from RAM starts on an even T-state, so an odd delay made of code in RAM cannot exist. Such engines hang or
report nonsense there. ctprobe checks for Even M1 before its engine runs (a loop of odd-length instructions
that fits in one frame without Even M1 and overruns it with Even M1). If it finds it, it switches to a delay
routine built only from instructions whose Even M1 length is known, in steps of 2 T-states, and corrects two
worked-out side effects on the rest of the engine; its README explains both. A Scorpion is then measured with
2 T-state resolution, which is all such a machine can resolve.

The same holds for demo code that synchronizes on `HALT` and counts T-states: on a Scorpion the T-state after
`HALT` is always even, and odd-length sequences in RAM are rounded up.

## Snow: when the refresh meets the ULA's fetch

The same shared DRAM has one more effect on the Ferranti ULA machines (16K, 48K, 128K, +2). Every opcode fetch
ends with a refresh cycle whose address is `I`:`R`. When `I` points into the slow memory (#40-#7F; on the
128K / +2 also #C0-#FF with an odd page there), the refresh goes to the screen's DRAM chips. If its first tick
(T3) falls on the tick the ULA fetches a group's first pixel byte, the ULA takes the low 7 address bits of that
pixel byte and its attribute from `R`: the cell shows another cell's contents ("snow"). If T3 falls on the
second pixel byte's fetch, the second pair is not read and the second cell repeats the first ("double"). The
+2A / +3 and the clones do not snow.

In unreal-ng: `Z80::NoteRefresh` (every M1, the INT and NMI acknowledge) calls `UlaContention::NoteRefresh`
when `ioContention` is set (the Ferranti ULA machines) and `I` points into a slow slot. The ULA model marks the
cell for the frame; the renderer (`ScreenZX::DrawRangeZX`, `RenderScreen_Batch8`) and the floating bus use the
bytes the ULA really fetched (`UlaContention::SnowOffsets`). The tick and the value of `R` (before its increment)
were fixed on photos of the test program Snow Hold from three real 48K machines; the test program
[snowtest](../../../../tools/verification/contention/snowtest/README.md) shows the effect next to a drawing of
what it should look like. Research, design and tests:
[docs/inprogress/2026-09-29-ula-snow](../../../inprogress/2026-09-29-ula-snow/research.md).

## Open questions

- Which Scorpion boards have Even M1: the 1996 turbo-board equations have it, a 2007 re-creation of that chip
  does not, and the earliest boards are unknown. A real-machine measurement settles it.
- Whether the turbo mode still aligns M1, and the exact turbo wait pattern during the paper.

The full list is in the research document, section 13.
