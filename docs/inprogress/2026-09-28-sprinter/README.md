# Peters Plus Sprinter Sp2000 machine support

**Created:** 2026-09-28 · **Status:** design drafted, review round 1 done (2026-09-28); S0 done
(2026-10-01, MAME captures included, branch `sprinter-mame`); **S1 done** (2026-10-01, branch `sprinter-s1`: creatable `SPRINTER`, BIOS 3.04 reaches its boot prompt); **CPU library** (2026-10-01, branch `sprinter-cpu`: the Z84C15 runs on its own CPU library, [2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/README.md)); **S2 done** (2026-10-01, branch `sprinter-s2`: the video renderer, the BIOS logo equals MAME's frame); **S3a done** (2026-10-01, branch `sprinter-s3a`: DSS 1.62 boots from the floppy, Spectrum mode with TR-DOS); **S3b done** (2026-10-02, branch `sprinter-s3b`: IDE on two channels, DSS boots from a hard disk image); S4-S7 not started; PLAN row #59, started by the owner on 2026-10-01, with the shared-infrastructure row #60 done (see
[TODO.md](TODO.md))

Add the Sprinter Sp2000 (Peters Plus, 2000) as a creatable machine `SPRINTER`: its own BIOS boots,
fills its programmable port table and boots Estex DSS from a floppy, a hard-disk image or a PC
folder; its Spectrum mode runs BASIC and TR-DOS; its graphics modes, accelerator and
Covox-Blaster work; and it has TTD, debugger and automation support like the other models.

## Documents

| File | Content |
|---|---|
| [goals-and-requirements.md](goals-and-requirements.md) | goals, what "done" means, non-goals, FR / NFR, acceptance scenarios on real firmware |
| [materials.md](materials.md) | every source with links and revisions, what each is good for, what is missing, ROM and test-data provisioning, licenses |
| [hardware-reference.md](hardware-reference.md) | the machine as the emulator needs it: CPU and clocks, memory, **the port table**, video, accelerator, sound, IDE, floppy, ISA, CMOS, input, boot; source disagreements tabulated |
| [peripherals-wiring.md](peripherals-wiring.md) | how every peripheral is wired and driven, with diagrams: the three roads from the CPU (on-chip ports, the DCP, memory-mapped), keyboard, mouse, video, accelerator, FDD, IDE, CMOS, SD (none on board), ISA, General Sound / NeoGS through the ZX-bus adapter |
| [sprinter-cpu-and-peripherals.md](../../emulator/design/core/sprinter-cpu-and-peripherals.md) | the emulator's architecture: who owns the Z84C15 and the engine, one instruction step end to end, the time model, memory and port paths, CTC / SIO / PIO / watchdog / daisy chain on the Sprinter, the PLD's INT, event delivery, performance; where the older documents differ from the code |
| [test-disk-images.md](test-disk-images.md) | test disks that boot straight into a program: why the shipped disk does not, the in-session rewrite, the composite medium, the host copy; which to pick, the same boot every time, what was checked |
| [Sprinter demo benchmarks](../../../core/benchmarks/emulator/machines/README.md) | `BM_SprinterDemo_*`: host CPU per frame of DNTBLINK, ROTOZOOM, PLASMA2, BADAPPLE from the system disk, without and with the shipped sound cards; how they start, what they measure, how to compare two builds, results |
| [high-level-design.md](high-level-design.md) | components, port access, boot, memory write path, storage (diagrams); decisions D1-D11 |
| [technical-design.md](technical-design.md) | index of the detailed designs; the shared hooks (clock ratio, wait states, write intercept, interrupt source); risks |
| [tdd-ports-memory.md](tdd-ports-memory.md) | port decoder, PLD state, memory windows, graphics pages, configuration loader, PLD configuration modules, resets |
| [tdd-video.md](tdd-video.md) | video RAM, renderer, palettes, INT from the mode table |
| [tdd-storage.md](tdd-storage.md) | floppy (density, PC images), IDE adapter (two channels, A8 latch), CMOS, media slots, the DSS boot profile for folder volumes |
| [research-zx-mode.md](research-zx-mode.md), [tdd-zx-mode.md](tdd-zx-mode.md) | the ZX (Spectrum) mode: how the real machine runs TRD / SCL / tape (RAM disk + TR-DOS 7.0x, no PLD trap), MAME runs, the design and phase S8 |
| [tdd-accel-sound-input.md](tdd-accel-sound-input.md) | accelerator, Covox-Blaster, keyboard (matrix + AT codes), mouse, Z84C15 SIO/CTC/PIO |
| [tdd-integration.md](tdd-integration.md) | model registration, config, ROM, TTD ids, snapshots, automation, Qt debugger |
| [unreal-ng-mapping.md](unreal-ng-mapping.md) | reused as-is / extended / new; how it plugs into the media manager, IDE core, TSConf hooks, ZX-Evo E2b |
| [roadmap-and-plan.md](roadmap-and-plan.md) | phases S0-S7, dependencies on PLAN rows, sizes, what can start now, review round 1 decisions |
| [test-plan.md](test-plan.md) | tests by layer with IDs, firmware tests, test data, coverage matrix |
| [atapi-cd-boot.md](atapi-cd-boot.md) | booting from an ATAPI CD: which BIOS can, the CMOS cells and checksum, the boot sector at sector 17, what a bootable CD needs, what is missing, verified on the emulator |
| [estex-dss-build.md](estex-dss-build.md) | building Estex DSS (kernel, shell, installer, boot loader) from source with sjasmplus, the symbols for debugging, putting the build on a disk; the built 1.71.66 is in testdata |
| [pld-configurations.md](pld-configurations.md) | PLD configurations overview: how bitstreams load on the hardware, how MAME and unreal-ng model them as modules, the differences and why (read first); §6 is the full account of the DooM and Video configurations (Sprinter 97 legacy, not planned) |
| [game-configuration.md](game-configuration.md) | the "Game" PLD configuration (V10): the bitstream, what it changes, the grid-offset picture, the module, tests |

