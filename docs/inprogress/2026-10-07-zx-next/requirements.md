# ZX Spectrum Next: requirements

**Date:** 2026-10-07 · part of [README.md](README.md) · phases in [phases.md](phases.md)

Confidence: **H** = two independent sources agree (or the FPGA source says it); **M** = one primary source;
**O** = open, needs the evidence step named in the last column before it is built. Sources are listed in
[research-sources.md](research-sources.md); register facts in [research-nextreg-and-ports.md](research-nextreg-and-ports.md).

## 1. Goals

| ID | Goal |
|:--|:--|
| G1 | `NEXT` is creatable. The real boot chain (bootrom, `TBBLUE.FW`, NextZXOS) runs from an SD card image or an unpacked folder and reaches the NextZXOS menu |
| G2 | Hardware faithful, not software-compatible only: ports, NextREGs, MMU, layers, copper, DMA, interrupts behave as the FPGA core does, checked against the VHDL-derived tables and the two C++ reference emulators |
| G3 | Nothing changes for any other model: the Z80N is a separate library, the Next's video and ports live behind the Next's own decoder, shared hot paths gain no test (A/B benchmark when a shared file is touched) |
| G4 | Automation parity: every Next device and report is reachable from CLI, WebAPI (+ OpenAPI), MCP, Lua, Python and Qt, and each phase leaves a verified recipe in `.recipe/machines/` |
| G5 | TTD records every run (a recording is started before the run under test), checkpoints carry the whole machine, blobs are append-only ids |
| G6 | Snapshots: NEX loads; `.sna` / `.z80` / `.szx` load into the Next's classic personalities; a native checkpoint-based save exists; machine state transfer works where physically possible |
| G7 | Real board variants are modelled as board profiles (Issue 2, Issue 4 / KS2, KS3 and, if it is confirmed to exist, a Mini), with the differences that software can see |
| G8 | Simple code: tables and plain functions, no pattern layers; the first renderer is per-scanline and naive |

## 2. Non-goals (this design)

| Not a goal | Why |
|:--|:--|
| Cycle-exact FPGA pin behavior (HDMI encoder, scandoubler, VGA/RGB timings 0-7) | video output is the emulator's own frame buffer; NR `#11` and NR `#09` scanline weight are stored and reported only |
| The Raspberry Pi accelerator board running Linux | only the Pi-facing ports are stubs (UART, SPI select, I2S control registers) |
| The flash programmer path (writing `TBBLUE.TBU` to FPGA flash) beyond a persistent flash blob | needed only by core updaters; flash is a stored blob ([design-boot-and-firmware.md](design-boot-and-firmware.md) section 5) |
| Real expansion-bus cards other than those already emulated as ZX-bus slot cards (General Sound, NeoGS, ...) | covered by the slot system; the Next's bus gating registers are modelled, cards come later |
| Network access through the ESP | an emulated ESP-01 exists in jnext; here it is a later phase (N10) and off by default |
| cartridge-less ROM images from outside the SD card for the firmware ROMs | the personalities load their ROMs from `/machines/next/` on the card, as the firmware does |

## 3. Requirements

