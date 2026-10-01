# Peters Plus Sprinter Sp2000 machine support

**Created:** 2026-09-28 · **Status:** design drafted, review round 1 done (2026-09-28); S0 done
except the MAME captures (2026-10-01); emulation not started; PLAN row #59, after TSConf and the shared-infrastructure row #60 (see
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
| [high-level-design.md](high-level-design.md) | components, port access, boot, memory write path, storage (diagrams); decisions D1-D11 |
| [technical-design.md](technical-design.md) | index of the detailed designs; the shared hooks (clock ratio, wait states, write intercept, interrupt source); risks |
| [tdd-ports-memory.md](tdd-ports-memory.md) | port decoder, PLD state, memory windows, graphics pages, configuration loader, PLD configuration modules, resets |
| [tdd-video.md](tdd-video.md) | video RAM, renderer, palettes, INT from the mode table |
| [tdd-storage.md](tdd-storage.md) | floppy (density, PC images), IDE adapter (two channels, A8 latch), CMOS, media slots, the DSS boot profile for folder volumes |
| [tdd-accel-sound-input.md](tdd-accel-sound-input.md) | accelerator, Covox-Blaster, keyboard (matrix + AT codes), mouse, Z84C15 SIO/CTC/PIO |
| [tdd-integration.md](tdd-integration.md) | model registration, config, ROM, TTD ids, snapshots, automation, Qt debugger |
| [unreal-ng-mapping.md](unreal-ng-mapping.md) | reused as-is / extended / new; how it plugs into the media manager, IDE core, TSConf hooks, ZX-Evo E2b |
| [roadmap-and-plan.md](roadmap-and-plan.md) | phases S0-S7, dependencies on PLAN rows, sizes, what can start now, review round 1 decisions |
| [test-plan.md](test-plan.md) | tests by layer with IDs, firmware tests, test data, coverage matrix |

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
- PLD configurations are **modules** (`SprinterPldConfiguration`): v1 ships the Standard one; the
  Game, DooM and Video configurations can be added later without touching the decoder core.

## Glossary

| Term | Meaning |
|---|---|
| **PLD** | the Altera ACEX chip that implements almost all of the Sprinter's logic; loaded from ROM at power-on |
| **Configuration / bitstream** | the file loaded into the PLD (~59 KB); "the standard configuration" = the Sp2000 build in the BIOS ROM |
| **Configuration module** | the emulator's code for one PLD configuration (`SprinterPldConfiguration`); Standard is the only one in v1, others (Game, DooM, Video) plug in later |
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
