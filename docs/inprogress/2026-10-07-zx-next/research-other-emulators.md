# Research: how other emulators model the Next

**Date:** 2026-10-07 · part of [README.md](README.md)

Only what was read is stated. Revisions are in [research-sources.md](research-sources.md).

## 1. MAME (`mamedev/mame`, `src/mame/sinclair/next/`)

| Aspect | What it does |
|:--|:--|
| Size | `specnext.cpp` 4358 lines plus 25 small device files (copper, CTC, DivMMC, DMA, IM2, layer 2, lores, multiface, sprites, tiles, UART, vtest, NEX loader), 7381 lines in all |
| CPU | `Z80N` device derived from `z80_device`, clock `28 MHz / 8` scaled by `1 << NR07 speed`; DMA and IM2 devices scaled the same way; `in_nextreg` / `out_nextreg` callbacks; "stackless NMI" |
| Video | one screen device (`28 MHz / 2` pixel clock, width 456 x 2, height 312), one palette of 512 x 5 + 1 entries (ULA, tilemap, Layer 2, sprites twice, fallback), separate devices per layer (`SCREEN_ULA_NEXT`, `SPECNEXT_LORES`, `SPECNEXT_TILES`, `SPECNEXT_LAYER2`, `SPECNEXT_SPRITES`), a `SPECNEXT_VTEST` device |
| Copper | a device with a timer that fires at the WAIT position; listing in its header: WAIT bit 15 = 1, bits 14:9 horizontal column in units of 8, bits 8:0 vertical line; MOVE bit 15 = 0, bits 14:8 register, bits 7:0 value; NOOP = MOVE 0,0; HALT = WAIT `#FFFF` |
| Audio | 3 x `YM2149` at `14 MHz / 8`, four 8-bit R2R DACs, a speaker |
| Peripherals | `I2C_DS1307`, two `SPI_SDCARD` (prefer SDHC), two RS232 ports (ESP and Pi) and MIDI ports, a ZX-bus slot, Multiface, DivMMC |
| Memory | `m_ram` default 2M, options 1M and 4M; the first 16K of the address space is the boot ROM |
| Status | the file's own TODO: contention, `internal_port_enable()` support, tile / sprite cache invalidation |
| Software list | `specnext_sd` (SD card images) |
| Snapshots | `nex` is registered with `ach,frz,plusd,prg,sem,sit,sna,snp,snx,sp,spg,z80,zx`; the NEX loader is a separate file |
| Use for us | the best register-by-register reference; all state in members named `m_nr_XX_*` that map one-to-one to the NextREG list; BSD-3-Clause |

## 2. jnext (`jorgegv/jnext`)

| Aspect | What it does |
|:--|:--|
| Scope | boots NextZXOS from an SD image through the real chain (bootrom, `TBBLUE.FW`), Z80N, all layers, copper, DMA, CTC, UARTs with an ESP-01 emulator, DivMMC with automap, RTC, Multiface, NEX saving and loading, SZX / SNA / Z80 / TAP / TZX / RZX, a Qt debugger |
| CPU | a FUSE-derived Z80 core with the Z80N added by opcode interception in `z80n_ext.cpp`; a plan to translate the T80N VHDL to C++ (4 to 6 weeks, `INTERNAL-Z80N-CORE-PLAN.md`) |
| Timing | a `VideoTiming` class from the VHDL constants: 48K 448 x 312 pixel ticks (224 T per line), 128K 456 x 311, Pentagon 448 x 320, 60 Hz 264 lines (59136 T), with interrupt positions per machine type |
| Docs | an extensive `doc/` tree: FPGA repo analysis, SD image booting, bypass of `TBBLUE.FW`, test-plan designs per subsystem (copper, DMA, layer 2, lores, sprites, compositor, contention, floating bus, NextREG, port dispatch), and a traceability matrix. These are the most useful reading for per-subsystem test ideas |
| Extras | rewind, a scriptable debugger, a magic breakpoint (`ED FF` / `DD 01`), host USB gamepads, video / audio recording |
| Use for us | a readable second opinion with VHDL line citations in comments; its bypass study gives the register writes of the firmware boot ([design-boot-and-firmware.md](design-boot-and-firmware.md)) |

## 3. ZEsarUX (`chernandezba/zesarux`)

The local checkout is an old one (commit `66d7b42`, 2023-05-05) with `tbblue.c`, `tbblue.h`, a bundled
`tbblue_loader.rom`, a `tbblue.mmc` test card and a `tests/tbblue_mmu.sh`. **`tbblue.c` was not read** for this
design; only the snapshot code was grepped (`snap.c`: `.snx` is loaded as `.sna`; `.nex` is supported; an
"esxdos handler" mode opens the NEX through the host folder). Its older tbblue personality is a software-level
emulation (it does not run the FPGA firmware chain; the loader ROM is its own). Treat it as a source of
behavior reports for programs, not for hardware.

## 4. CSpect

Closed-source Windows emulator by Mike Dailly. The local collection has five releases (2.12.9 to 2.13.00) as
binaries; **nothing was read**. Public facts used elsewhere in this folder come from the SpecNext tutorial URL
already cited in the storage-manager documents: CSpect mounts `-mmc=<file.img>` and has a folder mode that only
emulates the esxDOS file API. It is the de-facto reference for NEX programs (including `ED FF` / `DD 01`
magic breakpoints), so its behavior matters for acceptance A5.

## 5. Others in the collection

No Xpeccy Next implementation was found in the xpeccy reports read (a grep of the xpeccy-plus report for "Next"
found nothing). ZXMAK2 and unreal-speccy variants in the local collection were not searched for a Next model.
The DeZog protocol has Next commands (banks, sprites, sprite patterns, palettes, clip window; `ZXNEXT = 4`
machine type) in our own `core/automation/dezog/include/dzrptypes.h`.

## 6. What we take from each

| Question | Reference to diff against |
|:--|:--|
| Register read/write semantics | MAME first, then jnext, then the VHDL |
| Video layers and compositor | MAME devices for the shape; jnext's compositor plans for blend modes |
| Copper and DMA timing | MAME and the VHDL; jnext test-plan designs list the cases |
| Boot chain | jnext's bypass study, the wiki Boot Sequence page, `tbblue` firmware sources |
| Program compatibility | CSpect and ZEsarUX on the same NEX demos |