### 3.1 Machine, memory, CPU

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R1 | Model `NEXT` (`MM_NEXT`, 2048K) becomes creatable; the model table row stays; `list_models` reports `creatable: true`; RAM sizes offered: 2048K default, 1024K for Issue-2-class boards if confirmed | M | model table (`config.h`); MAME `specnext.cpp` (`ks1` 1M, `ks2` 2M); Q4 |
| R2 | RAM is 8K pages numbered per the Next map: ROM = 8K pages 0-1 (or page 255 selector), ULA bank 5 = pages 10-11, bank 7 = 14-15, RAM from page 16, pages up to 223 on a fully expanded (2 MB) machine; an unexpanded machine has 768K in total of which 256K is ROM / firmware space (wiki), exact page ranges per size are open (Q4) | M | NR `#50` text in `nextreg.txt`; wiki Memory map |
| R3 | The 64K address space is eight 8K MMU slots (NR `#50`-`#57`); a write of 255 shows the ROM; the 128K / +3 / Pentagon-512 ports are views onto the same slots | H | `nextreg.txt`, `ports.txt`, MAME, jnext |
| R4 | Memory decode order for 0-16K: bootrom, config mapping, Multiface, DivMMC, Layer 2 mapping, MMU, expansion-bus ROM, ROM; 16K-48K: Layer 2 mapping, MMU; 48K-64K: MMU | M | `nextreg.txt` tail; jnext FPGA notes |
| R5 | CPU is Z80N: all `ED`-prefixed extension opcodes of [research-z80n.md](research-z80n.md) at the right T-state counts and flag effects; run speeds 3.5 / 7 / 14 / 28 MHz (NR `#07`) | H | wiki; MAME `z80ndasm.cpp`; jnext `z80n_ext.cpp` |
| R6 | The base Z80 and every other machine's CPU path are unchanged; the Z80N lives in `core/src/3rdparty/` and runs through `ICpuEngine` | - | design-cpu |
| R7 | Machine types 48K, 128K / +2, +3 / +2B and Pentagon (NR `#03`), each with its ROM set, port behavior and display timing | H | `nextreg.txt`; MAME `machine_type_*`; jnext |
| R8 | Hard reset, soft reset and the NR `#02` reset bits; the reset-type dependent register defaults ("soft reset = x", "hard reset = y" in `nextreg.txt`) | H | `nextreg.txt` |

### 3.2 NextREG, ports, interrupts

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R10 | NextREG space: every register of the core in use with read/write behavior and reset values, as data (one table) | H for 3.1.5 list; M for newer ones | `nextreg.txt`; MAME handles NR `#0B`, `#0F`, `#20`, `#8F`, `#B2`, `#B8`-`#BB`, `#C0`-`#CE`, `#D8`-`#DA`, `#F0`, `#F8`-`#FA` beyond that list |
| R11 | The port map of `ports.txt` including the "precedence" notes (DAC ports over AY, `#DFFD` over AY) and the internal / expansion port-enable registers NR `#82`-`#89` | H | `ports.txt`, `nextreg.txt` |
| R12 | The `NEXTREG` opcodes write registers without touching the `#243B` select latch | M | jnext comment citing the VHDL |
| R13 | Interrupts: ULA frame, line interrupt, CTC channels, UARTs, DMA end-of-transfer, in pulse mode or hardware IM2 mode with the 14-source priority and `RETI` handling; NMI sources (Multiface, DivMMC buttons) with the stackless NMI option | M | MAME `specnext_im2`, NR `#C0`-`#CE`; jnext `im2.cpp` |
| R14 | Alias ports: NR `#69` mirrors `#123B` bit 1, `#7FFD` bit 3 and `#FF` bits 5:0; NR `#8E` and NR `#8F` mirror the paging ports | H | `nextreg.txt`; MAME |

