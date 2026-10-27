# ZXM-Phoenix: requirements

**Date:** 2026-10-07 · part of [README.md](README.md) · evidence in [research-reference-consensus.md](research-reference-consensus.md)

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `PHOENIX` is creatable with 1024K (default) and 2048K RAM, boots its factory image to the 128K editor, 48 BASIC and TR-DOS |
| G2 | The memory map, ports and ROM selection match the reference consensus; anything only one source states is not built until a second source confirms it |
| G3 | Nothing changes for any other model: configs, snapshots, TTD recordings and tests stay as they are |
| G4 | No cost for other machines: Phoenix differences live in its own decoder, never on a shared hot path |
| G5 | Automation parity: CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt all know the model, with a verified `.recipe/machines/phoenix.md` |
| G6 | TTD records and replays a Phoenix session exactly (paging included) |

Not goals: a NemoBus slot card population (see design section 8), the 7 MHz mode until a port is known (Q8), a Phoenix BIOS
beyond the image in `data/rom/`, any other Nemo-family machine (Kay, Quorum), speeding up anything.

## 2. Requirements

Confidence: **H** = two or more independent sources agree; **M** = one source and no contrary one; **O** = open (listed in
[TODO.md](TODO.md)).

### 2.1 Machine and firmware

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R1 | Model `PHOENIX` (`MM_PHOENIX`, name "ZXM-Phoenix v1.0"), RAM 1024 (default) or 2048 KB; both creatable from CLI, WebAPI, MCP, Lua, Python, Qt | H | `config.h` row exists; Unreal `vars.cpp` and Xpeccy+ `phoenix.conf` agree on the sizes |
| R2 | Default ROM `rom/ZXM-Phoenix_bios.bin`, 64K: SYS, TR-DOS, 128K, 48K pages in that order | H | U, X, P, `rom.cpp` |
| R3 | The SYS page of the shipped image is erased and reads `#FF` | H | [roms.md](roms.md) (read from the file) |
| R4 | Reset: `#7FFD`, `#1FFD`, `#EFF7` = 0; 128K editor ROM at `#0000` | H | X, P (`phxReset`) |

### 2.2 Memory

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R10 | `#4000` = RAM page 5, `#8000` = page 2, `#C000` = the paged page | H | U, X, P |
| R11 | Page number (7 bits) = `#7FFD` bits 2-0, `#1FFD` bit 4 -> bit 3, `#7FFD` bit 7 -> bit 4, `#1FFD` bit 7 -> bit 5, `#1FFD` bit 6 -> bit 6; then masked by the RAM size (1024K drops bit 6) | M | P, X and the "real" comment; U swaps bits 3 and 4 (Q2) |
| R12 | `#7FFD` bit 3 selects the screen: page 7, else 5. The high page bits do not move it | H | U, X, P |
| R13 | `#7FFD` bit 5 locks `#7FFD` (the whole write); `#1FFD` is never locked | H | U, X, P |
| R14 | `#0000` priority: `#1FFD` bit 0 RAM page 0 (writable) > bit 1 ROM page 0 > bit 3 alternate pair > normal | H | U, X, P agree on all but the TR-DOS case of bit 3 |
| R15 | Normal ROM: TR-DOS active -> page 1; else `#7FFD` bit 4 ? page 3 (48 BASIC) : page 2 (128K) | M | X, P; U (and the shared unreal-ng code) pick page 1 only with `#7FFD` bit 4 = 1 and page 0 otherwise while TR-DOS is active (Q9) |
| R16 | `#1FFD` bit 3 ROM: TR-DOS active -> page 3; else `#7FFD` bit 4 ? page 1 : page 0 | M | X, P (U differs when TR-DOS is active) |

