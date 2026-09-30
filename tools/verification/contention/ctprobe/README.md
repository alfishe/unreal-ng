# ctprobe - memory contention probe for ZX Spectrum machines and emulators

ctprobe is a small program for the ZX Spectrum. It measures, to a single CPU clock tick, how long pieces of
code take when they touch the memory the screen hardware also reads. It then compares every measurement with
what a real machine does, and prints `OK` or `BAD` for each check.

Use it to answer one question: **does this machine (or this emulator) slow the CPU down exactly like the real
hardware?** Demos, multicolor effects, some loaders and many games depend on that timing.

## Contents

- [Quick start](#quick-start)
- [What it measures, in plain words](#what-it-measures-in-plain-words)
- [Reading the screen](#reading-the-screen)
- [The checks, one by one](#the-checks-one-by-one)
- [How a BAD line is worked out: an example](#how-a-bad-line-is-worked-out-an-example)
- [Something is BAD: what to do](#something-is-bad-what-to-do)
- [Results so far](#results-so-far)
- [Where the expected values come from](#where-the-expected-values-come-from)
- [Files and rebuilding](#files-and-rebuilding)
- [Glossary](#glossary)

## Quick start

| Machine | File | How to start |
|:--|:--|:--|
| 48K | `ctprobe.tap` | `LOAD ""` |
| 128K / +2 | `ctprobe.tap` | `LOAD ""` from **128 BASIC** (in 48 BASIC the page checks are skipped) |
| +2A / +3 | `ctprobe.tap` | from **+3 BASIC**: `LOAD "t:"`, then `LOAD ""` (in 48 BASIC the page and layout checks are skipped) |
| Pentagon, Scorpion, ATM Turbo 2+, Profi | `ctprobe.trd` | `RUN` in TR-DOS (the tape works too) |
| ZX-Evo (BaseConf) | `ctprobe.trd` | reset with SPACE held (straight to TR-DOS), then `RUN`. The CPU must be at 3.5 MHz: in the Evo Reset Service menu, W steps 3.5 / 7 / 14 MHz |

The program needs the CPU at 3.5 MHz. A Scorpion's turbo is switched off by the program itself. On other
machines (ATM Turbo, ZX-Evo) it stops and says so: "The CPU runs faster than 3.5 MHz. Switch the machine to
3.5 MHz (turbo off) and run it again."

**The Scorpion and "Even M1".** A real Scorpion (and unreal-ng's since 2026-09-29) makes every opcode fetch
from RAM start on an even clock tick: a fetch that would start on an odd tick waits one ("Even M1", from its
circuit). So every instruction run from RAM takes its length rounded up to an even number of ticks: `LD A,n`
takes 8, not 7. The program checks for it first: after a screen interrupt it runs a loop that takes 65931
ticks, which ends within the frame; with Even M1 the same loop takes 73236 ticks and runs past the next
interrupt. If so, it switches its measuring engine to delays in steps of 2 ticks (the only steps such a
machine can make; see "Where the expected values come from"), and the machine type becomes `attr bus,
Even M1`, with its own table: a Scorpion with Even M1 is checked against what Even M1 does, and a Scorpion
without it (the 2007 re-creation of its logic chip, or an emulator that leaves it out) against the plain
Scorpion table.

A full run takes **about 3 minutes** at the normal 3.5 MHz speed. An emulator's fast or turbo mode is fine: the
program measures CPU clock ticks, not seconds.

At the end the border turns **green** if every value was as expected and **red** otherwise.
`PRINT USR 36000` instead of `RANDOMIZE USR 36000` also prints the number of wrong values.

## What it measures, in plain words

The Spectrum's screen lives in ordinary memory, at addresses `#4000`-`#7FFF`. While the TV picture is being
drawn, the ULA (the chip that makes the picture) reads that memory. On a 48K, 128K, +2, +2A or +3 the CPU and
the ULA share it: when the CPU wants that memory at the moment the ULA is reading it, the CPU is **held back**
for a few clock ticks. This is called **contention**.

How long the CPU waits depends on the exact clock tick. It repeats every 8 ticks along each screen line:

| Machine | Waits for the 8 ticks of each group |
|:--|:--|
| 48K, 128K, +2 (Ferranti ULA) | 6, 5, 4, 3, 2, 1, 0, 0 |
| +2A, +3 (Amstrad gate array) | 1, 0, 7, 6, 5, 4, 3, 2 |
| Pentagon, Scorpion and other clones | no waits at all (a Scorpion rounds each instruction run from RAM up to an even number of ticks, Even M1, above) |

The waits only happen while the picture itself (not the border) is drawn: 128 ticks of every one of the 192
screen lines. On the +2A / +3 the gate array holds the CPU for one more tick after that.

ctprobe runs a short piece of code (a **fragment**) starting at one exact tick, measures how long it took,
then starts it again one tick later, and so on. The row of numbers it gets shows the wait pattern. Getting a
single tick wrong shows up at once.

## Reading the screen

The first lines describe the machine as the program detected it:

```
ctprobe 1 - contention probe
Machine: ULA 48K
Frame 69888, onset 14335
Paging no, +3 layouts no
Takes about 3 min at 3.5 MHz
```

| Line | Meaning |
|:--|:--|
| `Machine:` | The contention type it found: `ULA 48K`, `ULA 128K`, `gate array` (+2A/+3), `no contention` (Pentagon, ATM, Profi and similar), `no contention, attr bus` (Scorpion without Even M1), `attr bus, Even M1` (Scorpion) |
| `Frame` | Clock ticks from one screen interrupt to the next: 69888 on a 48K and a Scorpion, 70908 on a 128K / +2 / +2A / +3, 71680 on a Pentagon |
| `onset` | The first contended tick after the interrupt: 14335 (48K), 14361 (128K and later) |
| `Paging` | Whether it could switch 16K memory pages at `#C000`. `no` on a 48K, and also on a 128K or +3 started from 48 BASIC, which locks the paging |
| `+3 layouts` | Whether it could use the +2A / +3 "all RAM" memory layouts |

If `Machine:` is not what you expect (for example `no contention` on a 48K), the emulator does not contend
memory at all, or the probe started in a wrong state. Every other line depends on it.

Then one line per check, as it runs, about every 5 seconds:

```
M1-01 OK
M1-03 BAD T14453 got 5 exp 4
M1-P0 skipped as N/A
```

| Line | Meaning |
|:--|:--|
| `M1-01 OK` | Every measurement of this check matched a real machine |
| `M1-03 BAD T14453 got 5 exp 4` | At least one did not. It shows the **first** wrong one: the fragment was started at tick **14453** after the interrupt, took **5** ticks more than its bare length, where a real machine takes **4** |
| `M1-P0 skipped as N/A` | This check needs something the machine does not have or has locked (memory pages, the +3 layouts) |

At the end the machine lines are shown again, then the total:

```
ALL VALUES AS EXPECTED
```

or

```
367 VALUES WRONG
```

A check has up to 20 measurements, one per tick, so one wrong check can add up to 20 to the total.

### What `got` and `exp` are

For all checks but `P-02`, `got` and `exp` are durations in clock ticks (T-states), counted from the fragment's
first instruction to its end, **including** any wait of the `RET` that follows it. The program subtracts the
`RET`'s own 10 ticks, but not its wait.

For `P-02`, `got` and `exp` are the byte read from port `#FF` (the "floating bus"), not a duration. 255 means
"nothing on the bus".

`T` is the tick, counted from the start of the screen interrupt, at which the fragment started. Ticks are
counted the way FUSE and most emulators count them: the 48K's first contended tick is 14335.

## The checks, one by one

"Contended memory" below means `#4000`-`#7FFF` (plus the contended pages at `#C000` on the 128K and +2A/+3).

| Code | What runs, and where | What it checks |
|:--|:--|:--|
| M1-01 | `NOP` at `#4000`, 20 ticks in a row from just before the first contended tick | The basic wait pattern of an opcode fetch |
| M1-02 | `NOP` at `#4000` while the top border is drawn | No waits outside the picture area |
| M1-03 | `NOP` at `#4000` around the end of the first screen line | Where the waits stop at the end of a line (the gate array's extra tick) |
| M1-04 | `LD A,n` with the opcode at `#7FFF` and its operand at `#8000` | The opcode fetch waits; the operand read just above contended memory does not |
| M1-5A | `RLC B` (a `CB` instruction) at `#4000` | Both opcode fetches of a prefixed instruction wait |
| M1-5B | `NEG` (an `ED` instruction) at `#4000` | Same, for the `ED` prefix |
| M1-5C | `INC IX` / `DEC IX` at `#4000` | Same, for the `DD` prefix |
| M1-06 | `RLC (IX+0)` at `#4000` | Two opcode fetches wait; the displacement and the fourth byte are plain reads |
| M1-07 | `DD DD DD NOP` at `#4000` | Every prefix is an opcode fetch of its own |
| M1-P0 .. P7 | `RET` at `#C000` with memory page 0 .. 7 mapped there | 128K: odd pages are contended. +2A/+3: pages 4-7. Clones: none |
| M1-L0 .. L3 | Code that switches the +3 to "all RAM" layout 0 .. 3, runs, reads `#0000` and `#C000`, and switches back | +2A/+3: pages 4-7 are contended in any 16K slot |
| D-01A | `LD A,(#4000)` run from ordinary memory | A data read waits |
| D-01B | `LD (#4000),A` run from ordinary memory | A data write waits |
| D-02 | `LD A,(#4000)` placed at `#4000` itself | Both the fetch and the read wait |
| D-03 | `PUSH BC` / `POP BC` with the stack in contended memory | Stack accesses wait |
| D-04 | `LDI` copying from `#4000` to `#4100` | Each read and write of a block copy waits |
| N-01 | `INC (HL)` with `HL` = `#4000` | The extra internal tick of a read-modify-write instruction waits (48K / 128K only) |
| N-02 | `JR +0` at `#4000` | The 5 internal ticks of a taken jump wait |
| N-03 | `EX (SP),HL` with the stack in contended memory | Its internal ticks on the stack address wait |
| N-04 | `ADD HL,BC` with register `I` = `#40` | Ticks that put `I` on the address bus wait when `I` points at the screen (why programs should not set `I` to `#40`-`#7F` on a 48K) |
| N-05A | `LDIR` copying 2 bytes in contended memory | The 5 extra ticks of the repeat wait |
| N-05B | `CPIR` over 2 bytes in contended memory | Same, for a search |
| N-06 | `LD A,(IX+0)` at `#4000` | The 5 internal ticks spent on the displacement wait |
| P-01A | `IN A,(C)` from port `#00FE` | Port access: the ULA port (even address), high byte outside contended memory |
| P-01B | `IN A,(C)` from port `#40FE` | ULA port, high byte inside contended memory |
| P-01C | `IN A,(C)` from port `#00FF` | Other port, high byte outside |
| P-01D | `IN A,(C)` from port `#40FF` | Other port, high byte inside: the CPU waits four times |
| P-01E | `OUT (C),A` to port `#40FE` | Same rules for a write |
| P-03A | `OUT (C),A` to port `#00FE` | Write to the ULA port, high byte outside contended memory: 1 free tick, then a contended one |
| P-03B | `OUT (C),A` to port `#00FF` | Write to another port, high byte outside: never waits |
| P-03C | `OUT (C),A` to port `#40FF` | Write to another port, high byte inside: waits four times |
| P-03D | `IN A,(#FE)` with `A` = `#40` | The same rules when the port's high byte comes from `A` (port `#40FE`) |
| P-03E | `OUT (#FE),A` with `A` = `#40` | Same, for a write |
| P-04A | `INI` from port `#40FE` | Block input: the port read, then the write to memory |
| P-04B | `OUTI` to port `#40FE` (`B` = `#41`) | Block output: `B` goes down **before** the port is written, so the port is `#40FE` |
| P-04C | `INIR` over 2 ports | The repeat and its 5 extra ticks |
| P-04D | `OTIR` over 3 ports | The repeat and its 5 extra ticks |
| P-05A..D | `IN A,(C)` from ports `#C0FE` and `#C0FF`, with page 0 and then page 1 at `#C000` | 128K / +2: a port whose high byte points at an odd page at `#C000` waits like a contended one. **Not yet confirmed on real hardware**: this rule is what FUSE and the other emulators do; no photographed test covers it |
| P-02 | `IN A,(#FF)` at 20 ticks around the start of the picture | The byte the screen hardware is reading at that tick (the "floating bus"). The program first writes a known pattern into the first screen cells. Skipped on the plain clones (Pentagon, ATM, Profi): there an unused port's answer depends on the machine |
| X-02 | A `RET` inside the ROM | Code in ROM never waits |

The 48K and the clones skip the page and layout checks. The 128K skips the layout checks.

## How a BAD line is worked out: an example

Take a line from a 48K run in an emulator:

```
M1-01 BAD T14333 got 7 exp 8
```

M1-01 runs a `NOP` at `#4000`, then the `RET` the program puts right after it at `#4001`. Both are in contended
memory. On a real 48K, started at tick 14333:

1. The `NOP` is fetched at tick 14333. That is two ticks before the first contended tick (14335), so it does
   not wait. It takes its normal 4 ticks, until 14337.
2. The `RET` is fetched at tick 14337. That is the third tick of the first group (14335 is the first), and the
   pattern `6, 5, 4, 3, ...` gives a wait of **4** there.
3. The result is 4 (the `NOP`) + 4 (the `RET`'s wait) = **8**. That is `exp`.

The emulator measured 7. Its `RET` waited 3 ticks, which is what a real 48K does one tick **later**, at 14338.
So in that emulator everything happens one tick later than on the hardware, relative to the start of the
interrupt.

To see that, look at all the values of the check at once, not just the first wrong one. On the machine you
test they appear on screen only as that first one; see the next section for getting the whole row.

## Something is BAD: what to do

1. **Check the machine settings first.** Many emulators have "early" / "late" ULA timing, a machine subtype, or
   an option that switches contention off. The expected values are for the "early" timing of the 48K and 128K
   (the common one). Pick the right machine: a 128K is not a +2A.
2. **Run Patrik Rak's Timing Test** (`testdata/contention/rak-timing-test/timing.tap` in the unreal-ng tree, test `0`, "contended NOP"). Its pictures
   were photographed on real machines. On a 48K with early timing the row `14328` must read
   `4 4 4 4 4 4 4 4` and the row `14336` must start with `10`. If the emulator shows `10` at the end of row
   `14328` instead, it runs one tick off relative to the interrupt, and most ctprobe checks fail for that one
   reason. ctprobe and this test measure time the same way, so they always agree.
3. **Look at the pattern of what fails**:

   | What you see | What it usually means |
   |:--|:--|
   | Almost every check BAD, only `M1-02`, `P-01C`, `P-03B` and `X-02` OK (on the +2A/+3 also the other port checks and the page checks of pages 0-3) | Everything is shifted by a few ticks: those checks never wait, so a shift cannot show there. The interrupt, or the start of contention, is off by that many ticks |
   | Only `M1-03` BAD | The end of each line is wrong: the wait window is one tick too short or too long (the +2A/+3 gate array holds one tick longer than the ULA) |
   | Only `M1-P*` BAD | Wrong pages are contended at `#C000` (128K: odd pages; +2A/+3: pages 4-7) |
   | Only `M1-L*` BAD | The +3 "all RAM" layouts: pages 4-7 must be contended in every slot |
   | Only `D-*` BAD | Data reads / writes do not wait, but opcode fetches do |
   | Only `N-*` BAD | The internal ticks (no memory access, only an address on the bus) are not contended. Right on the +2A/+3, wrong on the 48K / 128K |
   | Only `P-01*`, `P-03*` BAD | Port access timing (the four port patterns) |
   | Only `P-04*` BAD | Block I/O: the order of the port and memory cycles, or when `B` goes down in `OUTI` / `OTIR` |
   | Only `P-05*` BAD | Whether a port's high byte counts as contended when it points at the page at `#C000` (128K / +2); also check the note on P-05 above |
   | Only `P-02` BAD | The floating bus: wrong byte, or right byte one tick off |
   | `Machine: no contention` on a 48K / 128K / +3 | Contention is switched off or not emulated; every contended check will fail |

4. **Get every value, not just the first wrong one.** When the program has finished, the measurements are in
   memory. A debugger or an emulator's memory dump of `36000` (`START`) to `PROBEEND` (addresses in `ctprobe.sym`)
   is enough; the case table in that same memory says which bytes belong to which check. Comparing the whole
   row with the expected row shows a shift at once (in the example above every value is the expected value of
   the tick before). `ctprobe-compare.py` does it for you:

   ```
   python3 ctprobe-compare.py dump.bin
   ```

   ```
   N-06: from T14359, one value per tick
     got  29  28  35  34  33  32  39  38  37  36  35  34  33  32  39  38
     exp  35  34  33  32  39  38  37  36  35  34  33  32  39  38  37  36
     = the expected row shifted: this machine behaves 2 tick(s) later than a real one
   ```
5. **Report it with the full line**, the machine, the emulator's version and its timing settings.

## Results so far

Run by the [co-emulation harness](../../coemu/README.md), with each emulator's stock settings, 2026-09-29 (Kozynax,
ZX-M8XXX and spec_chum 2026-09-30; each runner's README explains its differences):

| Emulator | 48K | 128K | +2 | +2A / +3 | Pentagon | Scorpion |
|:--|:--|:--|:--|:--|:--|:--|
| unreal-ng | all as expected | all as expected | all as expected | all as expected | all as expected | all as expected (attr bus, Even M1) |
| xpeccy-plus 7a96d8da | all as expected | P-05 only: a port whose high byte points at an odd page at `#C000` does not wait | as the 128K | the gate array's waits come 2 ticks late; the extra tick at the end of each line is missing | all as expected | P-02 only, 2 ticks late: its `scrp.wait` adds the Even M1 tick at the end of an odd-length instruction, before it looks at the interrupt (see below) |
| FUSE 1.6.0 | all as expected | all as expected | all as expected | the extra tick at the end of each line is missing (1 value) | all as expected | - |
| MAME 0.289 | all as expected | everything 2 ticks late | as the 128K | the waits 4 ticks late; port accesses wait, which the gate array does not do | all as expected | all as expected (Even M1, no attr bus) |
| ZEsarUX 13.0 | floating bus (P-02) only | floating bus (P-02) only | floating bus (P-02) only | the waits 4 ticks late; internal ticks wait, which the gate array does not do | all as expected | - |
| SkoolKit 10.1 | floating bus (P-02) only: unused ports always read `#FF` | floating bus (P-02) only | - | - | - | - |
| ZXMAK2 | everything 1 tick late | everything 1 tick late | - | +3: the +3 layouts and the floating bus (10 checks) | all as expected | all as expected (Even M1, no attr bus) |
| Xpeccy (upstream) | many values differ (33 checks) | many values differ (40 checks) | as the 128K | many values differ (33 checks) | all as expected | all as expected (Even M1 off by default, no attr bus) |
| Kozynax | everything 1 tick late (its stock ULA is the "late" one), byte-identical to ZXMAK2 | as the 48K | - | +3: the 128K's pages wait, the +3 layouts, the extra tick at the end of each line, the floating bus (10 checks) | all as expected | all as expected (Even M1, no attr bus) |
| ZX-M8XXX 26.09.03 | internal ticks of indexed instructions, `CPIR`, `EX (SP),HL` and chained prefixes; `IN r,(C)` / block I/O port cycle placed early; floating bus 11 ticks late (14 checks) | as the 48K, and ports never wait, no floating bus (18 checks) | as the 128K | the extra tick at the end of each line, chained prefixes, `EX (SP),HL` (3 checks) | all as expected | timed as a Pentagon: no Even M1, no attr bus |
| spec_chum 0.7.0 | internal ticks (an address on the bus, no memory access) never wait (9 checks) | as the 48K | as the 48K | the 128K's wait pattern, so the probe finds a 128K (29 checks) | all as expected (from tape) | does not load: its TR-DOS ROM stays paged in after a call through RAM |

unreal-ng also runs it on the Scorpion with ProfROM, the ATM Turbo 2+, the ZX-Evo and the Profi: all as
expected; xpeccy-plus on its ATM Turbo 2+, ZX-Evo and Profi, ZXMAK2 and Xpeccy on theirs and on the Scorpion with
ProfROM, and MAME on its Scorpion with ProfROM: all as
expected. MAME's ATM Turbo boots at 7 MHz (the probe stops and asks for 3.5 MHz).

**The Scorpion.** Three emulators model Even M1, each in its own way: unreal-ng (a wait inside the opcode
fetch), MAME and ZXMAK2 (a wait in the opcode read), xpeccy-plus (the instruction's length rounded up at its
end). All give the same durations, so the measuring engine's Even M1 mode is not tuned to one of them. They
differ in when the interrupt is looked at: the Z80 samples it at the end of an instruction, and the Scorpion's
WAIT for the next fetch comes after that, as in unreal-ng, MAME and ZXMAK2; xpeccy-plus adds the tick first.
That moves the measuring engine's start by 2 ticks, which only the floating-bus check P-02 can see, and only
unreal-ng and xpeccy-plus have the Scorpion's attribute bus. A real Scorpion decides it.

On P-05 the emulators disagree: FUSE, SkoolKit and unreal-ng contend such a port, xpeccy-plus does not. A
real 128K decides it; until someone runs the probe on one, treat P-05 as open.

SkoolKit is a disassembly toolkit with a Z80 contention simulator, not a full emulator: it has only the 48K and
the 128K, and no floating bus. Everything it does model agrees with the expected values, including P-05.

`-`: the emulator lacks the machine, or its runner or ROMs do not cover it yet. Real hardware: not run yet. Please
send results.

## Where the expected values come from

The expected values do not come from any emulator. They are computed from:

- the wait patterns in the table above and the tick where contention starts (the consensus of the emulators
  that have been checked against real machines: FUSE, ZXMAK2, Xpeccy, MAME, and the photographs of Rak's
  test);
- the picture geometry: the ticks per line and where the picture starts;
- for each instruction, the list of memory, internal and port cycles it makes, from FUSE's instruction
  tables.

The program detects which kind of machine it runs on and uses that machine's table. It times a `NOP` over 16
ticks from the first contended tick: the largest wait is 6 on the Ferranti ULA, 7 on the gate array, 0 without
contention. That holds even when an emulator's timing is a few ticks off. The frame length then tells the 48K
from the 128K. Without contention it reads port `#FF` while the picture is drawn. The Scorpion answers with
the attribute byte the screen hardware is reading (described in its programmer's manual); the exact tick grid
used here, one cell per 4 ticks, is unreal-ng's model and is not yet checked on a real Scorpion. Other clones
answer `#FF`, or a device's byte. On those plain clones the floating-bus check P-02 is skipped as N/A: what an
unused port reads depends on the machine and its mode.

**Even M1 in the expected values and in the engine.** On a Scorpion with Even M1 the expected durations add,
before every opcode fetch from RAM (the fragment's first one included: the wait is inside that fetch's
cycle), one tick when the fetch would start on an odd tick, and a `RET` in ROM (X-02) one tick when the
engine's next fetch back in RAM would. The measuring engine needs delays of every length, and on such a
machine none of its instructions in RAM can take an odd number of ticks, so it runs with `DELAYE`, a delay
built only from instructions whose Even M1 length is known, in steps of 2 ticks. The rest of the engine is
unchanged, and two effects of Even M1 on it are worked out and corrected (`TIMEIT` in `ctprobe.asm`):
its frame measurement relies on the interrupt landing on a different tick in each of four passes, which
Even M1 prevents, and comes out exactly 2 ticks short whatever the phase; with that frame length the engine
starts code 2 ticks early and reports durations 4 ticks short. The floating-bus check P-02 shows where the
code really runs, so the start correction is measured, not assumed. On every other machine the corrections
are 0.

The measuring engine is Jan Bobrowski's, as adjusted by Patrik Rak for his Timing Test (GPL). It is the same
code, byte for byte, as in Rak's test, whose results were photographed on real 48K, 128K, +2A and +3 machines.

## Files and rebuilding

| File | Content |
|:--|:--|
| `ctprobe.tap` | Tape: a BASIC loader and the code at 36000 |
| `ctprobe.trd` | TR-DOS disk: `boot` loads and runs the same code |
| `ctprobe.sym` | Every label with its address, for debuggers (for example `DONE`, `RESULTS`, `PROBEEND`) |
| `ctprobe-compare.py` | Reads a memory dump of a finished run and prints every check that differs, with the whole row of values |
| `ctprobe.asm` | The program: machine detection, the checks, the output |
| `engine.asm` | The measuring engine (Bobrowski / Rak, GPL) |
| `Makefile` | Rebuild the files, run the checks, compare a dump, run the probe on other emulators |
| `../../coemu/` | Runs the probe on every emulator it finds (unreal-ng, xpeccy-plus, ...) and puts the results in one table ([README](../../coemu/README.md)) |

```
make files                                  # rebuild ctprobe.tap / .trd / .sym
make check                                  # the files match the sources; one quick 48K run
make test                                   # every check on every machine in unreal-ng, and the files loaded as a user does
make compare DUMP=out.bin                   # compare a memory dump with the expected values
make coemu                                  # the same run on every emulator found (see ../../coemu)
```

The `.tap`, `.trd` and `.sym` are built from the two `.asm` files by unreal-ng's test suite
(`core/tests/emulator/video/ctprobe_test.cpp`), which has the assembler and the oracle that fills in the
expected values. `BUILD` points the Makefile at an unreal-ng build directory configured with `-DTESTS=ON`
(default: `cmake-build-agent-release` at the tree's root). A test fails when the committed files get out of
step with the source.

For programs that run the probe themselves: the settings at the start of the code (`ONSET`, `CLASS`, `CAPS`,
`DEF7FFD`, `DEF1FFD`, `ONLY`, `SHOW`; see `ctprobe.asm`) can be set before starting at `HOSTENTRY`. `DONE`
becomes 1 at the end and `FAILS` holds the number of wrong values.

## Glossary

| Term | Meaning |
|:--|:--|
| T-state, tick | One CPU clock tick: 1/3,500,000 of a second at 3.5 MHz |
| Frame | The time from one screen interrupt to the next (1/50 s) |
| Interrupt (INT) | The signal the screen hardware sends at the start of every frame; all ticks are counted from it |
| Contention | The CPU being held back because the screen hardware is reading the same memory |
| Onset | The first tick of a frame at which contention can happen |
| ULA / gate array | The chip that draws the picture: Ferranti ULA on the 48K / 128K / +2, Amstrad gate array on the +2A / +3 |
| Opcode fetch (M1) | The CPU reading an instruction byte |
| Internal tick | A tick in which the CPU accesses no memory but leaves an address on the bus; the 48K / 128K ULA still contends it |
| Page | One of the 16K memory banks of a 128K-type machine, switched in at `#C000` through port `#7FFD` |
| Layout | A +2A / +3 mode (port `#1FFD`) that fills all four 16K slots with RAM pages |
| Floating bus | What the CPU reads from a port nobody answers: on a 48K / 128K, the byte the ULA is reading from screen memory at that moment |