## Key findings

- **Ports are data, not wiring.** Every external port access reads one byte from RAM page `#40`;
  the index is built from A15, A14, A13, A7, A6, A5, A2-A0, read/write, the TR-DOS signal, the
  `#7FFD` lock bit and one of four maps. The BIOS writes the table at start-up; the emulator must
  look it up, not hard-code it (MAN §13.1; MAME `sprinter.cpp:584`).
- The IDE channel-select strobes, decoded from the BIOS table: `OUT (#BC),A` with **A = `#21` →
  primary**, **A = `#01` → secondary** (the value lands on A15-A8, A13 picks the channel). The
  same trick selects floppy density (`#21BD` = 1.44 MB, `#01BD` = 720 KB) and the frame length
  (`#41BD` = 320 lines, `#61BD` = 312).
- The IDE data latch is **one PLD register for both directions**, holding the low byte on writes;
  the BIOS alternates A8 through `INI`/`OUTI`'s B countdown (PLD `SP2_1K30.TDF`; BIOS-TT
  `ATA_DRV.ASM`).
- DSS boots from **LBA 1** (a 3-sector loader starting with `Starting...`), accepts FAT12/FAT16 only,
  and its loader checks **only MBR entry 0** (DSS `DOSBOOT4.ASM`). A folder volume becomes
  bootable by putting the loader from `BOOT.EXE` at LBA 1-3.
- A DSS 1.62 **bootable 1.44 MB floppy image** is published (app.sprinter.ru): the first acceptance
  test needs no HDD at all.
- unreal-ng already has the M1 hook and 256 RAM pages (exactly the Sprinter's 4 MB); it lacks a ×6
  clock ratio, a write intercept, a programmable interrupt source (both specified by TSConf), a raw
  PC floppy loader, the IDE core, and any Z84C15 device. The shared pieces are planned before the
  Sprinter (PLAN #60, TSConf #41).
- The exact BIOS 3.04 image (CRC `1729cb5c`) is in the board repository; the ZXMAK2 copy is the same
  build with a different board-id byte. No 3.04 source is public, so S0 disassembles it
  ([materials.md](materials.md) §5).
- PLD configurations are **modules** (`SprinterPldConfiguration`): Standard and, since 2026-10-03, Game
  ([game-configuration.md](game-configuration.md): GAME_00, LDConf's GC.BIN); DooM and Video can be added the same
  way without touching the decoder core.

## Glossary

| Term | Meaning |
|---|---|
| **PLD** | the Altera ACEX chip that implements almost all of the Sprinter's logic; loaded from ROM at power-on |
| **Configuration / bitstream** | the file loaded into the PLD (~59 KB); "the standard configuration" = the Sp2000 build in the BIOS ROM |
| **Configuration module** | the emulator's code for one PLD configuration (`SprinterPldConfiguration`); Standard and Game today, others (DooM, Video) plug in later |
| **Grid offset** | the Game configuration's per-square scroll: Mode3 of a square with Mode0 bit 2 shifts the squares that follow ([game-configuration.md](game-configuration.md) §3) |
| **DCP / port table** | the 16 KB table in RAM page `#40` that maps a port access to an internal device code |
| **Internal code** | the byte from the table, e.g. `#27` = IDE command register |
| **Cells** | PLD registers `#C0-#FF` (page numbers, `#1FFD`, `#7FFD`, video registers) |
| **Map (CNF 0-3)** | one of the four 4 KB tables in page `#40`, chosen by the CNF port |
| **vROM** | a RAM page holding a Spectrum ROM image, mapped as read-only ROM in Spectrum mode |
| **Fast RAM / cache** | 64 KB of RAM that runs without wait states at 21 MHz |
| **Square** | an 8×8 (or 16×8) screen cell; each has its own mode bytes |
| **Mode table / mode page** | the 4 bytes per square in video RAM that choose text or graphics, the palette and the source address; two pages switched by RGMOD |
| **PORT_Y / RGADR** | the register that selects the video line for graphics writes and the block for Spectrum-screen shadow writes |
| **Accelerator** | a 256-byte PLD buffer that repeats a memory access many times (fill, copy, logic ops) |
| **Covox-Blaster (CBL)** | a Covox with a 256-sample ring buffer and its own playback clock |
| **Z84C15** | the CPU: a Z80 with a timer (CTC), two serial ports (SIO), a parallel port (PIO) and system registers |
| **DSS / Estex DSS** | the Sprinter's disk operating system (FAT12/FAT16, `.EXE` programs) |
| **BIOS disk API** | `RST #08` functions `#51-#5F` with a 32-bit sector number in HL:IX |
| **TTD** | unreal-ng's time-travel debugging (record, seek, replay) |
| **Media manager** | the planned unreal-ng component that owns every drive slot (PLAN #58) |

Source short names (MAN, BIOS-TT, INC, DSS, PLD, MAME, …) are defined in [materials.md](materials.md).
