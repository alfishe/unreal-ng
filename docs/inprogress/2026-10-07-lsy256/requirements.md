# LSY256: requirements

**Date:** 2026-10-07 · part of [README.md](README.md) · evidence in [research-reference-consensus.md](research-reference-consensus.md)

Confidence: **M** = one source (Unreal NedoPC) and no contrary source; **R** = read off the ROM bytes; **O** = open.
No requirement is **H** (independently confirmed): there is only one source.

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `LSY256` is creatable and boots `data/rom/lsy256.rom` to its SYS menu |
| G2 | The `#7B` latch and the window-0 modes behave as the ROM expects |
| G3 | Nothing changes for other models: no cost on shared hot paths, no new fingerprint for existing machines |
| G4 | Automation parity: CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt all know the model and its latch |
| G5 | TTD records and replays the machine bit-exactly |

Not goals: schematic-level timing (no source), any BK-08 expansion not named here.

## 2. Requirements

| ID | Requirement | Conf. |
|:--|:--|:--|
| R1 | Model id stays `MM_LSY256`, short name `LSY256`, 256K only, config folder `lsy256` (derived from the short name) | M |
| R2 | ROM: `[ROM] LSY=`, 64K, roles 128 / 48 / SYS / TR-DOS = pages 0 / 1 / 2 / 3 (already in `rom.cpp`) | M |
| R3 | Latch `#7B`: written when the low address byte is `#7B`; bit 0 DV0, bit 1 BLKROM, bit 3 EMUL, bit 4 PA3 | M |
| R4 | Window 0 by (EMUL, BLKROM): (0,0) SYS ROM; (1,0) the normal ROM choice (128 / 48 / TR-DOS by `#7FFD` bit 4 and the DOS state); (0,1) RAM page 12 or 13 (DV0) read/write; (1,1) RAM page 8..11 read-only: 8 = 128, 9 = 48, 10 = SYS, 11 = TR-DOS role by `#7FFD` bit 4 and the DOS state | M |
| R5 | Window 3 = RAM page `(#7FFD & 7) + 8 * PA3`, 16 pages of 16K | R (the ROM's RAM test); Unreal differs |
| R6 | Windows 1 and 2 are RAM 5 and 2; `#7FFD` bit 3 picks screen 5 or 7; bit 4 picks the ROM; bit 5 locks `#7FFD`; bits 6 and 7 do nothing | M |
| R7 | Reset: `#7B` = 0, `#7FFD` = 0, SYS ROM at `#0000` | M |
| R8 | The `#7B` decode ignores the high address byte | M |
| R9 | `#7B` is write-only; a read is not claimed | M / O |
| R10 | TR-DOS: the Beta 128 interface with the usual `#3Dxx` entry in the 48 ROM and the usual exit rules, as on the Pentagon | O (Q3) |
| R11 | Keyboard: the BK-08 matrix, 8 half-rows, bits D0-D5 for six keys and D7 for a seventh "layer" key per row (Unreal's table); the ROM's key table is the check | M |
| R12 | Sound, joystick: the Pentagon 128 set (AY at `#FFFD` / `#BFFD`, Kempston joystick at `#1F`) | O (Q6) |
| R13 | Frame: 71680 T (Pentagon class) until a source says otherwise | O (Q5) |
| R14 | Video: standard 256x192, screens 5 and 7 | M |
| R15 | RAM at `#0000` in modes (0,1) and (1,1): the debugger and the automation memory view show it as RAM; (1,1) is write-protected | M |

## 3. Integration requirements

| ID | Requirement |
|:--|:--|
| I1 | `PortDecoder::GetPortDecoderForModel` and `IsModelSupported` get the case together (the two must stay in sync); the old assertion in `emulatormanager_test.cpp` is turned around |
| I2 | State lives in `EmulatorState::lsy` (`platforms/lsy/lsystate.h`), as the model-state refactor does for the other families; no field in the flat struct |
| I3 | TTD: one new blob `LsyPaging` with the next free `PeripheralId` (62 on master today; re-check on the day), listed in `GetTTDModelStateIds` |
| I4 | Snapshots: capture view and `PagingFamily` (design 5); no other family's behavior changes |
| I5 | Slots: `slots::refdata::Machines()` row for `LSY256` (buses, built-ins) |
| I6 | Qt: `MM_LSY256` in the Machine menu's supported list; the keyboard handled by the host-key route |
| I7 | Docs: `.recipe/machines/lsy256.md`, `docs/emulator/environment-variables.md` only if a new variable appears (none planned), `AGENTS.md` model list if it names models |

## 4. Acceptance

1. `emulator_manage create LSY256` works on every surface; the SYS ROM reaches its menu or RAM test screen.
2. A test program writes each `#7B` value and the window map reports the modes of R4; the window-3 page follows R5.
3. NMI (`#0066`) puts RAM 13 at `#0000`.
4. TTD record, run and replay agree bit-exactly; the `#7B` latch is in the checkpoint.
5. A 128K snapshot loads on `LSY256` and an `LSY256` in a 128-compatible state saves; other states refuse with a reason.
6. All other models' tests and TTD fixtures are unchanged; zero warnings on gcc (Docker) and clang.
