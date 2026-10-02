# Memory timing test programs, and what only real hardware can answer

Three ZX Spectrum programs of this project check how a machine shares its memory between the CPU and the picture,
and a fourth one, FUSE's own timing test, is built here from its source:

| Program | Checks | Files |
|:--|:--|:--|
| [ctprobe](ctprobe/README.md) | contention (how long the CPU waits for the screen), the floating bus, the Scorpion's "Even M1" | `ctprobe/ctprobe.tap`, `ctprobe/ctprobe.trd` |
| [snowtest](snowtest/README.md) | ULA snow (a picture corrupted by the CPU's memory refresh) | `snowtest/snowtest.tap`, `snowtest/snowtest.trd` |
| [turbotest](turbotest/README.md) | the Scorpion Turbo+'s memory waits at 7 MHz, and which logic firmware (SC15.1 / SC15.3) it has | `turbotest/turbotest.trd`, `turbotest/turbotest.tap` |
| [fusetest](fusetest/README.md) (Philip Kendall, GPL) | contention, contended `IN`, high-port contention, the floating bus, reads of the paging and AY ports | `fusetest/fusetest.tap` (48K, 128K, +3; its Pentagon detection is broken) |

They load like any program (`LOAD ""`, or `RUN` in TR-DOS) and print their result on the screen; ctprobe and
snowtest run at 3.5 MHz, turbotest at 3.5 and 7 MHz. unreal-ng passes ctprobe on every machine it has, snowtest
on the 48K, 128K, +3 and Pentagon, and turbotest on the Scorpion with either logic firmware (its test suite
checks them); the emulators disagree on a few points that no emulator can settle. **Those points need a real machine.** This page lists them, with what to run and what to send back.

## What to send back

- A photo of the screen at the end (for snowtest: while it runs), sharp enough to read the text.
- The machine: model, board issue, ULA / gate array marking if you can see it, CPU make (Zilog, NEC, ...),
  for clones the board revision and any modifications (turbo, memory).
- How it was loaded (tape, TR-DOS disk, from which BASIC).

Open an issue in the project, or send them to the maintainer; the results go into the programs' READMEs.

## The open questions

### 1. The 128K / +2: does a port in an odd memory page wait? (ctprobe, P-05)

**Run** ctprobe from 128 BASIC (`LOAD ""`) on a 128K or a grey +2.

**Look at** the lines `P-05A` .. `P-05D`: `OK`, or `BAD` with the values.

**What it settles:** an `IN` from a port whose high byte points at #C000 while an odd page (1, 3, 5, 7) is there.
FUSE, SkoolKit and unreal-ng make it wait as if it were a slow memory address; xpeccy-plus does not. All four
`OK` means the first group is right; `BAD` on P-05 only means the second.

### 2. Which Scorpions have "Even M1"? (ctprobe, the machine line)

**Run** ctprobe on a Scorpion ZS-256 (yellow or green board, with or without Turbo+, with ProfROM or not), from
TR-DOS (`RUN`).

**Look at** the line `Machine:` near the top:

| It says | Meaning |
|:--|:--|
| `attr bus, Even M1` | opcode fetches from RAM wait for an even clock tick, as the Scorpion's logic chip equations (1996) say |
| `no contention, attr bus` | no Even M1: as the 2007 re-creation of that chip, and some emulators |
| anything else | send the whole screen |

**What it settles:** the circuit research found Even M1 in the turbo board's equations and no Even M1 in a later
re-creation; the earliest boards are unknown. Each photo tells us one board.

### 3. Where exactly does a Scorpion with Even M1 start the code? (ctprobe, P-02)

**Run** as in question 2. **Look at** `P-02`, and the summary at the end.

**What it settles:** the probe corrects its measuring engine for Even M1 (its README explains how). Three
emulators agree on every duration; they differ by 2 ticks on where the code starts, which only P-02 (the
floating bus: what an unused port reads while the picture is drawn) can see. `OK` on a real Scorpion confirms
the correction; `BAD` with a shift of 2 means the Scorpion adds its wait before it looks at the interrupt, as
xpeccy-plus does.

### 4. Does the 128K / +2 snow? (snowtest)

**Run** snowtest on a 128K or grey +2 (`LOAD ""` from 128 BASIC), and on a 48K if you have one.

**Look at** the LIVE band while it runs. Photograph it.

| You see | Meaning |
|:--|:--|
| LIVE exactly like EXPECTED | the snow model is right on this machine |
| LIVE plain | this machine does not snow (the MiSTer core's rule for the 128K) |
| snow in other columns or other characters | the model's tick or register value is off: the photo tells by how much |

**What it settles:** Weiv's tests and videos show snow on a real +2; the MiSTer core says the 128K ULA has none.
On the 48K the model was fixed on photos of another program (Snow Hold) from three machines; a snowtest photo
from a 48K confirms it with this program.

### 5. Does a +2A / +3 or a clone snow? (snowtest)

**Run** snowtest on a +2A / +3 (`LOAD "t:"`, `LOAD ""` from +3 BASIC) or a Pentagon / Scorpion / other clone
(TR-DOS `RUN`). **Expected:** LIVE stays plain. A photo of anything else is news.

### 6. How fast is a Scorpion's turbo, and which logic firmware does it have? (turbotest)

**Run** turbotest on a Scorpion ZS-256 Turbo+ (`RUN` in TR-DOS). In turbo the logic chip makes RAM accesses
wait for a memory slot; its two firmwares, SC15.1 and SC15.3, do it differently. The program counts how often
five small pieces of code run in one frame at 3.5 MHz and in turbo, and compares with both firmwares.

**Send** the photo of the final screen, the board revision, and the marking on the logic chip (DD30) if it is
readable. Expected: `Turbo+ logic: SC15.1` or `SC15.3`. `Matches neither firmware` is the most interesting
answer: the counts then show which accesses wait differently.

## What these programs cannot answer

- **The 128K's snow crash.** Some 128K machines hang or reset under snow; snowtest keeps `I` in slow memory only
  during its band, so it is not a crash test.
