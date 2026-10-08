# Research: sources and revisions

**Date:** 2026-10-07 · part of [README.md](README.md)

Local copies are in the owner's reference collection (not in the repository, and not copied into it). The links
are the upstream locations.

| Key | Upstream | Revision / date read | Read how | Good for |
|:--|:--|:--|:--|:--|
| NR-TXT, PORTS-TXT | https://github.com/MrKWatkins/ZXSpectrumNextTests (`nextreg.txt`, `ports.txt`; mirror of the FPGA repo's lists) | commit `98bb90c`, 2026-06-03 | both files read in full; `Tests/` folder only listed | register and port lists (core 3.1.5), test programs for timing, ULA, sprites, interrupts |
| MAME-NEXT | https://github.com/mamedev/mame/tree/master/src/mame/sinclair/next | commit `f43983b`, 2026-09-23 | register cases, machine config, ROM list, copper header, DMA head read; the rest by grep | register semantics, variants, device split |
| MAME-Z80N | https://github.com/mamedev/mame/tree/master/src/devices/cpu/z80 (`z80n.cpp`, `z80ndasm.cpp`) | same | disassembler table decoded; `z80n.cpp` head | opcode list, NMI stackless, nextreg callbacks |
| JNEXT | https://github.com/jorgegv/jnext | commit `810cfdff`, 2026-08-13, version 0.99.155 | FEATURES.md, `doc/analysis/*`, `doc/design/FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md`, `TASK-VIDEOTIMING-EXPANSION-PLAN.md`, `INTERNAL-Z80N-CORE-PLAN.md` (first 120 lines), `z80n_ext.cpp` comments | boot chain, timing constants, VHDL citations, feature scope |
| FPGA | https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA | commit `b047011`, 2025-11-06 (local clone in the emulator sources) | `zxnext.vhd` memory / port / NextREG / interrupt / copper blocks, `zxula_timing`, `zxula` contention, `tilemap`, `layer2`, `sprites` (structure), `dma` (state machine), `copper`, `t80n_mcode` / `t80n` (Z80N), `bootrom` | the authoritative hardware; findings in [research-fpga-vhdl.md](research-fpga-vhdl.md) |
| TBBLUE | https://gitlab.com/thesmog358/tbblue | commit `5cd10da`, 2026-09-28 (local clone) | `src/firmware`: `loader/src/main.c`, `app/src/boot.c`, `firmware/src/main.c`, `hardware.h`; tree listing of `machines/next` and `nextzxos` | boot chain and register init; the SD-card tree incl. `TBBLUE.FW` (the repository licence forbids selling and renaming, the ROMs keep their own licences) |
| WIKI-BOOT | https://wiki.specnext.dev/Boot_Sequence | fetched 2026-10-07 | summary of the page | boot stages and files |
| WIKI-Z80N | https://wiki.specnext.dev/Extended_Z80_instruction_set | fetched 2026-10-07 | the opcode table | opcodes, T-states |
| WIKI-NEX | https://wiki.specnext.dev/NEX_file_format | fetched 2026-10-07 | summary only | header fields, bank order |
| WIKI-MEM | https://wiki.specnext.dev/Memory_map | fetched 2026-10-07 | summary only | page numbers, 768K vs 2 MB |
| WIKI-OTHER | https://wiki.specnext.dev/FAQ, https://wiki.specnext.dev/Specifications | not fetched in this pass | cited from the storage-manager documents | to read in N0 |
| ZESARUX | https://github.com/chernandezba/zesarux | local commit `2d8dba1`, 2026-10-08 | `machines/tbblue.c` reset and fast-boot code (9005 lines, not read through) | second implementation to diff against; `tbblue_fast_boot_mode`; ships an older `tbblue_loader.rom` and a 64 MB `tbblue.mmc` |
| REPO-DOCS | the repository's own documents: [storage-manager integration-next](../2026-09-28-storage-manager/integration-next.md), [storage survey zx-next](../2026-09-28-storage-controllers-survey/zx-next.md), [snapshot pipeline](../2026-10-02-snapshot-pipeline/proposal.md), [Sprinter design](../2026-09-28-sprinter/README.md), [z84c15 CPU library](../2026-10-01-z84c15-cpu-library/design.md) | master `d59959065` | read | reuse and patterns |
| SEARCH | web search 2026-10-07 for variants | | result summaries only | variants (research-variants.md) |
| MANUAL | the SpecNext user manual ("ZX-Spectrum-Next-Manual", 7z) exists in the owner's collection, **not extracted or read** | | | user-level facts; N0 |

## What N0 reads next

1. The FPGA repository: `nextreg.txt` and `changelog.md` for the current core, `zxula_timing.vhd`, `t80n_mcode.vhd`,
   `zxnext.vhd` (port decode and memory decode), `dma.vhd`, `copper.vhd`, `sprites.vhd`.
2. The wiki pages: Sprites, Copper, DMA, Layer 2, Tilemap, Palettes, Interrupts, DivMMC, Multiface, UART, I2C, RTC.
3. The user manual from the owner's collection, for the keyboard, joysticks, power-on behavior and the F-key hotkeys.
4. `tbblue` firmware sources: `boot.c`, `config.c`, `menu.def`, `hardware.h`.
5. Public ZEsarUX `tbblue.c` for a third opinion on any register where MAME and jnext differ.
