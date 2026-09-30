# Machine waits not modeled yet: requirements

**Date:** 2026-09-29 · **Plan:** follow-up 4 of PLAN #61 ([m1-contention TODO](../2026-09-28-m1-contention/TODO.md)) ·
**Status:** [TODO.md](TODO.md)

## 1. What is missing

unreal-ng models a machine's turbo as a clock multiplier only (`EmulatorState::hw_turbo_ratio`): every
instruction runs at its nominal length at the higher clock. Real machines add wait states there, and one clone
emulates Sinclair contention in some modes. From [contention-by-machine.md](../2026-09-28-m1-contention/contention-by-machine.md):

| Machine | Missing effect | Source |
|:--|:--|:--|
| Scorpion Turbo+ (and green boards) at 7 MHz | every RAM access waits for a free memory slot, more often while the picture is drawn | the board's EPLD equations (SC15.1) |
| ZX-Evo BaseConf (`ATM3`) at 14 MHz | variable waits per memory access, external I/O at 7 MHz | the released FPGA RTL |
| ZX-Evo BaseConf in its 48K / 128K rasters at 3.5 MHz | emulated Sinclair contention | the released FPGA RTL |
| ZX-Evo TS-Conf at 14 MHz | waits on cache misses | TS-Conf RTL; **not in this work**: part of the TSConf machine (PLAN #41), which owns `MemoryWaitOverlay` |

## 2. Goals

| ID | Goal |
|:--|:--|
| G1 | The listed machines take the time the hardware takes in these modes |
| G2 | Every rule traced to its source (equations, RTL lines), with confidence |
| G3 | No cost for machines and modes without waits (the wait overlay is installed only while a rule applies) |
| G4 | Switchable with the `contention` feature, like the Sinclair contention |

## 3. Requirements

| ID | Requirement |
|:--|:--|
| R1 | Each rule is a `MemoryWaitOverlay` (docs: `core/src/emulator/memory/memorywaitoverlay.h`) installed while the mode is on and removed when it is off |
| R2 | The rule's inputs are what the hardware uses (clock phase, access kind, beam position), taken from unreal-ng's raster, not from a second timing model |
| R3 | The ZX-Evo's emulated contention reuses the Ferranti ULA rules where the RTL matches them, and differs only where it differs |
| R4 | Tests on the invariant per rule (a known instruction at a known phase takes the RTL's length), negatives (other modes, other machines, the feature off), fingerprints of the other machines unchanged |
| R5 | A/B measurement of the machines without waits (docs/guidelines/performance-guidelines.md) |
| R6 | Automation parity: the state reports that show contention say which rule is active |

## 4. Acceptance criteria

| ID | Criterion |
|:--|:--|
| AC1 | Worked examples from the research (per rule) reproduce in `core-tests` |
| AC2 | Pentagon, Scorpion at 3.5 MHz, ATM710 and the Sinclair machines: timing fingerprints unchanged |
| AC3 | The `contention` feature off removes the waits |
| AC4 | Full build with zero warnings, all of `core-tests` pass (with `-j` at half the cores) |
