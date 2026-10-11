# ZX Spectrum Next (`NEXT`, 2048K) as a creatable machine

**Created:** 2026-10-07 · **Status:** design only, nothing implemented · see [TODO.md](TODO.md)

## What this is

`MM_NEXT` has been in the `MEM_MODEL` enum and the model table (`NEXT`, 2048K, `RAM_2048`) for a long time, but
the model cannot be created: it has no port decoder, no memory map, no CPU, no video. Other code already
knows the name: the slot planner test calls it "not a creatable model", the snapshot capture lists it among the
models with no layout, and TTD refuses its port-read journal ("its DMA moves data into RAM without IN").

This folder designs the machine that makes `NEXT` creatable. It is the biggest model in the emulator: a
Z80N CPU at four speeds, a 2 MB RAM behind an 8K MMU, a 256-register control plane, five video layers
(ULA, LoRes, Layer 2, tilemap, sprites) with a copper, a DMA, three AY chips, four DACs, two SD sockets, a
DivMMC, a Multiface, two UARTs, an I2C clock, a PS/2 pair and a firmware chain (boot ROM, `TBBLUE.FW`,
NextZXOS). So the design is split into small documents, and the work into phases that each end in
something that can be shipped and checked from every automation surface.

## Approach in four sentences

1. **Emulate the FPGA core, run the real firmware.** The machine is the hardware (ports, NextREGs, memory,
   video, devices). The real boot ROM, `TBBLUE.FW` and NextZXOS run on it from an SD card, like on a board.
   A direct-boot shortcut exists for tests and for NEX programs, but it is a second path, not the first.
2. **The CPU is its own vendored library.** Z80N is a new `core/src/3rdparty/` library forked from
   unreal-z80 (the way `z84c15` was), plugged in through the existing `ICpuEngine` seam. The base Z80 is not
   patched and no other machine pays for it ([design-cpu.md](design-cpu.md)).
3. **Naive first, measure later.** Every device starts as the simplest code that is right for the
   documented behavior, driven by the register tables in the research documents. Per-pixel and
   per-scanline renderers come before any cache. Hot-path additions get A/B benchmarks.
4. **No new automation concepts.** Everything reaches CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt
   through the existing generic surfaces (state aspects, memory regions, media slots, TTD), plus a small
   Next-specific report set, each with a verified recipe ([design-automation.md](design-automation.md)).

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, non-goals, requirements with confidence and evidence, board variants, acceptance scenarios |
| [phases.md](phases.md) | **the phase plan N0-N12**: scope, shippable milestone, automation parity, recipe, TTD, tests, exit criteria per phase |
| [design.md](design.md) | architecture overview, model registration, class layout, decisions D1-D14 |
| [design-core.md](design-core.md) | memory and MMU, NextREG space, port decoder, layers, Layer 2, tilemap, sprites, copper, ULA modes, palettes, compositor |
| [design-cpu.md](design-cpu.md) | the Z80N library, opcodes, the engine seam, timing at four speeds, interrupts, NMI |
| [design-peripherals.md](design-peripherals.md) | zxnDMA, CTC and IM2, SD/SPI, I2C RTC, UART/ESP/Pi, DivMMC, 3 x AY + DACs + beeper, mouse, joysticks, keyboards, Multiface |
| [design-video-timing.md](design-video-timing.md) | frame and line timing per machine timing, 50/60 Hz, contention, floating bus, raster counters, copper and line interrupts |
| [design-boot-and-firmware.md](design-boot-and-firmware.md) | FPGA core vs bootrom vs `TBBLUE.FW` vs NextZXOS/esxDOS, flash, SD, config, the direct-boot paths |
| [design-media-and-snapshots.md](design-media-and-snapshots.md) | media manager slots, `HostFolderFat`, NEX / SNX / SNA / SZX, `MachineStateTransfer`, the snapshot pipeline |
| [design-ttd.md](design-ttd.md) | TTD recording before runs, blob ids, serializers, the DMA isolation rule, sealed replay |
| [design-automation.md](design-automation.md) | surface-by-surface parity table, report names, recipes, Qt |
| [tdd-plan.md](tdd-plan.md) | tests per phase, fixtures, the use of ZXSpectrumNextTests, performance gates |
| [roms.md](roms.md) | the firmware and ROM inventory, provenance, what may ship in `data/rom/` |
| [research-nextreg-and-ports.md](research-nextreg-and-ports.md) | the register and port tables as read from the sources, with the gaps |
| [research-z80n.md](research-z80n.md) | the Z80N opcode table with encodings and timings |
| [research-variants.md](research-variants.md) | Next boards (Issue 2, Issue 4 / KS2, KS3, "Next Mini") and what separates them |
| [../../disasm/rom/next/README.md](../../disasm/rom/next/README.md) | byte-complete listings of the boot ROM, the DivMMC ROM and the four NextZXOS ROMs (SD driver and API named) |
| [../2026-10-08-zx-next-hardware-poc/README.md](../2026-10-08-zx-next-hardware-poc/README.md) | autonomous POC for a real board: DMA / SRAM / contention timing, sprite line limit, `LDIRSCALE` (four ready `.nex` programs) |
| [verification-program.md](verification-program.md) | the evidence grades (real board, FPGA source, other emulators), the catalog of public Next test suites, how they become one program, order of use per phase |
| [esxdos-and-sd.md](esxdos-and-sd.md) | how esxDOS and NextZXOS read the card: DivMMC hardware and automap, the SPI / SD conversation, the API, the boot chain, the test layers |
| [design-integration.md](design-integration.md) | where the Next's code goes: the `unreal-next-z80` library, one interface for z80 / z84c15 / z80n, the engine adapter, what stays out of shared code |
| [research-fpga-vhdl.md](research-fpga-vhdl.md) | the FPGA VHDL and the tbblue firmware read in N0: timing, contention, memory map, ports, tilemap, Layer 2, sprites, copper, DMA, interrupts, boot chain, test corpus |
| [research-other-emulators.md](research-other-emulators.md) | MAME, jnext, ZEsarUX, CSpect: what each models and how it is built |
| [research-cosim-first-diff.md](research-cosim-first-diff.md) | the co-simulation tools (`tools/machines/next/cosim`) and the first diff of the real boot against jnext: NR #8C AltROM missing |
| [research-sources.md](research-sources.md) | every source with link, revision read and what it is good for |
| [TODO.md](TODO.md) | status marker, what is done and what is open |

