# ctprobe

The emulated-side contention probe (design: `docs/inprogress/2026-09-28-m1-contention/test-programs.md`, section 3).
It times code fragments at exact frame T-states, compares every value with the expected table of the machine's
contention class and prints a report.

| File | Content |
|:--|:--|
| `ctprobe.tap` | Reference build for tape: a BASIC loader and the code at 40000 |
| `ctprobe.trd` | Reference build for TR-DOS: `boot` loads and runs the same code |
| `ctprobe.asm` | Driver, detection, report and case table |
| `engine.asm` | The measuring engine of Patrik Rak's Timing Test v0.3 (after Jan Bobrowski's zxtests, GPL), ported to the in-tree assembler; byte for byte identical to the original |

## Running it

| Machine | How | What it can check |
|:--|:--|:--|
| 48K | `LOAD ""` | everything but paging |
| 128K / +2 | `LOAD ""` from **128 BASIC** (48 BASIC locks the paging) | + the pages at `#C000` |
| +2A / +3 | `LOAD "t:"`, then `LOAD ""` from **+3 BASIC** | + the pages and the all-RAM layouts |
| Pentagon, Scorpion | the `.trd`: `RUN` in TR-DOS (or the tape) | + the pages at `#C000` |

The report lists every case as `OK`, `BAD` or `--` (not run on this machine). It then shows the first wrong
value with its T-state. The border ends **green** when every value is as expected, **red** otherwise.
`PRINT USR 40000` returns the number of wrong values.

The probe detects the machine class itself from the frame length and a contended `NOP`:

| Class | How it is recognized |
|:--|:--|
| Ferranti ULA 48K | 69888 T frame, the `NOP` waits |
| Ferranti ULA 128K | 70908 T frame, the `NOP` waits |
| Gate array | 70908 T frame, the `NOP` waits 1 T |
| No contention | any other frame, the `NOP` does not wait |
| No contention with the attribute bus | 69888 T frame, the `NOP` does not wait (Scorpion: its unused ports read the fetched attribute) |

It also detects whether paging is open. It selects 3.5 MHz with `IN #1FFD`, which a Scorpion's ROM leaves at
7 MHz; the engine needs the INT pulse over before its handler returns.

The expected values come from an oracle that owes nothing to the emulator: the pattern tables, the raster, and
each instruction's bus cycles from FUSE's tables (`core/tests/emulator/video/ctprobe_test.cpp`). The Scorpion's
attribute timing (one cell per 4 T from 4 T before the paper) is this project's model; the programmer's manual
documents what the port returns, not when.

## Building

`ctprobe_test.cpp` assembles `ctprobe.asm` followed by `engine.asm` at 40000 with `Z80TextAssembler` and fills
the expected tables. `UNREAL_CTPROBE_EXPORT=1 ./core-tests --gtest_filter=CtProbeFiles_Test.Export` rewrites
the `.tap` and the `.trd`. `CtProbeFiles_Test.CommittedFilesMatchTheSource` fails when they drift from the
source.
