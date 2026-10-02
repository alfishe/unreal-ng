# turbotest - how fast a Scorpion's turbo really is

A ZX Spectrum test program for the **Scorpion ZS-256 Turbo+**. It measures how much work the CPU gets done in
one frame at 3.5 MHz and at 7 MHz (turbo), and tells which of the two logic chip firmwares the machine has,
SC15.1 or SC15.3.

## Why turbo is not simply twice as fast

In turbo the Scorpion's CPU runs at 7 MHz, but its memory does not get faster. A small logic chip (DD30, an
EP220) lets the CPU at the RAM only in its own time slots: every 4 ticks while the picture is drawn (the
screen hardware needs the RAM in between), every 2 ticks in the border. When the CPU wants the RAM between two
slots, the chip holds it with the WAIT signal until the next one. The ROM does not wait.

The chip came with two firmwares, and they differ:

| | SC15.1 | SC15.3 |
|:--|:--|:--|
| Opcode fetch from RAM in turbo | waits for the slot, and always one tick more | waits for the slot only |
| Read or write of RAM in turbo | waits for the slot | waits for the slot |
| Port access (`IN`, `OUT`) in turbo | 2 ticks longer | 1 tick longer |
| At 3.5 MHz | "Even M1": opcode fetches from RAM start on even ticks only | no waits |

So a `NOP` stream in RAM, 4 ticks per `NOP` at full speed, takes 8 ticks per `NOP` with SC15.1 while the picture
is drawn: no faster than at 3.5 MHz. With SC15.3 it takes 4. Code in ROM gets the full doubling with both.
The details, derived from both firmwares' fuse maps:
[research-scorpion-turbo.md](../../../../docs/inprogress/2026-09-29-machine-waits/research-scorpion-turbo.md).

Nobody has measured this on a real machine yet. This program is meant to do it.

## How to run it

| File | How |
|:--|:--|
| `turbotest.trd` | `RUN` in TR-DOS |
| `turbotest.tap` | `LOAD ""` from 48 BASIC |

It takes under a second and switches turbo back off at the end. Standalone from your own loader:
`CLEAR 35999`, load the code at 36000, `RANDOMIZE USR 36000`.

## What you see

```
turbotest: bodies per frame

Measured   3.5MHz  turbo
NOP         15506  19168
LD A,(RAM)   8209  12316
LD A,(ROM)   8210  16000
LD (RAM),A   8209  12316
OUT (FE),A   5583   7791
SC15.1     3.5MHz  turbo
NOP         15506  19168
...
SC15.3     3.5MHz  turbo
NOP         15507  30382
...

Turbo+ logic: SC15.1
```

- **Measured**: how many times each piece of code (a "body") ran in one frame, at 3.5 MHz and in turbo.
- **SC15.1**, **SC15.3**: the counts each firmware gives in unreal-ng's model of its equations.
- The last line: `Turbo+ logic: SC15.1` or `SC15.3` when every count matches that table exactly,
  `Matches neither firmware` otherwise, `Turbo makes no difference` when the turbo column equals the 3.5 MHz one,
  `Not a Scorpion: turbo not tried` on other machines (below).

The five bodies:

| Body | What it tests |
|:--|:--|
| `NOP` | opcode fetches from RAM only |
| `LD A,(RAM)` | a fetch and a read from RAM |
| `LD A,(ROM)` | a fetch from RAM and a read from ROM (the ROM does not wait) |
| `LD (RAM),A` | a fetch and a write to RAM |
| `OUT (FE),A` | a port write (the border stays black) |

How to read a difference, with an example: if the `NOP` turbo count is 30382 but `LD A,(RAM)` is far from
both tables, the opcode fetches follow SC15.3 but data reads wait differently than modeled. Every count is
exact to one body, and one tick more or less per body changes a count by several percent, so small
differences matter.

## How it measures

The program waits for the frame interrupt (`HALT`). The interrupt handler starts a loop: 32 copies of the body,
then `INC DE` and `JP` back. The next frame's interrupt stops it. The stop handler reads the loop counter (`DE`)
and the address the CPU was at, which tells how many bodies of the last pass were done:
count = `DE` x 32 + done.

Worked example, SC15.1, `NOP` in turbo: a pass is 32 `NOP`s and the tail. In the picture a `NOP` takes 8
ticks, in the border 6; the tail takes about 26 and 22. The picture is 192 lines of 256 of the 448 turbo ticks
per line, 49152 of the frame's 139776 turbo ticks:

```
49152 / (8 + 26/32) + (139776 - 49152) / (6 + 22/32) = 5577 + 13551 = 19128
```

The program counts 19168. The rest is the interrupt (at 3.5 MHz: the Turbo+ logic drops the clock while /INT
is active) and the exact slot phases.

## Other machines

Reading port `#7FFD` or `#1FFD` (the Scorpion's turbo switches) can change the memory paging on a 128K or a grey
+2. So the program first counts `NOP`s at whatever speed the machine runs (a Scorpion's ROM leaves turbo on) and
switches turbo only when that count is a Scorpion's, at either speed. The 128K, the +2, the Pentagon and
anything with another frame length get `Not a Scorpion`. A 48K has the Scorpion's frame and passes the
check; the reads are harmless there, and the result is `Turbo makes no difference`.

## Results so far

Emulators, through the [coemu harness](../../coemu/README.md) (`PROGRAM=.../turbotest/turbotest run-all.sh
scorpion`), 2026-10-01:

| Emulator | 3.5 MHz | Turbo |
|:--|:--|:--|
| unreal-ng | SC15.1 (its default; SC15.3 with `[MISC] ScorpionTurboLogic=SC15.3`) | SC15.1 |
| MAME, ZXMAK2, Kozynax, xpeccy-plus | as SC15.1 (Even M1), to one body | no turbo |
| Xpeccy | as SC15.3 (no Even M1) | exactly twice the 3.5 MHz counts: turbo without waits |
| ZX-M8XXX | 2.6 % more: its Scorpion has the Pentagon's 71680-tick frame | not tried |
| FUSE, ZEsarUX, SkoolKit | no Scorpion that loads the program | |

**Real machines: none yet.** If you have a Scorpion Turbo+, please send a photo of the screen with the board
revision and the marking of the logic chip (DD30); see [What to send back](../README.md#what-to-send-back).

## Files and rebuilding

| File | Content |
|:--|:--|
| `turbotest.trd`, `turbotest.tap` | the program, loading at 36000 |
| `turbotest.sym` | every label with its address (`DONE`, `MATCH`, `COUNTS`, `PROBEEND`, ...) |
| `turbotest-compare.py` | reads a memory dump of a finished run (from `START` to `PROBEEND`) and prints the counts next to both tables |
| `turbotest.asm` | the source; its expected tables are zero |

The files are built by unreal-ng's test suite (`core/tests/emulator/memory/scorpion/turbotest_test.cpp`): it
assembles the source, runs it on a Scorpion under each firmware to fill in the two tables, and checks those
counts against the research's per-instruction figures. `UNREAL_TURBOTEST_EXPORT=1` writes the files; a test
fails when the committed ones get out of step with the source or the model.

For programs that run it themselves: `SHOW` (print the report) and `FORCE` (1: measure turbo without the
Scorpion check) can be set before starting at `HOSTENTRY`. At the end `DONE` is 1, `MATCH` is 1 (SC15.1),
2 (SC15.3), 0 (neither), `#FF` (turbo makes no difference) or `#FE` (not a Scorpion), and `FAILS` holds the
number of counts that differ from the closer table.
