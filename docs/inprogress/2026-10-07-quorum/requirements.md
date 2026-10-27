# Quorum: requirements

**Date:** 2026-10-07 · part of [README.md](README.md) · evidence in [research-quorum-reference-consensus.md](research-quorum-reference-consensus.md)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `QUORUM` can be created with 128K and 1024K RAM, boots `qu7v42.rom` to its ROM-MENU, and starts 128 BASIC, 48 BASIC and TR-DOS from it |
| G2 | Every behavior rests on a source ([research-quorum-reference-consensus.md](research-quorum-reference-consensus.md)); a fact only one source states is built only when it cannot hurt the consensus behavior, and is marked |
| G3 | Nothing changes for any other model: no new work on a shared hot path; the Quorum differences live in its own decoder and devices |
| G4 | Automation parity: CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt all know the model and its state |
| G5 | Time-travel debugging, snapshots and the machine-state transfer know the model |

Not goals: the printer ports (`#1B`, `#7B`, `#FB`), ZXMAK2's 64K variant, the `#80FD` effect (unknown), a Quorum expansion
bus (no source), cycle-exact floppy timing beyond what the shared WD1793 gives.

## 2. Requirements

Confidence: **H** = U, Z (and BC or the ROM) agree; **M** = one source, no contradiction; **O** = open (listed in
[TODO.md](TODO.md)), built only after its question closes or as the recommended default.

### 2.1 Machine and firmware

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R1 | `QUORUM` (`MM_QUORUM`) is creatable (`IsModelSupported`, factory case) with RAM 128 and 1024 (the table row has `RAM_128 \| RAM_1024`, default 1024) | H | config.h; U `RAMSize` |
| R2 | System ROM `rom/qu7v42.rom`, 64K, pages SYS / DOS / 128 / 48 | H | U, Z, ROM |
| R3 | Reset: `#00` = 0, `#7FFD` = 0, TR-DOS trap off; the machine starts in the SYS page | H | U, Z |
| R4 | Config folder `data/configs/quorum/unreal.ini` with `[ROM] QUORUM=`, `RESET=` the SYS menu, single AY, Kempston joystick | M | design 3 |

### 2.2 Ports and memory

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R10 | `#00` write `0x99/0x00`: bit 0 RAM over ROM, bit 3 RAM page 8, bit 5 normal ROM (0 = SYS), bit 6 block 7FFD and `#0000` writes | H | P1, section 2 of the consensus |
| R11 | `#7FFD` write `0x801A/0x0018`: page `7FFD & 7` on 128K, bit 3 screen 5/7, bit 4 ROM 128/48; ignored while `#00` bit 6 is set | H (128K), O (lock, 1024K bits) | P2, M4, M7 |
| R12 | `#FE` read/write `0x99/0x98` (border, beeper, MIC, EAR, keys) | H | P4 |
| R13 | `#7E` read `0x99/0x18`: the extra matrix | H | P5, [research-quorum-keyboard.md](research-quorum-keyboard.md) |
| R14 | FDC at `#80..#83` (to the WD1793 registers), system port `#85` translated into the Beta128 `#FF` format; always on | H (addresses), O (aliases, gating) | P6-P9 |
| R15 | AY at `#FFFD / #BFFD`; Kempston joystick at `#1F` | H | P11, P12 |
| R16 | Memory map: `#0000` ROM page or RAM page 0 / 8 (`#00` bit 0, bit 3); `#4000` page 5; `#8000` page 2; `#C000` the 7FFD page | H | M1-M4 |
| R17 | TR-DOS trap: M1 fetch at `#3Dxx` with the 48 page mapped enters TR-DOS; leaves on an M1 fetch at `>= #4000` | H | M10 |
| R18 | What the trap shows at `#0000` | O | Q1 |
| R19 | NMI (key F11 or API) clears `#00` so the SYS handler runs | M | Z, ROM `#0066` |
| R20 | Writes under a mapped ROM: to the RAM page beneath, unless `#00` bit 6 | O | Q10 |

### 2.3 Video

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R30 | Standard 256x192 Spectrum screen from page 5 or 7 (7FFD bit 3), 15 colors with bright, flash | H | M9 |
| R31 | Frame: 224 T lines, 312 lines, 69888 T, INT 32 T long, first paper at line 80 + 65 T; no contention | M | V1, V3 (Z only) |
| R32 | `#00` bit 2 selects Pentagon timing (1024K only) | O | V2 (K only), Q3 |

### 2.4 Devices

| ID | Requirement | Conf. |
|:--|:--|:--|
| R40 | Built-in WD1793 with drives A and B (drive select values `01`, `10`); disk images mount through the normal media API | H |
| R41 | The extra keyboard matrix with all 39 known positions, host key mapping, F11 NMI, F12 reset | H |
| R42 | One AY-3-8912 (TurboSound=Single in the shipped config) and the beeper | H |

### 2.5 Integration

| ID | Requirement |
|:--|:--|
| R50 | TTD: the model's state (`#00`, `#80FD` latch, trap flag; 7FFD travels in the common state) is a registered peripheral blob, replay is bit-exact |
| R51 | Snapshots: loading SNA/Z80/SZX into a Quorum goes through `MachineStateTransfer` with a Quorum paging family; saving captures a 128K view only when `#00` is in the plain state, and refuses with a reason otherwise |
| R52 | Automation: model name `QUORUM` in the CLI, WebAPI + OpenAPI enum, MCP resource, Lua, Python; paging state reports `#00`, `#7FFD`, the ROM page name, the trap flag; `.recipe/machines/quorum.md` verified live |
| R53 | Qt: "Quorum" in the machine menu with its RAM sizes |
| R54 | Port map / port trace list the Quorum rows; `modelName` "Quorum" |

## 3. Acceptance

1. `emulator_manage create QUORUM` (and WebAPI, CLI, Lua/Python-host paths) succeeds for 128 and 1024; the ROM-MENU text is in VRAM.
2. TR-DOS boots a TRD image from the menu; a file loads; the disk write path works on drive B too.
3. Each decode row of the consensus table has a test; overlapping and open rows are tested as the recommended default and named after their question.
4. Every other model's fingerprints, TTD fixtures and tests unchanged.
5. Zero warnings (clang and gcc), `core-tests` green, a TTD recording of the verification run exists before the recipe is trusted.

## 4. Open questions

See [TODO.md](TODO.md) (Q1-Q12), each with a recommendation.