### 3.3 Video

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R20 | ULA: standard, Timex hi-colour and hi-res (`#FF`), ULA+ (`#BF3B` / `#FF3B`), ULANext attribute formats, shadow screen, X/Y scroll, clip window, 256-entry ULA palettes | H | `nextreg.txt`, `ports.txt`; jnext FEATURES |
| R21 | LoRes 128x96 (8-bit) and Radastan 128x96x4 (6144 bytes), scroll and palette offset | H | NR `#6A`; jnext |
| R22 | Layer 2: 256x192x8, 320x256x8, 640x256x4; active and shadow banks; scroll; clip; palette offset; priority-colour bit | H | NR `#12`-`#18`, `#70`, `#71`, `#44`; `ports.txt` `#123B` |
| R23 | Tilemap: 40x32 and 80x32, 8x8 tiles, 512-tile mode, text mode, per-tile attributes (mirror / rotate / palette offset / ULA-over), scroll, clip, base addresses in bank 5 | H | NR `#2F`-`#31`, `#6B`, `#6C`, `#6E`, `#6F`, `#1B`, `#4C` |
| R24 | Sprites: 128, 16x16, 8-bit and 4-bit patterns, scale x1/2/4/8, rotate / mirror, relative / composite sprites, 4-5 attribute bytes, collision and overtime flags, clip window, over-border mode | M | `ports.txt` `#303B`, NR `#34`-`#39`, `#75`-`#79`, wiki sprites page (open: read in design phase N5) |
| R25 | Eight palettes (ULA, Layer 2, sprites, tilemap, each first and second), 9-bit colour, auto-increment, read / write through NR `#40`-`#44`; fallback colour NR `#4A` | H | `nextreg.txt` |
| R26 | Layer order and blend modes (NR `#15` bits 4:2: SLU, LSU, SUL, LUS, USL, ULS, two blend modes), global transparency NR `#14`, ULA / tilemap stencil and blend (NR `#68`) | H | `nextreg.txt`; jnext compositor notes |
| R27 | Copper: 2K instruction RAM, WAIT / MOVE / NOP / HALT, four start modes (NR `#62`), position-triggered, writes registers below `#80` only | H | `nextreg.txt`; MAME `specnext_copper` |
| R28 | Vertical line offset NR `#64` moves ULA row 0 against the raster counter and so the copper, line interrupt and active-line registers | H | `nextreg.txt` |

### 3.4 Devices

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R30 | zxnDMA: Z80-DMA register set (`#0B`) and ZXN mode (`#6B`) with the burst mode and prescaler for sample playback; transfers memory-memory, memory-port, port-memory; the CPU is held while it works | M | `ports.txt`; MAME `specnext_dma` (derived from `z80dma`, "intensive testing"); the wiki page is to be read in phase N6 |
| R31 | CTC: four channels at `#183B`-`#1B3B` with interrupts and zero-count chaining | M | jnext FPGA notes (ports); MAME `specnext_ctc` |
| R32 | Three AY chips (TurboSound), selection through `#FFFD`, per-chip mono, ABC / ACB stereo, YM / AY mode (NR `#06`), four 8-bit DACs on the port aliases of `ports.txt`, beeper (EAR / MIC) | H | `ports.txt`; MAME (3 x `YM2149` at 14 MHz / 8) |
| R33 | SPI: `#E7` select (SD0, SD1, Pi SPI 0/1, flash), `#EB` data; two SD sockets with the NR `#0A` swap; the `#EB` read returns the previous exchange | H | `ports.txt`; storage survey section 1 |
| R34 | DivMMC: `#E3` control (readable, 4 bank bits, MAPRAM sticky), automap through NR `#B8`-`#BB` entry points, 128K DivMMC RAM, esxDOS ROM from the card | H | storage survey section 1; MAME `specnext_divmmc` |
| R35 | I2C bit-banged on `#103B` / `#113B` with a DS1307 RTC at address `#68`; optional on the board | M | `ports.txt`; MAME `i2c_ds1307` |
| R36 | Two UARTs (ESP, Pi) on shared ports `#133B` / `#143B` / `#153B`, prescaler baud, FIFOs, interrupts; joystick-port UART redirect | M | `ports.txt`; MAME `specnext_uart` |
| R37 | Input: 8x5 keyboard matrix with extended keys NR `#B0` / `#B1`, PS/2 keyboard and mouse (Kempston mouse ports `#FBDF` etc.), two joystick connectors with Kempston, Sinclair, Cursor, MD 3/6 button and I/O modes | H | `ports.txt`, `nextreg.txt` |
| R38 | Multiface (+3 type on the Next), with the M1 button NMI | M | `ports.txt`; MAME `specnext_multiface` |
| R39 | Expansion-bus gating registers (NR `#80`-`#8A`) stored and honored for the internal devices; external cards are the slot system's business | M | `nextreg.txt` |

### 3.5 Firmware, media, snapshots, tooling

