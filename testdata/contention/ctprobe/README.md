# ctprobe

The emulated-side contention probe (design: `docs/inprogress/2026-09-28-m1-contention/test-programs.md`, section 3).

| File | Content |
|:--|:--|
| `ctprobe.asm` | Driver and case table: places each code fragment, maps a page if the case needs one, times it at consecutive frame T-states and stores the durations |
| `engine.asm` | The measuring engine of Patrik Rak's Timing Test v0.3 (after Jan Bobrowski's zxtests, GPL), ported to the in-tree assembler; assembled byte for byte identical to the original |

Assemble `ctprobe.asm` followed by `engine.asm` at 40000 with `Z80TextAssembler` (no external assembler). The host
suite is `core/tests/emulator/video/ctprobe_test.cpp`.

Running it elsewhere (real hardware, other emulators) needs a 3.5 MHz CPU (a Scorpion's ROM leaves 7 MHz on:
`IN #1FFD` selects 3.5 MHz) and, for the paging cases, unlocked `#7FFD` paging (run from 128 BASIC, not 48 BASIC).
A `.tap` / `.trd` export with on-screen results is future work.
