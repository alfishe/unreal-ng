# SAA1099 co-simulation

Our SAA1099 model (`core/src/emulator/sound/chips/saa1099/`, design:
[tdd-saa1099.md](../../../docs/inprogress/2026-10-03-zx-multisound/tdd-saa1099.md)) is our own code. It was brought
to the consensus of the reference implementations by running them side by side on the same register streams
and comparing every generator, not by copying them (owner decision
[Q2](../../../docs/inprogress/2026-10-03-zx-multisound/open-questions.md)). This folder holds that harness and the
consensus table it produced. The agreed behavior is frozen in `core-tests` by golden digests over the same
corpus (`core/tests/emulator/sound/chips/saa1099/saa1099_golden_test.cpp`); the harness itself is not part of the
build.

## Layout

| Path | What |
|---|---|
| `fetch-refs.sh` | fetches the references at pinned revisions into `refs/` (git-ignored, never committed) |
| `build.sh` | builds the drivers into `bin/` (git-ignored): `drv-ours`, `drv-saasound`, `drv-mame`, `drv-mister` |
| `drivers/cosim.h` | stimulus format, the event dump every driver writes, the clock-by-clock loop |
| `drivers/drv-*.cpp` | one driver per model; `drivers/mame-shim/emu.h` is a stand-in for the few MAME device names |
| `gen-corpus.py` | writes `corpus/*.saa`: hand-written edge cases and seeded random streams (deterministic) |
| `corpus/` | the stimulus corpus (committed: the golden test reads it) |
| `compare.py` | compares two event dumps per generator, with the reference's time resolution |
| `run-cosim.py` | the whole matrix: every stream x every reference x every check |
| `expect.txt` | the differences that are expected, each with its row of the consensus table below |
| `out/` | event dumps of the last run (git-ignored) |

## Running

```sh
./fetch-refs.sh        # once; needs network
./build.sh             # needs a C++20 compiler and Verilator (5.x) for the RTL driver
./gen-corpus.py        # only after changing the corpus generator
./run-cosim.py         # whole matrix; -s <stream glob> -r <reference> -v for details
```

Exit status 0 means every check agreed or differed only as `expect.txt` says. Last run (2026-10-04):
**263 checks agree, 6 agree up to a jitter cut (below), 139 differences expected, 0 unexplained.**

## References

