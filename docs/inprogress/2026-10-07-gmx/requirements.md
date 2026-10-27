# GMX: requirements

**Date:** 2026-10-07 - part of [README.md](README.md) - evidence in [research-reference-consensus.md](research-reference-consensus.md)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `GMX` ("ZS Scorpion + GMX", 2048K) is creatable from every surface and boots the tracked `gmx.rom` to the GMX loader and the shadow monitor |
| G2 | The GMX register file (`#00 #78FD #7AFD #7CFD #7EFD #DFFD`) and its read-backs behave as the sources agree ([research-reference-consensus.md](research-reference-consensus.md)); a claim only one emulator makes is not built until a second source or a firmware trace confirms it |
| G3 | The 640x200 16-color extended mode renders |
| G4 | `SCORPION` and `PROFSCORP` do not change: configs, snapshots, TTD recordings and tests keep working; no cost on their hot paths |
| G5 | Automation parity: CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt know the model; each phase has a verified recipe in `.recipe/machines/gmx.md` and a TTD recording before the runs |

Not goals: the Pentagon and Composit schemes of the card (the Pentagon machine exists), the board test scheme as a
separate machine, flash programming (X7 / X8 jumpers), the never-built 320x200 one-color-per-pixel mode, the loader's FPGA
file format, the host-board modifications, RAM sizes below 2048 KB (Q10).

## 2. Requirements

Confidence: **H** two or more sources; **M** one source and no contrary one; **O** open (a question in [TODO.md](TODO.md)).

### 2.1 Model and firmware

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R1 | Model id `GMX`, enum `MM_GMX` (exists), RAM 2048K | H | config.h |
| R2 | ROM: one 512K image `rom/gmx.rom` (`[ROM] GMX=`), 8 planes of 64K; reset enters plane 0 (the loader) | H | MAME, PICO, page scan |
| R3 | `data/configs/gmx/` exists so `IsModelCreatable` is true; `PortDecoder::IsModelSupported(MM_GMX)` true | - | design 3 |
| R4 | Roles per plane for the ROM page names (`GetROMPageRole`) | O | Q1 |

### 2.2 Ports and memory

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R10 | Window 3 page `((DFFD & 7) << 4) \| ((1FFD & #10) >> 1) \| (7FFD & 7)`, masked to 128 pages | H | MAME, PICO, ART2 |
| R11 | Window 2 page `(#78FD & #7F) ^ 2` | H | MAME, PICO |
| R12 | `#7EFD` bits 6-4 = ROM plane unless `#00` bit 4; the legacy `#0100` plane strobe is off | H / M (Q2) | MAME, PICO, UNR |
| R13 | `#1FFD` bit 2 = plane page 3 at `#0000` with Beta on, overriding bit 0 | H | MAME, PICO |
| R14 | Read-backs of `#78FD`, `#7AFD`, `#7EFD` as in design 2.2 | H | UNR, MAME, PICO |
| R15 | Magic shift readout and CPU reset on `#00` bit 3 | H | UNR, MAME, PICO |
| R16 | `#00` bit 5 blocks the five GMX ports; `#00` stays reachable | M | MAME, PICO |
| R17 | `#7EFD` bit 2 | O | Q3 |
| R18 | Turbo from `#7EFD` bit 7, read back in bit 2; Scorpion Turbo+ waits | H | all |
| R19 | `IN` strobes from `#1FFD` / `#7FFD` families set/clear turbo | O | Q5 |
| R20 | Everything else (FDC, AY, `#FE`, Kempston, mouse, SMUC, covox) as `SCORPION` / `PROFSCORP` | H | ART1 |

### 2.3 Video

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R30 | Extended mode when `#7EFD` bit 3: 640x200, 80 bytes per line, pixel page `#39` / `#3B`, attribute page `+ #40`, Spectrum attribute bits | H | UNR, MAME, PICO |
| R31 | Scroller from `#7CFD:#7AFD` | O | Q4 |
| R32 | Flash inverts the pixels on the Spectrum flash phase | H | UNR, MAME |
| R33 | Border width, exact line timing in the extended mode | O | Q12 |
| R34 | Frame, line and INT as the Scorpion; INT select option | M / O | ART1, Q13 |

### 2.4 State, TTD and snapshots

| ID | Requirement | Conf. |
|:--|:--|:--|
| R40 | `GmxState` embedded in `EmulatorState`; trivially copyable; no pointers | - |
| R41 | TTD blob `GmxPaging` with the whole register file and the Magic lock; restore recomputes all windows | - |
| R42 | Snapshot capture gives the Scorpion-256 view only while no GMX extension is in use; otherwise `capture_unsupported` with a reason | - |
| R43 | `MachineStateTransfer`: Scorpion/GMX family rules (design 3) | - |

### 2.5 Automation and UI

| ID | Requirement |
|:--|:--|
| R50 | CLI `start GMX`, `list_models` shows it creatable; WebAPI model list, `GET /ports`, `GET /state/paging` (GMX fields) and the OpenAPI schemas; MCP `emulator_manage` description, `inspect_state` paging, `unreal://memory-map`; Lua and Python `get_paging` / port map; Qt Machine menu entry |
| R51 | `.recipe/machines/gmx.md` verified per phase on a real run, a TTD recording started before the run |
| R52 | `.recipe/_common/machines.md`, `.recipe/README.md`, `AGENTS.md` and `data/rom/README-ROMS.md` updated in phase 1 |

## 3. Acceptance

- `tools/build/build.sh` zero warnings, `tools/build/test.sh` green, `docker/linux/build.sh --test` for C++ changes.
- Every test in [tdd-plan.md](tdd-plan.md) of the phase passes in under 50 ms (boot tests carry a comment).
- `SCORPION` / `PROFSCORP` tests unchanged and green.
- The recipe section of each phase is run through the surface under test and ticked with date and build.
