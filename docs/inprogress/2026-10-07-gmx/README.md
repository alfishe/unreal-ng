# GMX (Scorpion ZS-256 + Graphic Memory eXpander, 2048K)

**Created:** 2026-10-07 - **Revised:** 2026-10-30 (original manual, real software, current firmware, the web Unreal
Speccy model) - **Status:** design only, not implemented - see [TODO.md](TODO.md)

## What this is

GMX is a plug-in card (Scorpion Ltd / MOA, 1998) for the Scorpion ZS-256 Turbo and Turbo+ boards. It sits in the Z80
socket and brings 2 MB of RAM, a 512 KB flash with the ROM planes and FPGA schemes, a second RAM window, a Profi-style
`#DFFD` page extension, a ROM plane register, a 7 MHz turbo bit and a 640x200 16-color text/graphics mode with a hardware
scroller. The enum `MM_GMX` and the model row ("ZS Scorpion + GMX", `RAM_2048`) exist; the machine cannot be created
(no port decoder factory case, no config folder, ROM loader asserts).

GMX is the Graphic Memory eXpander (Scorpion Ltd / MOA, 1998): a memory and video expansion with no MIDI part.

## What the 2026-10-30 revision found

- **The original manual** ("GMX (Graphic Memory eXpander). Руководство по подключению, программированию и работе",
  Scorpion, 22.11.1998, from the archived scorpion.ru) is now in [references/](references/), with the rev.2022 edition (same programming section). It settles most of the
  conflicts between emulators: `#7EFD` bit 2 blocks Magic **and** lifts the `#7FFD` lock (Q3); the scroller is a
  write-only byte start address with the low four bits zero (Q4); `#00` bits 0-3 pick and reload the FPGA scheme, which
  is what the magic readout reports to the loader (Q6); `#00` bit 7 selects the Scorpion or Pentagon INT and frame
  (Q13); RESET keeps `#00` (Q18). It says screen 1 is `#3A`, which the software contradicts (Q11, `#3B` kept).
- **Real software for the board** in `testdata/machines/gmx/`: Edge Grinder (izzx, 2021; double-buffers every frame,
  25 fps on the board) and the OSZ multitasking OS (izzx, 2024-2026; hardware scroll, read-backs every interrupt, boots
  from SD through the shadow monitor) ([research-2026-10-30-software.md](research-2026-10-30-software.md)).
- **Current firmware** `data/rom/gmx644.rom`: Loader V2.00 + Shadow Service Monitor 6.44su (LW/PLM, 21.08.2025) with
  FAT32 and Z-Controller SD, the image OSZ needs; it becomes the default (Q16).
- **The web Unreal Speccy** at <https://www.zxby.org/unreal/?model=osz> runs Scorpion GMX + OSZ in the browser. Its GMX
  layer (`web_gmx.cpp`, source not public) sits on Unreal's PROFSCORP model; it was read from the wasm
  ([research-2026-10-30-web-unreal.md](research-2026-10-30-web-unreal.md)). It agrees with the design on the
  ports, the RAM formula, the scroller and the `#3B` screen; it differs on the attribute byte (Q22) and keeps the
  ProfROM read strobe (Q2). No flash writes.
- ZEsarUX, Xpeccy, ZXMAK2, UnrealSpeccyP, Zero and the others still have no GMX. The zx-evo Unreal **does** have GMX
  memory paging (the 2026-10-07 survey missed it: the file is cp1251).

## What the 2026-10-07 research found

- Only **three** emulators implement it: Unreal Speccy (zx-evo fork, ports and video only: its memory mapping for GMX
  is missing), **MAME** (the most complete) and **pico-speccy** (MAME-derived, with firmware traces). ZXMAK2, Xpeccy,
  Xpeccy-plus, UnrealSpeccyP and ZX-M8XXX have none. No schematic or FPGA scheme was found.
- The three agree on: `#00` global port, `#78FD` second window (stored XOR 2), `#7AFD` / `#7CFD` scroller and paging
  read-backs, `#7EFD` mode port (turbo, plane, extended mode), `#DFFD` pages, the RAM formula, the ROM plane rule, the 640x200
  layout. They differ on `#7EFD` bit 2, the scroller semantics, the ProfROM strobe and the buffer screen pages; the design takes
  the majority or the traced one and keeps each as an open question with a recommendation.
- The tracked `data/rom/gmx.rom` (CRC32 `00DF8568`) is MAME's "GMX Boot Rom 1.2 V. 5.00": enough for phase 1.
- The model fits the existing Scorpion decoder as one more arm plus a small `GmxState`; no new library.

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, requirements with confidence, acceptance |
| [design.md](design.md) | hardware facts (ports, memory, ROM, video, timing, devices), integration points, decoder, TTD, snapshots, phases with automation parity, verification protocol |
| [tdd-plan.md](tdd-plan.md) | concrete tests by file and name |
| [roms.md](roms.md) | the firmware image, its page scan, other known firmware |
| [research-reference-consensus.md](research-reference-consensus.md) | the sources read, and the per-register comparison of Unreal / MAME / pico-speccy / articles (2026-10-07) |
| [research-2026-10-30-emulators-and-manual.md](research-2026-10-30-emulators-and-manual.md) | the manual's verdicts, every register with sources, the firmware page map, where GMX plugs into unreal-ng |
| [research-2026-10-30-software.md](research-2026-10-30-software.md) | the ports, video and paging Edge Grinder and OSZ use, their docs, the fixtures and what each tests |
| [research-2026-10-30-web-unreal.md](research-2026-10-30-web-unreal.md) | the web Unreal Speccy GMX model read from its wasm, the `gmx644.rom` flash blocks, its front end |
| [references/](references/) | the 1998 manual: the original Word file and its UTF-8 text |
| [TODO.md](TODO.md) | status marker, phase checklist, open questions with recommendations |

## Related

- Scorpion family: [2026-09-07-scorpion-zs256-clone](../2026-09-07-scorpion-zs256-clone/README.md), the SMUC:
  [2026-09-28-scorpion-smuc](../2026-09-28-scorpion-smuc/), turbo waits:
  [2026-09-29-machine-waits](../2026-09-29-machine-waits/research-scorpion-turbo.md)
- Model state structs: [2026-10-07-model-state](../2026-10-07-model-state/goals-and-requirements.md)
- Structural model: [2026-10-01-profi-v3-v5](../2026-10-01-profi-v3-v5/README.md); sibling creatable-model design:
  [2026-10-07-phoenix](../2026-10-07-phoenix/design.md)

## Upstream references

[Manual (archive.org)](https://web.archive.org/web/2006id_/http://www.scorpion.ru:80/spectrum/hard/gmx.zip) -
[OSZ](https://github.com/izzx-git/OSZ) - [EdgeGrinder](https://github.com/izzx-git/EdgeGrinder) -
[web Unreal Speccy](https://www.zxby.org/unreal/) -

[Apollonov, City #18](https://zxpress.ru/article.php?id=10414&lng=eng) -
[ZX-Pilot #31](https://zxpress.ru/article.php?id=9138) -
[Trident](https://zxpress.ru/article.php?id=10696&lng=eng) -
[MAME `scorpion.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp) -
[pico-speccy](https://github.com/DnCraptor/pico-speccy) -
[Unreal Speccy (zx-evo)](https://github.com/tslabs/zx-evo/tree/master/pentevo/unreal/Unreal) -
[Black_Cat port table](https://wiki.speccy.org/_media/cursos/ensamblador/zx-ports-full-table.pdf)