### 2.3 Ports and devices

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R20 | `#7FFD`: A15 = 0, A14 = 1, A1 = 0, A0 = 1 (A2 not decoded). `#1FFD`: A15 = 0, A14 = 0, A1 = 0, A0 = 1 | M | B, U (`#1FFD`); X, P add A2 (Q3) |
| R21 | `#EFF7`: full-decode write, stored, readable back; bit 7 forces the Beta Disk ports on whatever the ROM state; no other bit has an effect | M | X, P, U (bit 7 only in the sense of "DOS ports"); other bits Q4 |
| R22 | Beta Disk (WD1793, TR-DOS) as on the Pentagon | H | X, P (`difOut`), `config.h` |
| R23 | AY / YM with TurboSound option, Covox, General Sound as the Pentagon config | H | P `phoenix.conf` |
| R24 | Kempston joystick `#1F`, Kempston mouse `#FADF` / `#FBDF` / `#FFDF` (also with TR-DOS paged) | H | P, X |
| R25 | Unclaimed `IN` reads the floating bus like the Pentagon decoder of unreal-ng | M | P (`floatbus = attr`), X (`#FF`) |
| R26 | `IN #00F7` reads 0 | O | X, P only (Q6) |
| R27 | Gluk RTC on `#DFF7` / `#BFF7` only when `CMOS=` asks for it | O | U only (Q5) |

### 2.4 Video and timing

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R30 | 3.5 MHz, 224 T/line, 320 lines = 71680 T/frame, INT and its length as unreal-ng's Pentagon (Xpeccy+ lists 36 T for the length, the Pentagon row of the same table; unreal-ng keeps 32 T for the Pentagon); no contention | M | P table; the board is Pentagon-like in all three emulators (Q7) |
| R31 | Standard Spectrum screen (page 5 / 7, 256x192 plus border); no Pentagon `#EFF7` video modes (512 px, multicolor, GigaScreen) | M | none of X, P implements any; U runs the Pentagon `init_raster` for `#EFF7` writes (Q4) |
| R32 | The 7 MHz switch is not built (no port is known) | O | P only offers it by an emulator key (Q8) |

### 2.5 State, snapshots, TTD

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R40 | TTD: `#1FFD` is captured in its own blob `PhoenixPaging` (`#EFF7` is in the chipset state already); a Phoenix recording replays bit-exact; a checkpoint loads only into `PHOENIX` | - | design 6 |
| R41 | State transfer: a Phoenix whose latches are plain 128K moves to any 128K-class machine; its extended state (`#1FFD` != 0 or pages >= 8) only to a Phoenix of at least the same RAM | - | design 7 |
| R42 | Snapshot capture: `WindowMapCapture` as Profi and the ATMs use: a view exists while the live window map is a Spectrum 128K (RAM page n = bank n); else `capture_unsupported` with the reason | - | design 7 (Q11) |
| R43 | `.sna` / `.z80` / `.szx` have no Phoenix machine id: loading one keeps today's rules (it runs as a 128K) | - | formats |

### 2.6 Automation and UI

| ID | Requirement |
|:--|:--|
| R50 | `IsModelSupported(MM_PHOENIX)` true; `list_models` shows `creatable: true`; OpenAPI model enums and MCP descriptions say so |
| R51 | `state/paging` (WebAPI, CLI, Lua, Python) reports `#7FFD`, `#1FFD`, `#EFF7`, the 7-bit RAM page, the ROM page and its role |
| R52 | Port map and port trace list `#7FFD`, `#1FFD`, `#EFF7` with model name "Phoenix" |
| R53 | Qt Machine menu offers "ZXM-Phoenix (1024K / 2048K)" |
| R54 | `.recipe/machines/phoenix.md` verified live, and listed in `.recipe/README.md` and `_common/machines.md` |

## 3. Acceptance

| Scenario | Pass when |
|:--|:--|
| A1 | `start {model: PHOENIX}` -> 200; `state/paging` shows 1024K, ROM page 2 (128K editor); the 128K menu appears in the screenshot |
| A2 | `OUT #7FFD,#10` -> ROM page 3 at `#0000` (48 BASIC); `#1FFD` = 1 -> RAM page 0 at `#0000`, writable |
| A3 | All 128 combinations of the five paging bits give the page of R11, and `#C000` holds the byte written through that page |
| A4 | Lock: after `#7FFD` bit 5, a second `#7FFD` write changes nothing; `#1FFD` still pages |
| A5 | TR-DOS: `RANDOMIZE USR 15616` from 48 BASIC enters TR-DOS (ROM page 1); `CAT` reads a mounted TRD |
| A6 | TTD: record 3 s of boot plus paging writes, seek back, step forward: state hash equals the recorded one |
| A7 | 2048K: bit 6 pages beyond 1 MB, and a byte written there reads back through the same latches; on 1024K the same write aliases to page `n & 63` |