| Reference | Revision | Kind | How it runs |
|---|---|---|---|
| [SAASound](https://github.com/stripwax/SAASound) (Dave Hooper; the [SourceForge project](https://sourceforge.net/projects/saasound/) keeps only the old CVS tree and links here) | `3d92322` | software, primary | `CSAADevice` at a "sample rate" equal to its clock without oversampling: one tick per chip clock |
| [MAME `saa1099.cpp`](https://github.com/mamedev/mame/blob/f43983b62edf2b7d1dc8911ee4a2c08dba8a98de/src/devices/sound/saa1099.cpp) | `f43983b` (two files, SHA-256 pinned) | software | `saa1099_device` through a small device shim, one sample per 256 clocks (its fixed divider) |
| [MiSTer SAM Coupe `rtl/saa1099.sv`](https://github.com/MiSTer-devel/SAM-Coupe_MiSTer/blob/9888045b41b0a9a51cf6a1c56dc7fde6d3e9d264/rtl/saa1099.sv) | `9888045` | RTL (after Rodriguez Jodar's SAA1099.v and SAASound) | Verilator model, one `clk_sys` edge with `ce` per chip clock, internals via `--public-flat-rw` |
| [rejunity/tt06-psg-saa1099](https://github.com/rejunity/tt06-psg-saa1099) | `10c0983` | **no RTL** (the repository is the Tiny Tapeout template; the design was never written) | not run; its README is used for its **real-chip measurements**: the output-stage PDM patterns (Dave Hooper) and the noise polynomial (Jepael), plus the Philips datasheets and technical publication 231 in `docs/` |

The Philips documentation (SAA1099 data sheet 1984 / 1986, **technical publication 231**, all in the tt06
repository's `docs/`) is a reference of its own in the table: where it is explicit, it decides.

Instrumentation, not modification: SAASound and MAME are compiled with `-Dprivate=public -Dprotected=public` so
the drivers can read the generators; the MiSTer model exposes its signals through Verilator. No reference
source is changed and none is copied into the repository or into our code.

### Reference time resolution

- **MiSTer RTL** is exact to the chip clock. A tone edge clocks its noise generator and envelope one `clk_sys`
  edge later (pipeline), so those compare with 1 clock of tolerance.
- **SAASound** keeps tone periods as 12-bit fixed-point increments, so an edge lands 0 or 1 clock late. It
  never accumulates (the accumulator keeps its remainder): measured maximum 1 clock over 400 edges. When a
  write to a tone or octave register lands within that jitter of an edge (or between a tone number and the
  octave written after it, see row 3), the two models legitimately pick different periods from there;
  `compare.py --stimulus` detects this and compares up to that clock ("ok up to a jitter cut", 6 checks).
- **MAME** runs one sample per 256 clocks and starts every tone counter at 0, so only the intervals between
  its edges are compared, and its output is not (every output change sits next to an edge at that resolution).

## Stimulus corpus

Every write is a bus cycle at an absolute chip clock (8 MHz); consecutive writes are at least 8 clocks apart, as
on a real bus. `<clock> A|D <value>`, `<clock> END`, `;` comments.

| Stream | Covers (tdd §3.3) |
|---|---|
| `tone-periods` | every octave, tone numbers 0 / 255 / inner values (item 1) |
| `octave-latch` | tone then octave, octave then tone, tone alone, octave alone inside one half period (item 1) |
| `mixer` | tone only, noise only, tone + noise, none; fixed noise rates and a rate change (item 2) |
| `noise-from-tone` | noise clocked by tone generators 0 / 3, then back to a fixed rate (item 5) |
| `amplitude-zero` | amplitude 0 with tone on, sound enable off, no mixer (item 7) |
| `sync-reset` | RST held, numbers written during RST, release (item 6) |
| `env-shapes-{3,4}bit[-inverted]` | all 8 shapes, internal clock (item 3) |
| `env-buffered` | a write while a shape runs, after a single shape ended, switching off (item 4) |
| `env-external` | address-write clocking of both generators, the 5-bit address (#38 = #18) (item 4) |
| `env-resolution-switch` | 4-bit / 3-bit switches mid-envelope (item 3) |
| `all-voices` | six voices, both envelopes, noise from a tone generator |
| `random-1..6` | seeded random streams of *consensus* writes: no RST, noise sources chosen once (under RST), every tone number followed by its octave, repeating envelope shapes with one resolution per generator, envelope controls loaded while off |
| `random-quiet-9, 10` | the same without noise (so the RTL's output compares too, see row 6) |
| `random-raw-7, 8` | anything goes (golden digests and smoke runs; every reference legitimately runs apart after the first write of rows 3 / 7 / 9 / 11 / 13) |

## Consensus table

Columns: what each reference does. **Ours** is the choice and the reason. Rows are cited by `expect.txt`.

| # | Behavior | Philips docs / real chip | SAASound | MAME | MiSTer RTL | Ours and why |
|---|---|---|---|---|---|---|
| 1 | Tone half period | `f = 15625 * 2^oct / (511 - n)` | same | same | same | `(511 - n) << (8 - oct)` chip clocks: all agree (verified to the clock against the RTL) |
| 2 | Tone level after power-on and after RST | not stated | **high** (needed by the Fred59 SPACE DEMO, an observation of real software) | low, and the first edge comes at once | low | **high**: the only reference backed by an observation; documented as an expected difference for MAME / RTL |
| 3 | When new tone / octave numbers act | at the next transition ("up to half a period") | next transition, but a tone number written while no new octave is pending waits **one more** transition | next transition | next transition | **next transition for both**: Philips, MAME and the RTL; SAASound's extra transition contradicts Philips' "up to half a period" |
| 4 | Tone + noise on one voice | "the amplitude of the tone is increased relative to that of the noise" | tone high & noise low: full; both high: half; tone low: 0 | same | same | same (all agree). PDM: even periods carry the tone, odd periods tone AND NOT noise (stripwax's measurement) |
| 5 | Voice shaped by its envelope (2 / 5) | "the amplitude is 7/8ths that normally available" | sounds while the mixer output is **low**; amplitude LSB dropped | envelope multiplies a high output; applied to **all three** voices of the group; no LSB drop | sounds while low; LSB dropped | SAASound + RTL + Philips' 7/8 |
| 6 | Noise register | real chip (Jepael): 18-bit Galois LFSR, x^18 + x^11 + 1, period 2^18 - 1; seed unknown | that polynomial; seeded with 32 ones | same polynomial in Fibonacci form (its stream is a phase of ours: checked over the whole m-sequence) | **17-bit, other taps** (its stream is not in the m-sequence) | the measured polynomial; seed 18 ones (any non-zero seed is valid; MAME's "probably all 1s") |
| 7 | Fixed noise rate changed | not stated | the rate changes at once (phase accumulator) | from the next shift | from the next shift (reload) | **from the next shift**: MAME + RTL |
| 8 | Leaving noise source 3 (tone-clocked) | not stated | a full period of the new rate | divider kept running in source 3 | resumes its reload value (2048 clocks) | **a full period of the new rate** (SAASound): no two references agree; the primary software reference decides |
| 9 | RST (`#1C` bit 1) | tone generators reset and held; numbers written during RST act only after the first half period of the numbers held when RST was set | numbers written during RST act at once; output 0 while held | restarts the counters at the write only, nothing held | counters reload from the live registers while held; tone output low; envelopes reset too | **Philips** for the tone generators (restart with the numbers held at RST, hold, then the new numbers after the first half period); output 0 while held (SAASound; the RTL for unshaped voices); noise LFSR held and its divider starting a full period at release (SAASound + RTL); envelopes not reset (Philips names only the frequency generators; SAASound, MAME) |
| 10 | Envelope clock | internal: frequency generator 1 / 4 (977 Hz with 16 steps = both edges); external: "address 18 or 19 is written" | every edge; address #18 -> generator 0, #19 -> 1 | every edge; either address clocks **both** | every edge; #18 -> 0, #19 -> 1 | every edge; #18 -> 0, #19 -> 1 (5-bit address, so #38 is #18) |
| 11 | Envelope control writes | D7 (enable) and D4 (resolution) direct; shape, clock source and inversion buffered, acting at point 3 (end of a single shape) or point 4 (loop point); "after position (3) a new envelope will be implemented as soon as the new buffered controls are received" | as Philips: a write after point 3 acts at once | everything at once, and it restarts the shape | buffered, but shape / clock / inversion load only while the generator is off, and the enabling write stays pending, so a single shape plays **twice** | Philips + SAASound |
| 12 | Envelope steps and shapes | 16 levels up to 977 Hz, 8 levels above: 3-bit mode steps by 2; Fig.3: single shapes end at zero | steps by 2 in 3-bit mode; single shapes end at 0; right inverted = 15 - L (14 - L in 3-bit) | 3-bit mode steps by 1 (**half speed**), LSB masked; first step skipped | steps by 2; single attack holds 15 at the end (by its code; masked here by row 11) | Philips + SAASound |
| 13 | Resolution switched mid-envelope | not stated | 4 -> 3 bit drops the position LSB, 3 -> 4 bit **sets** it (measured by the author, test case `envext_34b`) | n/a (no buffering) | keeps the position as it is | SAASound (the only measured behavior) |
| 14 | Level of an envelope-shaped voice | real chip: amplitude and envelope PDM patterns are ANDed | its table = popcount(amplitude PDM AND envelope PDM): **verified equal to the real-chip patterns for all 128 entries** | amplitude x envelope / 16 | 4 a e (within 1 unit of the PDM AND) | the PDM AND, computed from the measured patterns |
| 15 | Silent voice, amplitude 0 with tone on | current sinks off | 0 | 0 | 0 | 0 (unipolar output; silence = 0) |
| 16 | Sound enable (`#1C` bit 0) clear | "sound enable for all channels" | output 0, generators run on | output 0 and **all generators stop** | output 0, generators run on | output 0, generators run on (SAASound + RTL) |
| 17 | Output stage | 6 equally weighted current sinks, PWM chopped at 62.5 kHz (8 MHz / 128), internal clock 4 MHz | mean levels (its PDM table) | mean levels | mean levels | HiFi: mean level of each voice's PDM pattern; Authentic: the measured 64-slot PDM bit stream, one slot per 2 chip clocks, even / odd periods as row 4 |
| 18 | Register address | "this block of 32 registers is repeated eight times" | 5 bits | 5 bits (#1D-#1F logged as unknown) | 5 bits | 5 bits; writes to unused addresses are stored for `Describe` and do nothing |

### Hardware evidence used

- **PDM patterns** (tt06 README, "PDM", published by Dave Hooper from a real SAA1099P): the 16 amplitude and 16
  envelope 64-slot patterns are in `saa1099.cpp`. ANDing them reproduces SAASound's effective-amplitude table
  exactly (all 8 x 16 entries), which is how row 14 was decided.
- **Noise** (tt06 README, "Noise", Jepael on [VOGONS](https://www.vogons.org/viewtopic.php?f=9&t=51695)): 18-bit
  Galois LFSR, x^18 + x^11 + x^1, "verified to match recorded noise from my SAA1099P".
- **Philips technical publication 231**: latch-at-transition timing (row 3), RST semantics (row 9), direct and
  buffered envelope controls and points 3 / 4 (row 11), 977 Hz / 16 levels (rows 10, 12), 7/8 amplitude with
  envelopes (row 5), 62.5 kHz chopping and 4 MHz internal clock (row 17).
- Not used yet: the real-chip FLAC recordings in the tt06 repository (`real_chip_recordings/`) - a future
  audio-level check (open issue in the TDD).

## Licenses

The references keep their licenses (SAASound BSD-style, MAME device BSD-3-Clause, MiSTer core GPL-2.0+,
tt06 Apache-2.0). They live only in the git-ignored `refs/`, fetched by `fetch-refs.sh`; the drivers compile
them from there. Nothing of them is in the repository.