| ID | Requirement | Conf. | Evidence |
|:--|:--|:--|:--|
| R40 | The bootrom (8K) runs at reset, loads `TBBLUE.FW` through SPI, the firmware reads `config.ini` / `menu.def` and loads ROMs into the ROM RAM area, writes NR `#03`, and soft-resets into the chosen personality | H | wiki Boot Sequence; jnext bypass study (traces the firmware's NextREG writes) |
| R41 | The first boot writes `config.ini` to the card, so the card is writable (session access) | H | storage-manager integration doc |
| R42 | SD cards are media-manager slots `sd.next0`, `sd.next1` (`required` on the first); an unpacked `sn-complete` folder works through `HostFolderFat` | H | storage-manager docs |
| R43 | NEX V1.0-V1.2 files load (header, optional screen, banks in the NEX order, entry point); V1.3 is an opt-in | H (1.2) / M (1.3) | wiki NEX page; MAME `snapshot_nex.cpp`; jnext FEATURES |
| R44 | `.sna` (48K and 128K), `.z80`, `.szx` of classic machines load into the Next in the matching personality; `.snx` loads as a 128K `.sna` | M | ZEsarUX `snap.c`; jnext FEATURES; design-media-and-snapshots |
| R45 | Saving: a NEX from the running machine (limited, say so), an `.szx` for the classic personalities, a TTD checkpoint file for everything | O | Q9 |
| **Decided (owner 2026-10-08): TTD checkpoint container for now; later the planned UNS format for all machines at once** (no Next-specific `.nxs`). NEX / SNX / SZX stay the exchange formats with their limits ([design-media-and-snapshots.md](design-media-and-snapshots.md)) | A debugger view per layer / device: NextREG dump, MMU map, sprites, copper list and position, palettes, DMA state, with symbols working on banked 8K pages | M | jnext debugger panels (survey); DeZog protocol commands (`CMD_GET_SPRITES`, ...) |
| R47 | Z80N is known to the disassembler, the assembler (`unreal-asm` `ZXN` mode), the DZRP (DeZog) and GDB servers | O | Q10 |

## 4. Board variants (summary, details in [research-variants.md](research-variants.md))

| **Decided (owner 2026-10-08): the debugger disassembler and the `unreal-asm` ZXN mode in N1; the DZRP (DeZog) and GDB servers in N12** | What software can see | Status |
|:--|:--|:--|
| Issue 2 (Spartan 6 FPGA) | machine id `#0A`; the jnext author targets it as the reference; 1 MB RAM unexpanded, 2 MB with the accelerator | M |
| Issue 4 / KS2 (Artix 7) | same register map, 2 MB RAM on every machine, bigger flash with more core slots (15 per a press report), HDMI timing notes on the shop site | M |
| KS3 | MAME models it as Issue 5 with 4 MB; no other source read says so | O (Q3) |
| "Next Mini" | no source read mentions it | O (Q1) |
| Emulators id (`#08`) | MAME's base `tbblue` machine reports it so that software detects an emulator | M |

Variants are one `NextBoard` profile struct (machine id, board issue, RAM size, keyboard issue-2 default, RTC fitted,
flash slots), chosen by `[NEXT] Board=`, exactly like `ProfiBoard` ([design.md](design.md) D3).

## 5. Acceptance scenarios

| # | Scenario | Phase |
|:--|:--|:--|
| A1 | Create `NEXT`: it starts, runs 48K BASIC from a ROM image with no SD card (the "bare" personality), the border and keyboard work | N3 |
| A2 | Z80N test programs (the opcode tests of ZXSpectrumNextTests) pass on the engine | N1-N2 |
| A3 | A program that sets Layer 2 320x256, sprites and the copper renders a frame equal to MAME's or jnext's for the same registers (screenshot compare with a stated tolerance) | N6-N8 |
| A4 | An unpacked `sn-complete` folder in `sd.next0` boots the real bootrom, `TBBLUE.FW` and NextZXOS to its main menu | N9 |
| A5 | A NEX demo loads and runs with sprites, copper, DMA audio and three AY chips | N8-N11 |
| A6 | A TTD recording started before the NextZXOS boot can be sought backwards to the first menu frame and forwards again, bit-exact | N9, N10 |
| A7 | Every phase has a recipe whose commands were run and whose outputs are real | each |

## 6. Open questions

| ID | Question | Next step |
|:--|:--|:--|
| Q1 | Does a "ZX Spectrum Next Mini" exist, and what is different (FPGA, RAM, ports, no expansion bus?) | **Dropped for now**: no source we hold mentions a "Next Mini"; reopen if someone points to one |
| Q2 | Exact register list for the newest cores (3.02.xx): NR `#0B`, `#0F`, `#20`, `#8F`, `#B2`, `#C0`-`#CE`, `#D8`-`#DA`, `#F0`, `#F8`-`#FA` meanings | **Decided (owner 2026-10-08): core 3.02.03 only for now; older cores are kept in mind, not built yet.** The design leaves room (a core-version field in `NextBoard`, the change list in research-fpga-vhdl.md section 24) but no code or test for pre-3.02.02 behavior until a real program needs it |
| Q3 | Are there KS3 hardware differences software can see (4 MB RAM?) | Same decision as Q4: no hardware difference visible to software is known; 4096K only as a size label ([Q4]) |
| Q4 | Which RAM sizes exist per issue (1 MB / 2 MB; MAME offers 1M, 2M, 4M) | **Decided (owner 2026-10-08): offer 768K, 1024K, 2048K and 4096K.** Check against the VHDL (core 3.02.03, `zxnext.vhd` memory decode): the MMU page number is 8 bits and a page `#E0` or above selects ROM, so RAM tops out at 1792K above the 256K system area = **2048K total**; 4096K (MAME's KS3) is not addressable in this core. Implementation: 4096K is accepted as a size but behaves as 2048K until a core that defines more is read (to be reported to the owner) |
| Q5 | Is the 128K timing 311 or 312 lines | **Closed: 311 lines** (VHDL `zxula_timing.vhd`; MAME's 312 is wrong), see research-fpga-vhdl.md section 1 |
| Q6 | Contention: does the Next contend like the real machine in 48K / 128K / +3 modes at 3.5 MHz, and is it disabled at speed (NR `#08` bit 6)? MAME lists contention as TODO | **Closed**: contention only at 3.5 MHz, not Pentagon, off with NR `#08` bit 6; banks and window per research-fpga-vhdl.md section 2 |
| Q7 | `LDIRSCALE` and `LDPIRX` timing and flags; whether `LDIRSCALE` exists on all cores | **Closed for LDIRSCALE**: decoded, scaling commented out (acts like `LDIRX`); `LDPIRX` microcode read, flags verified in N1 against Tests/base/Z80N |
| Q8 | Which firmware versions to support (`TBBLUE.FW` and boot ROMs 3.01.00, 3.02.00, 3.02.04) | **Default taken (follows Q2): the distribution that matches core 3.02.03** (`TBBLUE.FW` 1.44, the boot ROM extracted from the VHDL, NextZXOS of the `tbblue` tree read in N0); no multi-version support |
| Q9 | Native snapshot format: reuse the TTD checkpoint container, or a documented `.nxs` | decide in N11 with the snapshot-pipeline owner |
| Q10 | Assembler / DZRP / GDB scope for Z80N | N12 |
| Q11 | Whether esxDOS-only operation (no NextZXOS) is a supported mode (`esxdos-stub` like jnext) | **Decided (owner 2026-10-08): yes, as a separate optional mode in a late phase** (after the real chain boots in N9); not part of N9 |
| Q12 | Which NMOS/CMOS Z80 behaviors the Next's T80-based CPU shows (undocumented flags, LD A,I, OUT (C),0) | N1 with the z80test-style suites and ZXSpectrumNextTests |
| Q13 | Is there a real ZX Spectrum Next available to run small programs of ours? | **Yes, later, no date** (owner 2026-10-08): packaged as the autonomous POC [2026-10-08-zx-next-hardware-poc](../2026-10-08-zx-next-hardware-poc/README.md); the FPGA source stands as evidence meanwhile |