## Key findings so far

- **A VHDL-faithful reference exists in C++.** MAME's `specnext.cpp` (Issue 5 based, with KS1 / KS2 / KS3
  variants) and jnext both follow the FPGA source and are cross-checked against it. They are the two
  implementations to diff our work against. The official register and port lists are in
  [ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) (`nextreg.txt`, `ports.txt`,
  core 3.1.5 of 2020: older than the cores MAME models, see [research-nextreg-and-ports.md](research-nextreg-and-ports.md)).
- **29 Z80N opcode entries** (all under the `ED` prefix) are in MAME's disassembler table
  ([research-z80n.md](research-z80n.md)). jnext adds `LDIRSCALE`, which neither the wiki table nor MAME's
  table has; the FPGA decodes it but does not implement the scaling (it acts like `LDIRX`).
- **The slot design for the SD cards is already done.** `sd.next0` / `sd.next1`, `required` on the first,
  `HostFolderFat` with the FAT32 cluster minimum, and a DivMMC framework plan
  ([storage-manager](../2026-09-28-storage-manager/integration-next.md),
  [storage survey](../2026-09-28-storage-controllers-survey/zx-next.md)). This design reuses it.
- **TTD cannot isolate the Next's port reads yet.** DMA writes RAM without an `IN`; the code says so at
  `PortJournalUnsupportedReason` in both TTD files. Recording works (state checkpoints); the sealed
  replay uses the live devices. The design keeps that and says what a later isolation needs
  ([design-ttd.md](design-ttd.md)).
- **SNX is a 128K `.sna` in disguise** (ZEsarUX `snap.c`, quoting the NextZXOS documentation): the `.sna`
  loader already covers it. NEX is a different, simple format with its own loader.
- **Unverified and left as open questions:** the existence and hardware of a "Next Mini", which Issue-2 /
  Issue-4 differences matter to software, Whether the Next Mini exists. The 128K line count (311) and the contention rules are **settled** by the FPGA VHDL
  read in N0 ([research-fpga-vhdl.md](research-fpga-vhdl.md)).

## Glossary

| Term | Meaning |
|:--|:--|
| **NextREG / NR** | the Next's control registers, 8-bit numbers, reached through ports `#243B` (select) and `#253B` (data) or the `NEXTREG` opcodes |
| **Core** | the FPGA bitstream (the hardware); versions like 3.02.04 |
| **Bootrom / IPL** | the 8K ROM inside the core that loads `TBBLUE.FW` from the SD card |
| **TBBLUE.FW** | the firmware program (boot module, menu, config reader, ROM loader) on the SD card |
| **NextZXOS** | the operating system on the SD card (+3DOS-compatible, NextBASIC) |
| **esxDOS** | the DivMMC firmware; on the Next it runs inside the NextZXOS stack |
| **Layer 2** | the 256x192 / 320x256 / 640x256 bitmap layer |
| **Copper** | the display-synchronised coprocessor that writes NextREGs at raster positions |
| **MMU slot** | one of eight 8K windows in the 64K address space (NR `#50`-`#57`) |
| **Machine timing** | NR `#03` bits 6:4: 48K, 128K, +3 or Pentagon video timing |
| **Personality** | a ROM set and machine type chosen by `TBBLUE.FW`'s menu (`menu.def`) |
| **NEX** | the Next's executable format: a header, optional loading screen and 16K banks |
| **SNX** | a 128K `.sna` that NextZXOS loads keeping a file handle open |
