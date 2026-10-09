# ZX Spectrum Next: phases

**Date:** 2026-10-07 · part of [README.md](README.md)

Each phase ends in a shippable milestone: the model stays creatable, nothing else regresses, every surface and a
verified recipe (`.recipe/machines/next.md`, extended per phase; commands run, outputs real) are updated, and a
TTD recording is started before the runs under test. "Parity" below means the rows of
[design-automation.md](design-automation.md) for the devices the phase adds. Sizes: S < 1 week, M 1-2, L 2-4, XL > 4.

| Phase | Scope | Milestone (shippable) | Size | Depends |
|:--|:--|:--|:--|:--|
| N0 | Research closure: (first and second pass done 2026-10-08, see [research-fpga-vhdl.md](research-fpga-vhdl.md), [esxdos-and-sd.md](esxdos-and-sd.md); open: TODO.md) read the FPGA repo (nextreg, timing, `t80n_mcode`, DMA, copper, sprites), the wiki pages, the manual, `tbblue` firmware; resolve Q1-Q8; table extractors in `tools/machines/next/`; provision the test card (not in repo) | the register and port tables as data files with a test that they equal the documents; open questions closed or re-filed | M | - |
| N1 | Z80N library (`core/src/3rdparty/unreal-next-z80`), [design-integration.md](design-integration.md), tests per opcode; no machine + disassembler and `unreal-asm` ZXN mode (Q10) | library tests green; base Z80 suites green on the fork; no change to any machine (benchmark A/B shows 0) | M | N0 |
| N2 | Skeleton machine: `NextBoard`, `PortDecoderNext` (ULA, paging ports, NextREG select/data, table-driven), `NextMemory` + slot table + MMU, engine installed, 3.5 MHz, 48K/128K/+3/Pentagon timing and ULA only (existing ScreenZX-class output), bare personality with user ROMs, keyboard, beeper, `list_models` creatable | `NEXT` creatable; 48K/128K/+3 BASIC runs; `next_regs`, `next_mmu` reports on all surfaces; recipe v0. **Core done 2026-10-08, uncommitted**: `Z80NEngine`, `NextMemory` (8x8K slot table, offsets, `Memory::ModelMemoryInterface` neutral hook, debug bookkeeping extracted into `Read/WriteDebugEffects`), `NextBoard` (NR #00-#04, #07, #50-#57), `PortDecoder_Next` (#7FFD/#1FFD/#DFFD rewrite the table, #243B/#253B), `data/configs/next` + `data/rom/next.rom` (128 + 48 + 48), 8 skeleton tests, gcc:16 -O3 clean, 8554 tests pass. Left: the two reports and the recipe, the A/B of non-Next machines (memory.cpp debug path was refactored), contention for 48K timing (N3) | L | N1 |
| N3 | Speeds 7/14/28, NR table complete for reads/writes, config mode, machine-type switch, 50/60 Hz, contention per machine type, floating bus, line/frame IRQ and line interrupt, `NextInterruptSource` pulse mode | NR `#00-#0A`, `#07`, `#22/#23` behave; timing tests of ZXSpectrumNextTests pass; A/B benchmark | L | N2 |
| N4 | Audio: 3 AY + selection, 4 DAC aliases, beeper options, mixer, mono/stereo flags | three AYs audible and in reports; DAC ports in tests | M | N2 |
| N5 | CTC + IM2 hardware mode + UART skeleton (no peers) + I2C + DS1307 | NR `#C0-#CE`; CTC test programs; RTC read by bit-bang test | M | N3 |
| N6 | Video I: ULA modes (Timex, ULA+, ULANext), LoRes/Radastan, Layer 2 (3 resolutions), palettes, clip/scroll, compositor orders, `ScreenNext` per-line, NR `#64` | screenshot compare with MAME/jnext on test programs (A3 part) | XL | N3 |
| N7 | Video II: tilemap, sprites (all attribute forms), copper, blend modes, stencil, line-split effects | A3 complete; copper/sprite reports; DeZog sprite commands | XL | N6 |
| N8 | zxnDMA + DMA bus agent + DMA interrupt, sample playback to DACs | DMA tests; A5 audio part | L | N5 |
| N9 | SPI + `sd.next0/1` + DivMMC + boot ROM + flash stub + media slots; boot the real chain ([esxdos-and-sd.md](esxdos-and-sd.md)) | A4: NextZXOS main menu from an unpacked folder; boot trace golden. **A4 reached 2026-10-09** with the ZX renderer (ULA screen): `NextFirmware_Test.NextZxosMainMenuIsDrawn`, see TODO.md; NextBASIC / Browser / dot commands left | XL | N3, N5, N8 |
| N10 | Input and buses: PS/2 keymap, mouse, joysticks (MD, I/O mode), Multiface, expansion enables, UART peers (ESP stub, joystick UART), +3 FDC traps NR `#D8-#DA` | joystick/mouse automation parity; NextBASIC `LOAD` of DSK works | L | N9 |
| N11 | Snapshots and TTD depth: NEX loader/saver, SNX/SNA/Z80/SZX into personalities, `MachineStateTransfer` rules, TTD blobs complete, checkpoint save, seek test over a NextZXOS boot | A5, A6 | L | N9 |
| N12 | Variants (issue2, ks3), magic breakpoints, assembler/DZRP/GDB Z80N, Qt panels (layers, sprites, copper, MMU), performance pass with measurements | all of Q-list closed or deferred explicitly; variants selectable | L | N11 |

Rules between phases: a phase may not change a register's behavior decided by an earlier one without a test update
naming the source; hot-path touching phases (N1, N2 registration) carry A/B numbers in their notes; each phase's
TODO rows move to `DONE` only with the recipe run output attached.

## Order rule (owner decision 2026-10-08)

**N1 (the Z80N library) is first and nothing else starts before it is green.** Reason found in N0: the boot ROM, the
DivMMC ROM and NextZXOS all execute Z80N instructions (`NEXTREG`, `PUSH nnnn`) in their first lines, so a machine without a
correct Z80N cannot read the card, boot or show a menu ([esxdos-and-sd.md](esxdos-and-sd.md) section 3.4). The
library is a self-contained step (no machine needed) with its own gate, so it also de-risks the largest unknown early.

## Proofs of concept (before the phase they de-risk; each in `tools/poc/`, thrown away or promoted)

| POC | What it proves | Result feeds | Size |
|:--|:--|:--|:--|
| P1 | The `unreal-next-z80` fork builds with the rename script, runs on the callback bus, and executes V1 `Z80N` / `Z80Nc2` programs on a bare 64K memory: counts T-states, flags and `NEXTREG` calls. **Done 2026-10-08** with N1: `tools/machines/next/forkz80n.py` makes the fork, `core/tests/3rdparty/unreal-next-z80/` runs both V1 programs (`!Z80N.snx` 23 instructions, `!Z80Nc2.snx` 6) on a bare 64K bus with the real-board result bytes, and checks T-states, flags and the NEXTREG callback | N1 scope; the common adapter template ([design-integration.md](design-integration.md) section 3) | S |
| P2 | Memory model: the 8-slot table of 8K pages (read / write pointers, rebuild on mapping change) against today's `Memory` in a micro-benchmark, plus the engine's per-step overhead A/B. **Done 2026-10-08** ([PoC 024](../../../tools/poc/024-next-memory-model/README.md)): D6 confirmed (8x8K = 4x16K, 45.3 us per frame), the Z80N library is as fast as its parents, a paged bus would save 26 % of the CPU part (0.5 % of a 28 MHz frame): not for N2; a mapping change costs 7 ns; the A/B against the native core waits for N2's adapter | decision D6, N2 | S |
| P3 | **Boot chain on a skeleton**: boot ROM extracted from the VHDL + SPI + `SdCardSpi` + a folder card + NextREG file + MMU, no video: the loader reaches `JP #6000`, the firmware module writes the expected NextREG sequence ([research-fpga-vhdl.md](research-fpga-vhdl.md) section 14) and soft-resets into the personality ROM | N9 risk early; the NextREG golden sequence; the card-side requirements. **Done 2026-10-08, uncommitted**: the real boot ROM (extracted from the VHDL, `data/rom/next/nextboot.rom`, MD5 `8c4f0c1b...`) runs on `Z80NEngine`, initializes an SD card over `#E7`/`#EB` (`SdCardSpi` unchanged, 16-clock rule), loads `TBBLUE.FW` from a `HostFolderFat` card, jumps to `#6000`; the boot module writes the register sequence of research section 14 (NR #07 = 3, NR #03 = 0, the ROMs through the NR #04 config mapping, NR #05-#0A, #82-#85 = `DA 3F FF 01` for +3, NR #03 = `B3`, NR #02 = 1) and the soft reset starts the personality ROM from the system area (`NextFirmware_Test` on `testdata/machines/zxnext/card`). Needed on the way: config mode and the boot ROM overlay in `NextMemory`, NR #01/#0E = core 3.02.03 (the firmware refuses older than 3.01.10), `config.ini` with `timing=0` on the card (without it the firmware loops in its video test). Not yet: SPI flash (the firmware's core-version read), the DivMMC automap, the keyboard-driven menu | M |
| P4 | A second guest for the DivMMC: **UnoDOS 3** (open source, GPLv3) boots on the existing classic machine plus a new `DivMmcPaging` device (also PLAN #63's first step) and lists a folder through the SPI + `SdCardSpi` path | DivMMC / automap model, `RST $08` flow, TTD blob for the device. **Done 2026-10-08 except the TTD blob and the slot card, uncommitted**: `DivMmcPaging` (`core/src/emulator/io/divmmc/`) is a bus overlay over `#0000-#3FFF` + an M1 observer (the automap: `#0000 #0008 #0038 #0066 #04C6 #0562` after the fetch, `#3Dxx` instantly, `#1FF8-#1FFF` out) + the ports `#E3 #E7 #EB` as a low-byte card + two `SdCardSpi`; no change to `Memory`. UnoDOS 3.141 (`testdata/storage/divmmc/unodos`, 8K ROM + `unodos.sys`) boots on the 48K machine in 270 frames, mounts a FAT16 `HostFolderFat` card, reaches BASIC; a guest program lists the card root through `RST 8` / `F_OPENDIR` / `F_READDIR` (`DivMmcUnoDos_Test`). The read of `#EB` starts an exchange (the Z-Controller rule) and UnoDOS works with it. Left: the TTD blob (`DivMmcPaging` state + SD protocol state), the `sd.divmmc` / `divmmc` slot card, the Next backend over the 8K slot table (N9) | M |
| P5 | Compositor from dumps: a standalone function takes a frame state (NextREGs, bank 5 / 7, Layer 2 RAM, palettes, sprite RAM) and produces the pixels by the VHDL rules; compared with V1 photos for the layer-mixing programs | the per-line renderer design, performance | M |
| P6 | Sprite line budget: a small cycle model of `sprites.vhd`'s state machine (S_START / S_QUALIFY / S_PROCESS) counting clocks per sprite and per pixel | the "max sprites per line" flag instead of a count of 100 | S |
| P7 | DMA transfer timing at 3.5 / 7 / 14 / 28 MHz from `dma.vhd` (cycles per byte, prescaler, burst release) | N8; the bus-agent granularity | S |

Order: P1, P2, P3, P4 first (they decide the architecture and de-risk the boot); P5-P7 as the video / DMA phases come near.

## Later (no phase number yet)

- **esxDOS handler mode** (host folder answers `RST $08`, no firmware): optional, after N9 ([esxdos-and-sd.md](esxdos-and-sd.md) E1).
- **Older cores** (before 3.02.02): kept in mind, not built ([requirements.md](requirements.md) Q2).
- **UNS** (the universal snapshot format for all machines): the Next's full state goes into the TTD checkpoint container until UNS lands ([requirements.md](requirements.md) Q9).
