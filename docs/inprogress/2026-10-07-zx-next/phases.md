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
| N2 | Skeleton machine: `NextBoard`, `PortDecoderNext` (ULA, paging ports, NextREG select/data, table-driven), `NextMemory` + slot table + MMU, engine installed, 3.5 MHz, 48K/128K/+3/Pentagon timing and ULA only (existing ScreenZX-class output), bare personality with user ROMs, keyboard, beeper, `list_models` creatable | `NEXT` creatable; 48K/128K/+3 BASIC runs; `next_regs`, `next_mmu` reports on all surfaces; recipe v0 | L | N1 |
| N3 | Speeds 7/14/28, NR table complete for reads/writes, config mode, machine-type switch, 50/60 Hz, contention per machine type, floating bus, line/frame IRQ and line interrupt, `NextInterruptSource` pulse mode | NR `#00-#0A`, `#07`, `#22/#23` behave; timing tests of ZXSpectrumNextTests pass; A/B benchmark | L | N2 |
| N4 | Audio: 3 AY + selection, 4 DAC aliases, beeper options, mixer, mono/stereo flags | three AYs audible and in reports; DAC ports in tests | M | N2 |
| N5 | CTC + IM2 hardware mode + UART skeleton (no peers) + I2C + DS1307 | NR `#C0-#CE`; CTC test programs; RTC read by bit-bang test | M | N3 |
| N6 | Video I: ULA modes (Timex, ULA+, ULANext), LoRes/Radastan, Layer 2 (3 resolutions), palettes, clip/scroll, compositor orders, `ScreenNext` per-line, NR `#64` | screenshot compare with MAME/jnext on test programs (A3 part) | XL | N3 |
| N7 | Video II: tilemap, sprites (all attribute forms), copper, blend modes, stencil, line-split effects | A3 complete; copper/sprite reports; DeZog sprite commands | XL | N6 |
| N8 | zxnDMA + DMA bus agent + DMA interrupt, sample playback to DACs | DMA tests; A5 audio part | L | N5 |
| N9 | SPI + `sd.next0/1` + DivMMC + boot ROM + flash stub + media slots; boot the real chain ([esxdos-and-sd.md](esxdos-and-sd.md)) | A4: NextZXOS main menu from an unpacked folder; boot trace golden | XL | N3, N5, N8 |
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
| P1 | The `unreal-next-z80` fork builds with the rename script, runs on the callback bus, and executes V1 `Z80N` / `Z80Nc2` programs on a bare 64K memory: counts T-states, flags and `NEXTREG` calls | N1 scope; the common adapter template ([design-integration.md](design-integration.md) section 3) | S |
| P2 | Memory model: the 8-slot table of 8K pages (read / write pointers, rebuild on mapping change) against today's `Memory` in a micro-benchmark, plus the engine's per-step overhead A/B | decision D6, N2 | S |
| P3 | **Boot chain on a skeleton**: boot ROM extracted from the VHDL + SPI + `SdCardSpi` + a folder card + NextREG file + MMU, no video: the loader reaches `JP #6000`, the firmware module writes the expected NextREG sequence ([research-fpga-vhdl.md](research-fpga-vhdl.md) section 14) and soft-resets into the personality ROM | N9 risk early; the NextREG golden sequence; the card-side requirements | M |
| P4 | A second guest for the DivMMC: **UnoDOS 3** (open source, GPLv3) boots on the existing classic machine plus a new `DivMmcPaging` device (also PLAN #63's first step) and lists a folder through the SPI + `SdCardSpi` path | DivMMC / automap model, `RST $08` flow, TTD blob for the device | M |
| P5 | Compositor from dumps: a standalone function takes a frame state (NextREGs, bank 5 / 7, Layer 2 RAM, palettes, sprite RAM) and produces the pixels by the VHDL rules; compared with V1 photos for the layer-mixing programs | the per-line renderer design, performance | M |
| P6 | Sprite line budget: a small cycle model of `sprites.vhd`'s state machine (S_START / S_QUALIFY / S_PROCESS) counting clocks per sprite and per pixel | the "max sprites per line" flag instead of a count of 100 | S |
| P7 | DMA transfer timing at 3.5 / 7 / 14 / 28 MHz from `dma.vhd` (cycles per byte, prescaler, burst release) | N8; the bus-agent granularity | S |

Order: P1, P2, P3, P4 first (they decide the architecture and de-risk the boot); P5-P7 as the video / DMA phases come near.

## Later (no phase number yet)

- **esxDOS handler mode** (host folder answers `RST $08`, no firmware): optional, after N9 ([esxdos-and-sd.md](esxdos-and-sd.md) E1).
- **Older cores** (before 3.02.02): kept in mind, not built ([requirements.md](requirements.md) Q2).
- **UNS** (the universal snapshot format for all machines): the Next's full state goes into the TTD checkpoint container until UNS lands ([requirements.md](requirements.md) Q9).
