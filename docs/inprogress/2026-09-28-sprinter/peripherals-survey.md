# Sprinter Sp2000 peripherals: what exists, what MAME has, what people use, what we still lack

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Research only. No code changed. Items not yet planned anywhere are added to [TODO.md](TODO.md) ("Peripherals not yet planned") |
| **Machine** | Peters Plus Sprinter Sp2000 (`SPRINTER` model in unreal-ng) and its community re-issues |
| **Related** | [mame-gap-analysis.md](mame-gap-analysis.md), [TODO.md](TODO.md) ("Input and device extras"), [hardware-reference.md](hardware-reference.md), ISA design [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/research.md), PLAN rows #59, #82 (buses and slots), #83 (shared CD audio) in [PLAN.md](../PLAN.md) |

This survey covers what can be plugged into a Sprinter or is built onto its board. Four areas already have a
design or a queued task, and this document only points to them:

- **ISA cards**: the ZX-bus adapter with General Sound / NeoGS, ISA RAM, UART and Wi-Fi cards, ESS688 / Sound
  Blaster, SprinterJoy and Sprinter-FT. See [2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/research.md),
  where the owner's decisions are also recorded.
- **Network**: the NE2000 ISA Ethernet card, ESP Wi-Fi, ZXNETUSB and modems. That design is being written in
  parallel, in `docs/inprogress/2026-10-02-sprinter-network/` on branch `sprinter-network-design`.
- **Spectrum mode**: also designed in parallel, in `tdd-zx-mode.md` on branch `sprinter-zxmode-design`.
- **Input and device extras**, queued in [TODO.md](TODO.md) from [mame-gap-analysis.md](mame-gap-analysis.md):
  - the two Sega-style pads (I10);
  - the serial mouse variants and the CTC wiring (I7, C11);
  - wiring the ATAPI CD to the Sprinter's IDE;
  - a test of tape input (I5);
  - CD audio for every machine, which is PLAN #83.

  Keyboard commands and LEDs are not planned; the owner does not need them.

## Contents

1. [In short](#1-in-short)
2. [Glossary](#2-glossary)
3. [Sources](#3-sources)
4. [Board revisions](#4-board-revisions)
5. [Inventory, by kind of device](#5-inventory-by-kind-of-device)
6. [MAME compared with unreal-ng](#6-mame-compared-with-unreal-ng)
7. [What the community uses today](#7-what-the-community-uses-today)
8. [Not yet covered by us, by priority](#8-not-yet-covered-by-us-by-priority)
9. [Open questions](#9-open-questions)
10. [Developer interest (2026-10-02)](#10-developer-interest-2026-10-02)

## 1. In short

- The Sp2000 board has these devices built in:
  - a floppy controller and IDE;
  - an AT keyboard port and an MS serial mouse port;
  - a Kempston joystick port with tape in and out;
  - a **Centronics printer port**;
  - the Dallas clock;
  - stereo sound;
  - two ISA-8 slots.
- **Everything built in is done or queued in unreal-ng except the Centronics printer port.** The printer has a
  driver in every DSS (function `#5F PRINT`). Neither MAME nor unreal-ng emulates it.
- The newest community hardware adds two things to look at:
  - the **ISA Wild Sound** sound card (STM32 microcontroller, AYX-32 compatible, a demo player on the owner's MAME
    disk). The ISA research lists this demo's card as "unknown"; this survey identifies it.
  - **2026 extensions in the community programmable-logic firmware**: an accelerator control register, rectangle
    mode and clipping, and a scale port. Tolik-Trek's own MAME fork has them (written by Andrei Holub, §10.2); upstream MAME and unreal-ng do not.
- Board revisions (sp2000, sp2000-light, sp2000s, sp2003s, sp2016s, sp2022d, and boards with the larger EP1K50
  chip) change almost nothing that software can see. Two exceptions:
  - the light board has no ISA slots and no second IDE channel;
  - the 2022d board carries 512 KB video RAM and a 512 KB ROM, which no known program uses.
- **Output and power mods need nothing from the emulator.** These are the VGA / HDMI scandoublers, the video DAC
  mod, the PAL coder, ATX power, the Dallas replacement and the SIMM module.
- **Not found for the Sprinter:** light guns, MIDI interfaces, USB adapters (discussed in 2004, never built) and SD
  adapters on the main board. The only SD cards are on the NeoGS and the Wild Sound.

## 2. Glossary

| Term | Meaning |
|---|---|
| **Centronics / LPT** | The parallel printer port of PCs: 8 data lines, a STROBE pulse per byte, and status lines back from the printer (BUSY, ACK, PAPER END, SELECT, ERROR) |
| **PIO, SIO, CTC** | The parallel port, the two serial ports and the timer inside the Sprinter's Z84C15 processor chip. The Sprinter wires keyboard, mouse, printer and pads to them |
| **Kempston joystick** | The common Spectrum joystick interface: one port byte, one bit per direction and fire |
| **Sega pad** | A Sega Mega Drive game pad: 8 directions and up to 8 buttons. It is read through a select line that the computer toggles, which switches which buttons the pad reports |
| **ISA-8** | The 8-bit PC expansion slot. The Sprinter reaches its two slots through memory window 3 (see the ISA research) |
| **ZX-bus** | The edge connector of Spectrum clones (General Sound, NeoGS and others plug into it). On the Sprinter it is reached through an ISA adapter card |
| **PLD, bitstream, configuration** | The Sprinter's main logic is a programmable chip (Altera ACEX). The "bitstream" is its firmware, loaded at start; a "configuration" is one such firmware (Standard, Game, DooM, ...) |
| **Scandoubler** | A board that doubles the line rate of the 15 kHz TV picture so a VGA or HDMI monitor can show it. It changes the output only, not what software sees |
| **Mod** | A small add-on board or rework soldered onto the main board (power, video DAC, clock chip) |
| **CF** | CompactFlash card. It speaks ATA (IDE) natively, so an IDE-to-CF adapter lets it replace a hard disk |
| **Journaled input** | unreal-ng records every outside input at register level so time-travel replay stays exact |
| **Priority / size** | P1 (do next) to P4 (only on demand). Size on the repository's scale: **S** under a week, **M** 1-2 weeks, **L** 2-4 weeks |

## 3. Sources

| Short name | What | Where |
|---|---|---|
| **MAME** | `sprinter.cpp` (commit history down to the 2024 "2 full joysticks" change), `isa_cards.cpp` (the `pc_isa8_cards` list) | [sprinter.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/sprinter.cpp), [isa_cards.cpp](https://github.com/mamedev/mame/blob/master/src/devices/bus/isa/isa_cards.cpp), [PR #12878](https://github.com/mamedev/mame/pull/12878) |
| **MAME-TT** | Tolik-Trek's MAME fork: the "tmkonf" change (accelerator control, rectangle mode, clipping) | [zxgit.org/Tolik-Trek/MAME commit 7ea867099](https://zxgit.org/Tolik-Trek/MAME/commit/7ea867099) |
| **DISK** | The owner's MAME-pack system disk: `DOCS/INFO_011..016`, `PRICE.TXT`, `TESTS/JOY/` (Sega pad tests on the Kempston and LPT ports), `MODEM/`, `DEMOS/WILDSND/` | owner's MAME pack (local) |
| **INFO_014** | Peters Plus assembly guide: board connectors `MOUSE`, `KMPS` (Kempston + tape), `Spesial`, `Centronics`, `VIDEO`, `SND`; option list (5.25" drive, hard disk, Dallas clock, "ISA -> Spectrum-BUS adapter", PAL coder) | DISK `DOCS/INFO_014.TXT` |
| **LPT-WIRING** | Ivan Mak's note on the Sprinter LPT port: which Z84C15 pins carry each printer signal | [nedopc.org topic 7375](http://www.nedopc.org/forum/viewtopic.php?t=7375) |
| **DSS-PRINT** | Estex DSS function `#5F` (send a byte to the printer, status in bits 7-3) | [Estex-DSS `DSS/API/Print.asm`](https://zxgit.org/Tolik-Trek/Estex-DSS/src/branch/master/DSS/API/Print.asm) |
| **DOC-RU** | doc.sprinter.ru: ISA interrupts (PIO port B bits 6-7 = printer), printer page, the never-built Sprinter Sound Card | [isa-interrupts](https://doc.sprinter.ru/blocks/z84c15/isa-interrupts), [printer](https://doc.sprinter.ru/blocks/z84c15/printer), [ssc](https://doc.sprinter.ru/blocks/sound/ssc) |
| **2022D** | RomanRom2's sale thread for the sp2022d board and every community mod (with photos, prices, stock) | [zx-pk.com topic 21431](https://zx-pk.com/forum/viewtopic.php?t=21431); board summary [xlat's photo post](https://xlat.livejournal.com/895625.html) |
| **HW-2000** | Board history: sp2000, sp2000-light, sp2000s, sp2003s, sp2016s | [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000), [Mick's sp2016s page](http://micklab.ru/ZX%20Spectrum/Sprinter2000.htm) |
| **CARDS** | Community cards and mods: [SprinterJoy](https://zxgit.org/romych/SprinterJoy), [SprinterSerial](https://zxgit.org/romych/SprinterSerial), [SprinterESP](https://zxgit.org/romych/SprinterESP), [Sprinter-FT](https://zxgit.org/romych/Sprinter-FT), [ATXMod](https://zxgit.org/romych/ATXMod), [ATXMod2](https://zxgit.org/romych/ATXMod2), [SIMM-72](https://zxgit.org/romych/SIMM-72), [RGB2VGA](https://zxgit.org/romych/RGB2VGA), [dallas](https://zxgit.org/RomanRom2/dallas), [ide-splitter](https://zxgit.org/RomanRom2/ide-splitter), [pwr-sw](https://zxgit.org/RomanRom2/pwr-sw), [VGA module](https://github.com/SprinterTeam/Sprinter-VGA-module), [VGA-HDMI module](https://github.com/SprinterTeam/Sprinter-VGA-HDMI-module) | as linked |
| **WILD** | AYX-32 / Wild Sound II firmware discussion (the base of the ISA Wild Sound) | [forum.tslabs.info topic 883](https://forum.tslabs.info/viewtopic.php?f=31&t=883) |
| **FORUM** | nedopc.org Sprinter section: PC link ([7876](http://www.nedopc.org/forum/viewtopic.php?t=7876)), USB idea ([7428](http://www.nedopc.org/forum/viewtopic.php?t=7428)), PS/2 mouse ([7943](http://www.nedopc.org/forum/viewtopic.php?t=7943)), hardware devices ([7709](http://www.nedopc.org/forum/viewtopic.php?t=7709)), sound cards ([7701](http://www.nedopc.org/forum/viewtopic.php?t=7701)) | as linked |
| **CHAT** | The community's Telegram group (needs an account; not read) | [t.me/zx_sprinter](https://t.me/zx_sprinter) |
| **BIOS-TT** | The community BIOS (3.05-3.07): ATA / ATAPI / CF changes, the `ACEX.SCALE` port | [zxgit.org/Tolik-Trek/Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS) |

The downloaded materials and an index are kept outside the repository, in the owner's local Sprinter collection
(`peripherals/README.md` there).

## 4. Board revisions

| Board | Year | What changed that matters to an emulator | Others (no software effect) | unreal-ng |
|---|---|---|---|---|
| **Sp97** | 1996-2000 | A different machine: EPF10K10 logic, 128 KB ROM, its own port table (ISA through codes `#30-#33`), Soundrive per ZXF-6 (INFO_011) | - | not planned (another model) |
| **sp2000** | 2001 | The reference board: 4 MB SIMM, 256 KB video RAM, two ISA-8 slots, two IDE channels (the second one did not work on the first batch, HW-2000) | - | **the emulated board** |
| **sp2000-light** | 2001 | **No ISA slots, no second IDE connector**, 256 KB video RAM | cheaper parts | not modeled (§8 item 7) |
| **sp2000s** | 2003 | SOJ video RAM chips; BIOS 3.03 / 3.04 changed the logic firmware to remove video artifacts | - | covered by the 3.04 firmware |
| **sp2003s** ("SPRIN_3M") | 2003, small batch after 2009 | unreleased Peters design, built by loxic | - | same as sp2000 |
| **sp2016s** (Mick) | 2016-2017 | none (ATX power connector, PS/2 keyboard socket added 2017, SMD parts) | ATX, PS/2 | same as sp2000 |
| **sp2022d** (RomanRom2) | 2021-11 onward | **512 KB video RAM, 512 KB ROM** (2022D); EP1K30 or EP1K50 logic chip; optional MB8877A in place of the КР1818ВГ93 floppy controller | ATX-20, PS/2 socket, new DAC and audio mods | 512 KB unused by known software (§9 Q5) |
| **EP1K50 boards** | 2016+ | a larger logic chip; BIOS 3.05+ carries a loader for both chips ([bios-versions.md](bios-versions.md)) | - | no effect (we emulate the logic, not the chip) |

Example: an sp2000-light running ProPlay. The program maps ISA I/O into window 3 and reads GS status at
`#C0BB`. With no slot there, the bus floats high and the read returns `#FF`, which is what unreal-ng returns today
for an empty slot. So the light board differs from the full board only where a card would be present.

## 5. Inventory, by kind of device

Columns: **Common today** = how often a real Sprinter has it now (mass / some / rare / prototype / none).
**MAME** = upstream MAME. **unreal-ng** = done, queued (where), or not planned.

### 5.1 Input

| Device | What and where | Software | Common today | MAME | unreal-ng |
|---|---|---|---|---|---|
| AT / PS/2 keyboard | SIO A; PS/2 socket on sp2016s / 2022d | everything | mass | yes (MS Natural keyboard MCU) | **done** (S4) |
| Keyboard commands / LEDs | host to keyboard | none | - | no | not needed (owner) |
| Serial MS mouse | SIO B, `MOUSE` connector | DSS mouse driver, FN, Commander DOS, 2D Studio | mass | yes, 3 variants | **done** (2-button); variants queued (TODO "Input and device extras", I7) |
| PS/2 mouse through a PS/2-to-serial converter | an AVR box that speaks the Microsoft wheel protocol (`MZ@`, 4-byte packets, 1200 baud) | same; DSS reads the 3-byte part | mass today (old serial mice are scarce) | `wheel_mouse` option | queued with the variants (I7) |
| Kempston mouse view | the logic presents the serial mouse at `#FADF/#FBDF/#FFDF` | Spectrum-mode programs | some | yes | **done** (I8) |
| Kempston joystick | `KMPS` connector, code `#15` | Spectrum games | some | yes | **done** (I9) |
| Sega pad on the Kempston port | select by SIO B DTR | `TESTS/JOY/kmst_*19.exe`, `joytst11.exe` | some | yes (pad 1) | queued (I10) |
| Sega pad on the LPT port | data on PIO A, select by PIO B bit 7 | `TESTS/JOY/lpt_*19.exe` | rare | yes (pad 2) | queued (I10) |
| SprinterJoy ISA card (two Sega pads) | ISA I/O `#250` | its test program | prototype ("in development") | no | ISA design I7 (deferred) |
| Tape input / output | `KMPS` connector, `#FE` bit 6 / bit 3 | Spectrum loaders | rare | input yes | input test and the base-clock time base under turbo in S8 Z2 ([tdd-zx-mode.md](tdd-zx-mode.md)); output follows the shared tape path |
| Light gun | - | - | none found | no | not needed |
| USB adapter | idea only (FORUM 7428, 2004) | - | none | no | not needed |

### 5.2 Printer and links

| Device | What and where | Software | Common today | MAME | unreal-ng |
|---|---|---|---|---|---|
| **Centronics printer** | `Centronics` connector: data on PIO A (STROBE from PIO A RDY), INIT from PIO B RDY, AUTOLF / SELECT IN on PIO B bits 6 / 7, BUSY on SIO B CTS (and PIO A STB), ACK and SELECT on SIO B DCD, PAPER END on SIO A CTS, ERROR on PIO B STB; SIO A RTS / DTR enable and turn the data buffer (LPT-WIRING) | DSS `#5F PRINT` (all DSS versions), anything printing through DSS | rare | **no** | **not planned** (§8 item 1) |
| LPT-to-LPT link to a PC | the same port, a "LapLink"-style cable | proposals only (FORUM 7876); the buffer cannot read the data lines without a board change (LPT-WIRING) | none | no | not needed |
| Serial link to a PC | SIO B on the `MOUSE` connector with a null-modem cable, or a SprinterSerial card | forum experiments; SprinterSerial COM1 has a USB bridge | rare | `rs232` slot accepts any serial device | SprinterSerial in the ISA design (I4); SIO B as a COM port: §9 Q4 |

### 5.3 Storage

| Device | What and where | Software | Common today | MAME | unreal-ng |
|---|---|---|---|---|---|
| Floppy 3.5" HD / DD, 5.25" | КР1818ВГ93 (or MB8877A on 2022d) | BIOS, DSS, TR-DOS | some | yes | **done** (S3a) |
| IDE hard disk, two channels | on board | BIOS, DSS | some | yes | **done** (S3b) |
| **CF card on an IDE adapter** | ATA; IDE splitters to reach four units | BIOS-TT `AUTOIDE` ("old CF bug fix", 2025) | common (CF adapters are the usual disk today; the IDE splitter exists to fit them) | as a plain disk | works as a plain disk image; a CF identity check is §8 item 5 |
| ATAPI CD / DVD | IDE, any unit | `CDX.EXE` (data), `CD_PLAY.TRD` (audio, CD on the slave), BIOS-TT ATAPI driver | some | yes, with audio on the primary slave | queued (TODO "Input and device extras"; audio PLAN #83) |
| SD card | no SD socket on any Sprinter board; SD only on the NeoGS and the Wild Sound | NeoGS players | some (via NeoGS) | NeoGS SD (`-hard3`) | NeoGS SD in the ISA design |
| ISA RAM card | ISA memory | Shaos's TIMER | rare | no (ISA memory not implemented) | ISA design I3 |

### 5.4 Sound

| Device | What and where | Software | Common today | MAME | unreal-ng |
|---|---|---|---|---|---|
| AY, beeper, Covox, Covox-Blaster, 16-bit DAC | built in | PT3PLAY, WAVPLAY, games, demos | mass | yes | **done** (S6) |
| Sprinter Sound Card (SSC) | planned in the logic, never built (DOC-RU) | none | none | no | not needed |
| Soundrive | Sp97 configuration only (INFO_011) | Sp97 software | none on Sp2000 | no | not needed |
| General Sound / NeoGS via the ZX-bus adapter (zxbus-mod) | ISA slot | ProPlay, Neo Player Light | some (the owner's setup) | yes | ISA design I2 |
| ESS688 / Sound Blaster Pro | ISA | ESSMIXER | rare | SB 1.0 / 1.5 only | ISA design I6 (deferred) |
| **ISA Wild Sound** (Robus, 2021+; STM32F405, AYX-32 compatible, four AY, SD socket, "AY / Phase / Synthesia / Digital" modes; needs the isa-mod on sp2016 and older boards) | ISA | `DEMOS/WILDSND/prosiak.exe` (XM player) on the owner's disk | rare (a few boards sold, 2022D) | no | **not planned** (§8 item 3) |
| AdLib, Game Blaster, Stereo FX, SSI-2001 | ISA (PC cards) | none for the Sprinter | none | yes (`pc_isa8_cards`) | not needed |
| MIDI (MPU-401, PC MIDI card) | ISA | none for the Sprinter | none | yes | not needed |
| MoonSound via ZX-bus | ISA + ZX-bus adapter | none found on the Sprinter | none | no | ISA design I5 (deferred) |
| CD audio | CD drive analog output to the board | `CD_PLAY.TRD` | some | yes | PLAN #83 |

### 5.5 Video

| Device | What | Common today | MAME | unreal-ng |
|---|---|---|---|---|
| RGB / CGA / TV output, PAL coder | built in; the PAL coder was a Peters option (INFO_014) | mass | n/a | output only, nothing to do |
| VGA / VGA+HDMI scandoubler mods, video-DAC mod (ADV7125), RGB2VGA | output side only (2022D, CARDS) | mass on running boards | n/a | not needed |
| 512 KB video RAM | 2022d fits 512 KB; the logic addresses 256 KB | 2022d boards | no | §9 Q5 |
| Sprinter-FT (FT812 graphics, ISA) | experimental card | prototype | no | ISA design I8 (deferred) |
| ISA PC video cards (CGA, EGA, VGA, MDA, Hercules) | MAME offers them | none (no Sprinter driver) | yes | not needed |

### 5.6 Clock, power, board mods, firmware

| Item | What | unreal-ng |
|---|---|---|
| Dallas DS12887A clock, dallas-mod replacement | on board (a socket on Peters boards) | **done** (`Ds12887`, CMOS file) |
| BIOS flash, updater `UP306.EXE` | the 256 KB flash on board; no separate programmer hardware is used | Deferred (TODO: BIOS flash emulation) |
| 512 KB ROM on 2022d | twice the BIOS size | §9 Q5 |
| Front panel: RESET, TURBO LED, HDD LED, POWER LED; F12 = turbo | `RS`, `TL`, `HL`, `PL` headers (INFO_014) | reset and turbo done; LEDs = S7 status bar (gap D6) |
| ATXMod / ATXMod2 / pwr-sw / sound-mod / vram-mod / simm-72 / isa-mod / ide-splitter | power and signal-quality mods | not needed. The isa-mod fixes the ISA strobe timing on old boards; the emulator models the working bus |
| `Spesial` connector | named in INFO_014; its purpose was not found | none |
| Community logic firmware 2026 (EP1K30 / EP1K50) | Tolik-Trek's MAME fork adds an accelerator control register (codes `#80` / `#81`), a 1 KB accelerator buffer, rectangle mode, X / Y clipping (MAME-TT); BIOS-TT had an `ACEX.SCALE` port in the port table and commented it out on 2026-08-26 | **not planned** (§8 item 2) |
| Verilog Sprinter core | an FPGA re-implementation ([SergeyDudinov/Zx-Sprinter-Core-Verelog](https://github.com/SergeyDudinov/Zx-Sprinter-Core-Verelog)) | a possible extra reference, not a device |

## 6. MAME compared with unreal-ng

| Area | MAME (upstream) | unreal-ng |
|---|---|---|
| Keyboard, serial mouse, Kempston joystick and mouse | yes | done |
| Two Sega pads (Kempston + LPT wiring) | yes | queued (I10) |
| Mouse variants (Logitech, wheel) | yes | queued (I7) |
| Centronics printer | **no** | **no** |
| ATAPI CD + CD audio | yes | queued (CD wiring; PLAN #83) |
| Tape | input from the Spectrum parent, but stuck: bit 6 never follows the tape (research-zx-mode §9.6) | input untested on the Sprinter (S8 Z2) |
| ISA slots | any `pc_isa8_cards` card, I/O only, no interrupts or DMA, no ISA memory | ISA design I0-I8 |
| ZX-bus adapter + NeoGS | yes | ISA design I2 |
| Wild Sound, SprinterJoy, Sprinter-FT, SprinterESP, ESS688 | no | Wild Sound not planned; the others in the ISA design |
| Network (NE1000 / 3C503 among the ISA cards) | NE1000, 3C503 (no Sprinter driver uses them) | network design (NE2000 confirmed) |
| Community 2026 logic firmware features | only in Tolik-Trek's fork | not planned |
| BIOS flash writes | no | Deferred |

Of MAME's 60-odd `pc_isa8_cards`, only these make sense in a Sprinter slot:

- `zxbus_adapter` (+ NeoGS);
- `com` / `comat` (the 16550 UART behind modems and SprinterSerial);
- `ne1000` (network design).

The others have no Sprinter software or driver: the video cards, floppy and hard-disk controllers, AdLib / Game
Blaster / Sound Blaster 1.x / Stereo FX / SSI-2001, MPU-401, `lpt`, the chess and speech cards. MAME offers them
only because the list is generic.

## 7. What the community uses today

From the 2022D thread (stock and orders), the BIOS-TT commit log and the owner's MAME setup, roughly in order:

1. **Boot and storage:** a CF card on an IDE adapter running DSS 1.71, plus a 3.5" floppy.
2. **Input:** a PS/2 keyboard, and a PS/2 mouse through the serial converter.
3. **Picture:** a VGA / HDMI scandoubler mod.
4. **Sound and network cards:** the NeoGS on the ZX-bus adapter (MOD and MP3 playback) and the Wi-Fi card
   (SprinterESP).
5. **Rarer:** Sega pads (test programs on the system disk), the Wild Sound card, ISA serial cards, a CD-ROM.
6. **Practically unused:** the printer port, tape, Sp97-era modems.

What this means for us:

- Items 1-3 are done, or work without emulation (CF behaves as an IDE disk image).
- Item 4 is designed (ISA design, network design).
- The gaps that remain are the printer and the Wild Sound.

## 8. Not yet covered by us, by priority

Only items with no design or queued task anywhere. Everything else in §5 is done, queued, or in the ISA,
network or ZX-mode designs. Priorities re-ranked on 2026-10-02 by developer interest (§10).

| # | Item | Why | Size | Priority |
|---|---|---|---|---|
| 1 | **Centronics printer port** with a "print to file" printer. Wire PIO A data and the RDY strobe, PIO B bits 6 / 7 and the SIO A / B status lines (LPT-WIRING), and add a printer device that writes the bytes to a host file with BUSY / ACK handshaking. The connector is shared with the LPT Sega pad (I10), so it is one slot with three possible devices: printer, pad or nothing (PLAN #82) | the only built-in port nobody emulates; DSS `#5F` drives it; also useful for automation (capture printed text) | S-M | **P4** (was P3; lowered by §10: no developer activity) |
| 2 | **Community logic firmware 2026 features**: identify them in the 2026-09-24 bitstream (`k30.acx` / `k50.acx`): accelerator control register `#80` / `#81`, 1 KB buffer, rectangle mode, clipping, scale port. If they are present, add them as an option of the Standard configuration module | the newest BIOS and software may come to rely on them; MAME upstream lacks them too | S (research), M (build) | **P2** (research), then by finding. §10: check the runtime configuration reload (LDConf) first; the tmkonf extension waits for a released bitstream and a program |
| 3 | **ISA Wild Sound** card. First get the protocol: its ISA ports, the AYX-32 register map, the SD and XM player commands. Then decide whether to build it | the demo `prosiak.exe` on the owner's disk needs it; the card is sold today | L (firmware behavior to reproduce) | **P4** (research P3) |
| 4 | Correct the ISA research: its §7.1 row "Wild Sound XM player ... card identity is a guess" names the ISA Wild Sound (2022D item 11) | one-line documentation fix | S | P2 (with the next ISA design edit) |
| 5 | **CF identity check**: boot DSS 1.71 from a disk that identifies itself as a CompactFlash card (CFA signature `#848A`, no IORDY, 8-bit transfer feature) and check that BIOS-TT `AUTOIDE` handles it | CF is the usual disk on running boards; the BIOS has CF-specific code | S | **P2** (was P3; raised by §10) |
| 6 | **SIO B as a COM port**: the `MOUSE` connector holds either the mouse or a serial peer (shared `ComPort`: TCP, pty) | a PC link without an ISA card; nothing found uses it yet | S | P4 (on demand) |
| 7 | **sp2000-light board option**: no ISA slots, no second IDE channel (a board profile, not a new machine) | faithful to a real variant; an empty slot already reads `#FF` | S | P4 |
| 8 | 512 KB video RAM / 512 KB ROM of the 2022d board | only if a program or bitstream uses them | S-M | P4 (on evidence) |

## 9. Open questions

Each has a recommendation; none blocks current work.

- **Q1. The printer device: a text file, a raw byte file, or both?**
  - Recommendation: a raw byte stream to a host file (`[SPRINTER] PrinterFile=`), which every automation surface
    can also read back.
  - A rendered "page" printer (ESC/P emulation) is not worth it.
  - Do it after PLAN #82 (buses and slots), so the Centronics connector is a slot from the start.
- **Q2. The community logic firmware: do we follow it, and which build is the reference?**
  - Recommendation: yes, as an option of the Standard configuration, keyed to the bitstream it ships in.
  - Ask Tolik-Trek (Telegram) which bitstream carries the "tmkonf" accelerator. Test against his MAME fork,
    because upstream MAME does not have it.
- **Q3. Wild Sound: is it worth an emulation?**
  - Recommendation: research only, for now. Ask Robus (the card's author) for the ISA port map and the firmware
    command list. Decide after that, based on how much software uses it; today we know of one player.
- **Q4. SIO B as a serial port to a PC, instead of the mouse?**
  - Recommendation: build it only when a program needs it. It is cheap with the shared `ComPort`. Until then the
    SprinterSerial card in the ISA design covers PC links.
- **Q5. The 512 KB video RAM and ROM of the 2022d board?**
  - Recommendation: ignore until a program or bitstream is found that uses the second half.
  - Do not add a 2022d machine variant: its other changes (power, connectors, mods) are invisible to software.
- **Q6. The sp2000-light board?**
  - Recommendation: no separate machine. If it is ever wanted, make it a board profile in the slot configuration
    (no ISA slots, one IDE channel).

## 10. Developer interest (2026-10-02)

Sections 1-9 ask what people **own and use**. This section asks a different question: what do the people who
**write software and build hardware for the Sprinter today** work on? A device that developers target now will
gain programs; a device nobody touches will not. We measured this instead of guessing:

- **Repositories:** commit counts per repository from the zxgit.org (Gitea), GitLab and GitHub APIs, over the
  last 12 and 24 months (windows ending 2026-10-02), with the commit messages read for the hardware they touch.
- **New programs, 2020-2026:** for each program, the hardware it needs or can use, from its README, its strings
  or its `file_id.diz`.
- **Forum threads:** the date of the last post and the number of posts in the last 24 months.
- **Chats:** whatever their public pages reveal.

The raw data (commit tables, commit messages, forum indexes, READMEs) is kept with the other downloaded materials,
in the owner's local collection (`peripherals/developer-interest/` there).

### 10.1 Who is active

Four people account for almost all Sprinter development in 2024-2026:

| Developer | Works on | Activity (commits, last 24 months) |
|---|---|---|
| **Tolik-Trek** (Anatoliy Belyanskiy) | the community BIOS, Estex DSS, the system utilities, the Spectrum launcher, the configuration loader | [Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS) 101, [Estex-DSS](https://zxgit.org/Tolik-Trek/Estex-DSS) 101, [Shared_Includes](https://zxgit.org/Tolik-Trek/Shared_Includes) 105, [Spectrum.EXE](https://zxgit.org/Tolik-Trek/Spectrum.EXE) 23, [CDX](https://zxgit.org/Tolik-Trek/CDX) 13, [LDConf](https://zxgit.org/Tolik-Trek/LDConf) 5. Last commit 2026-09-27. He also publishes an editor extension for AHDL, the language of the Sprinter's logic firmware ([AHDL_VSCode](https://github.com/Tolik-Trek/AHDL_VSCode), 2026-05) |
| **witchcraft2001** (Dmitry Mikhalchenkov) | network kits for three ISA cards, a shared network library, a Gopher browser, C and assembler SDKs, FUZIX for the Sprinter, his own MAME fork with the cards | about 1 040 commits in the last 12 months (all in 2026) across 29 repositories, MAME and FUZIX not counted; for example [sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi) 121, [sprinter-rtl8019a](https://github.com/witchcraft2001/sprinter-rtl8019a) 104, [sprinter-3C509B](https://github.com/witchcraft2001/sprinter-3C509B) 38, [sprinter_gopher_browser](https://github.com/witchcraft2001/sprinter_gopher_browser) 34, [sdcc-sprinter-sdk](https://github.com/witchcraft2001/sdcc-sprinter-sdk) 60, [sprinter_evo_sdk](https://github.com/witchcraft2001/sprinter_evo_sdk) 57, [modplay](https://github.com/witchcraft2001/modplay) 47. Last commit 2026-10-02 |
| **Andrei Holub** (maintainer of MAME's `sprinter` driver) | MAME: CD audio, two joysticks, configuration detection, the accelerator extensions | about 11 merged Sprinter pull requests since 2024-10, for example [#12878](https://github.com/mamedev/mame/pull/12878) (2 joysticks), [#12908](https://github.com/mamedev/mame/pull/12908) (Game configuration detected), [#13907](https://github.com/mamedev/mame/pull/13907) (CD audio) |
| **RomanRom2** (Roman Krupnin) | the sp2022d board and its mods; publishes board files | [GeneralSound](https://zxgit.org/RomanRom2/GeneralSound) and [zxbus-extension](https://zxgit.org/RomanRom2/zxbus-extension) (3 commits each, 2025-02); nothing on the Sprinter since |

Everyone else is quiet. romych's card repositories ([SprinterESP](https://zxgit.org/romych/SprinterESP),
[SprinterJoy](https://zxgit.org/romych/SprinterJoy), [Sprinter-FT](https://zxgit.org/romych/Sprinter-FT),
[ESPKit](https://zxgit.org/romych/ESPKit)) have no commit in the last 12 months. The last ones are from 2024-07,
2024-02, 2025-02 and 2025-05. [gitlab.com/sprinter-computer](https://gitlab.com/sprinter-computer) has had no
commit since 2023-02. kostya261 (Konstantin Kosarev) released the
[Sprinter Video Player](https://zxgit.org/kostya261/vplayer_for_sprinter) (2026-07/08).

Two developers also wrote Sprinter emulators in 2026: witchcraft2001's
[spemulator](https://github.com/witchcraft2001/spemulator) (2026-04) and Shaos's SprintEm
([nedopc topic 8172](http://www.nedopc.org/forum/viewtopic.php?t=8172), 9 posts in the last 12 months). Both are
quiet now. Everyone else develops in MAME: the network kits, MODPLAY and FUZIX all have a "run in MAME" build
target or MAME notes.

### 10.2 What the commits are about

**Tolik-Trek's BIOS and DSS (2024-10 to 2026-09)** are mostly storage work:

- **ATAPI:** media-change detection, which took 12 commits from 2025-01 to 2025-04. Also: boot from ATAPI
  (2025-02-15), eject (3.06 Hotfix 1, 2025-07-04), a drive-name function for ATA / ATAPI (2026-04-28), and a
  "ZIP" first-access fix, probably an ATAPI ZIP drive (2025-03).
- **CompactFlash:** a CF fix in ATA / ATAPI reads and writes (2024-12-26) and the `AUTOIDE` "old CF bug fix"
  (2025-05-20).
- **Floppy:** density switching for slow 5.25" drives (3.06 Hotfix 2, 2026-01-19), and the floppy driver
  rewritten for a separate buffer per drive (2026-01 to 2026-05).
- **DSS:** FAT32 boot fixes, "big directory" support, and a `beta_cdfs` branch, which is a CD file system
  ([Estex-DSS beta_cdfs](https://zxgit.org/Tolik-Trek/Estex-DSS/src/branch/beta_cdfs), last commit 2026-07-18).
  The [CDX](https://zxgit.org/Tolik-Trek/CDX) CD utility was rewritten in 2025.
- **Keyboard:** Russian-key and Num Lock fixes in DSS (2026-06). His MAME branch also fixes the keyboard
  controller ROM ([sp_mame Vibe](https://github.com/Tolik-Trek/sp_mame/tree/Vibe), 2026-06-19).
- **Logic firmware:**
  - the bitstream binaries were added to the BIOS repository (2026-06-26);
  - the BIOS now reads the `ALL MODE` port (2026-09-24);
  - LDConf restores `ALL MODE` and the screen position after it loads a new configuration (2026-09-27).
  - Two removals point the other way. The scale port `ACEX.SCALE` was **removed** from the BIOS start-up
    (2026-08-26), and the Spectrum launcher **dropped its use of the accelerator** ("убрал аксель из кода",
    2026-08-26).

**The "tmkonf" accelerator extension** is a correction to §5.6 and §8 item 2. Andrei Holub wrote it in MAME
([commit 7ea867099](https://zxgit.org/Tolik-Trek/MAME/commit/7ea867099), 2026-03-05), not Tolik-Trek. Tolik-Trek's
fork only carries it. It adds:

- a rectangle mode;
- X / Y clipping;
- codes `#80` / `#81`;
- a 1 KB buffer.

The name most likely means "TmK's configuration": TmK is deMarche's coder, who talks about Sprinter accelerator
timings on the forum ([nedopc topic 20230](http://www.nedopc.org/forum/viewtopic.php?t=20230), 2025-06). The
extension is not in upstream MAME, and no released program we found uses it.

**witchcraft2001's work (2026)** is mostly networking:

- three network kits, one per card, all with the same tools: `IFUP` (with DHCP), `PING`, `NSLOOKUP`, `NTP`,
  `TFTP`, `WGET`, `FTP` and `TELNET` (with Zmodem);
- one shared library interface (UNET, [unet_libs_core](https://github.com/witchcraft2001/unet_libs_core)) with
  assembler and C bindings;
- a Gopher browser and a [weather client](https://github.com/witchcraft2001/sprinter-weather-forecast) built on it;
- MAME models of all three cards on [his fork](https://github.com/witchcraft2001/mame_sprinter/tree/sprinter-isa8-rtl8019a)
  (SprinterESP, RTL8019AS, 3C509B, plus an ISA COM card);
- an upstream MAME pull request for the RTL8019AS ([#16206](https://github.com/mamedev/mame/pull/16206), opened
  2026-09-20, still open).

His other work runs on the board's own hardware: [MODPLAY](https://github.com/witchcraft2001/modplay) (Covox-Blaster,
21 MHz turbo), [GIF viewer](https://github.com/witchcraft2001/sprinter_gifview), the EvoSDK port (games written for
ZX-Evo build unchanged for the Sprinter), [checkdsk](https://github.com/witchcraft2001/checkdsk) (disk checker), and
[FUZIX](https://github.com/witchcraft2001/FUZIX/tree/sprinter-support) (a Unix-like OS: IDE, IM2, user space;
interactive shell reached on 2026-10-02).

### 10.3 New programs, 2020-2026, and the hardware they use

| Program | Year | Author | Needs | Can use |
|---|---|---|---|---|
| dontBlink (demo, 1st at Multimatograf 2025, [pouet](https://www.pouet.net/prod.php?which=104117)) | 2025 | deMarche (TmK) | 320x256x256 mode, accelerator, Covox-Blaster stereo streamed from disk; **BIOS ≥ 3.06 beta 9, DSS ≥ 1.70.998** (its own error strings) | - |
| Bad Apple for Sprinter ([pouet](https://www.pouet.net/prod.php?which=106033)) | 2026-04 | - | base board, disk streaming | - |
| Sprinter Video Player (SVP) | 2026-07 | kostya261 | modes `#81` / `#82`, Covox-Blaster | - |
| MODPLAY | 2026 (in progress) | witchcraft2001 | Covox-Blaster stereo, 21 MHz turbo | - |
| GIF viewer, EvoSDK games (Robo, XNX, Innsmouth), SDCC SDK | 2026 | witchcraft2001 | base board | - |
| Network kits (RTL8019AS 0.3.x, ESP 0.3.0, 3C509B 0.1.3), Gopher browser, weather client | 2026 | witchcraft2001 | **an ISA network card**: NE2000-class RTL8019AS, SprinterESP Wi-Fi, or 3Com 3C509B | any of the three through UNET |
| CDX 2025, DSS CD file system (beta) | 2025-2026 | Tolik-Trek | **ATAPI CD** on IDE | CD audio (MAME) |
| BIOS 3.06 / 3.07 beta, DSS 1.71 | 2025-2026 | Tolik-Trek | - | ATAPI boot, CF, 5.25" drives, ZIP |
| FUZIX for the Sprinter | 2026 (in progress) | witchcraft2001 | IDE | - |
| Spectrum.EXE launcher (TAP support 2025-10) | 2024-2026 | Tolik-Trek | - | - |
| LDConf with streams `STREAM.300-305` (MAME-pack `DEMOS/LDCONF`) | 2026 | Tolik-Trek | **runtime reload of a logic configuration** | - |
| TMNT fighters (build 2024-10-16), Xenon 2 (0.02, 2024-12), Nothing Engine (2023) | 2023-2024 | various | base board | - |
| `prosiak.exe` XM player (MAME-pack `DEMOS/WILDSND`) | - | - | ISA Wild Sound | - |

What this shows:

- **No new program from 2020-2026 needs a General Sound / NeoGS, a Sega pad, the SprinterJoy card, the FT812 card,
  the printer port or the 512 KB video RAM.** GS / NeoGS software (ProPlay, Neo Player Light) predates 2020.
- New games, demos and players use **only the board itself**: the 256-color modes, the accelerator, the
  Covox-Blaster, fast disk reads.
- The one family of new programs that needs a card is the **network kits**.

The [app.sprinter.ru](https://app.sprinter.ru) catalog no longer tracks any of this. Its newest DSS is 1.62.92, it
has no DSS 1.70 / 1.71, and its page assets are dated 2021-11.

### 10.4 Discussion

The forums are nearly silent. Discussion has moved to the Telegram group, which cannot be read without an account.

| Place | Most recent | Posts, last 24 months |
|---|---|---|
| nedopc.org Sprinter section ([f=60](http://www.nedopc.org/forum/viewforum.php?f=60)) | "Sprinter on the internet" ([20230](http://www.nedopc.org/forum/viewtopic.php?t=20230)): 2025-11-17, demo news and TmK's accelerator timings; SprintEm ([8172](http://www.nedopc.org/forum/viewtopic.php?t=8172)): 2025-11-17; SprinterNet ([20283](http://www.nedopc.org/forum/viewtopic.php?t=20283), network server project): 2024-10-17 | 6 / 9 / 3 |
| zx-pk.ru Sprinter forum ([121](https://zx-pk.ru/forums/121-sprinter.html)) | "ZX Mode" ([36324](https://zx-pk.ru/threads/36324-zx-mode.html)): 2025-09; extended video modes 352x280 / 368x288 ([33329](https://zx-pk.ru/threads/33329-rasshirennye-videorezhimy-352x280-i-368x288.html)): one post 2026-03-30; the main thread ([10983](https://zx-pk.ru/threads/10983-sprinter-vtoroe-prishestvie.html), 2 013 posts) ended 2021 | a handful |
| zx-pk.com 2022d sale thread ([21431](https://zx-pk.com/forum/viewtopic.php?t=21431)) | 2024-10-28 | few |
| Telegram [t.me/zx_sprinter](https://t.me/zx_sprinter) | 425 members, 80 online (public page, 2026-10-02). It is a group, so the `t.me/s/` web preview shows no messages; tgstat and telemetr refuse scripts (403); no Wayback copy | unknown |
| [Discord](https://discord.gg/x59TnYWkSN) | invite metadata: 75 members, 0 online | dormant |

### 10.5 Ranking

**Score** (0-10) adds three parts:

- **repository activity**, 0-4: commits in the last 24 months that touch the item;
- **new programs** that need it or use it, 0-4;
- **discussion recency**, 0-2.

**Our status** is the state in unreal-ng (done / designed / queued / not planned).

| # | Hardware / feature | Score | Evidence | Our status | Recommendation |
|---|---|---|---|---|---|
| 1 | **ISA network cards**: NE2000-class RTL8019AS, SprinterESP Wi-Fi, 3Com 3C509B | **9** (4+4+1) | about 340 commits in 2026 (three kits, UNET, Gopher, weather); MAME models of all three on a fork plus an upstream pull request (2026-09-20); the only new programs that need a card | **designed** (network design S6c on branch `sprinter-network-design`, SN0-SN6), after ISA I1 | **Raise.** Right after the demo pass, build ISA I1 and then network SN1-SN3, **before** the NeoGS (S6b). Use the kits' own utilities (`IFUP`, `PING`, `WGET`) as acceptance tests and witchcraft2001's MAME fork as the reference |
| 2 | **Board graphics, accelerator, Covox-Blaster** | **9** (3+4+2) | dontBlink 2025, Bad Apple 2026, SVP 2026, MODPLAY, the EvoSDK port; TmK's accelerator timing post 2025-06 | **done** (S2, S5, S6) | Keep the demo pass (TODO "Next" item 2) as the top priority. These are the programs people actually release |
| 3 | **Storage: ATAPI CD (with media change and boot), CompactFlash, 5.25" floppy** | **8** (4+3+1) | most of the BIOS / DSS commits since 2024-10; CDX rewritten 2025; DSS CD file system branch 2026-07; MAME CD audio 2025-07 | IDE and floppy **done**; ATAPI CD **queued**; CF check **P3** | **Raise** the ATAPI CD on `IDE_SPRINTER` to P2, including media change, eject and ATAPI boot (BIOS 3.06+). Raise the CF identity check to P2. Test both with BIOS 3.06 HF2 and DSS 1.71 |
| 4 | **Spectrum mode** (launcher, TAP, ZX ROMs) | 5 (3+1+1) | Spectrum.EXE 23 commits (TAP 2025-10, TRD fix 2026-09); ZX-SP-ROMs 25; zx-pk "ZX Mode" 2025-09 | **designed** (S8, Z1-Z6) | Unchanged |
| 5 | **Logic-firmware changes**: tmkonf accelerator extension, runtime configuration reload (LDConf), `ALL MODE`, the scale port | 5 (3+1+1) | tmkonf in MAME forks 2026-03; LDConf 2026-09; bitstreams in the BIOS repository 2026-06. But the scale port and the launcher's accelerator use were removed 2026-08, and no program uses tmkonf | **not planned** (§8 item 2, P2 research) | **Change the research.** First check that a **runtime configuration reload** works (LDConf with the MAME-pack `STREAM.300-305`, then back to Standard): programs do that today. Postpone tmkonf until a released bitstream and a program use it; the feature set is still changing |
| 6 | PS/2 keyboard (keyboard controller ROM, Num Lock) | 3 (2+0+1) | Tolik-Trek's keyboard ROM fix 2026-06-19; DSS key fixes 2026-06 | **done** (S4) | One check: the arrow keys with Num Lock on, against the fixed ROM |
| 7 | Serial (ISA COM card, SprinterSerial, TELNET with Zmodem over the network kits) | 2 (1+1+0) | ISA COM card in the MAME fork 2026-05 | ISA design I4 / network SN4 | Unchanged |
| 8 | General Sound / NeoGS on the ZX-bus adapter | 1 (1+0+0) | board files only (RomanRom2, 2025-02); no new software since 2020 | ISA design I2, S6b | Keep it for the existing players (ProPlay, MAME-pack disk), but **after** the network cards |
| 9 | Sprinter-FT (FT812) | 1 (1+0+0) | 10 commits 2024-11 to 2025-02, then silence; no software | deferred (ISA I8) | Unchanged (deferred) |
| 10 | Sega pads, SprinterJoy | 1 (1+0+0) | MAME pads 2024-10; SprinterJoy last commit 2024-02; only the old test programs | queued (I10), ISA I7 deferred | Keep queued, **lowest** of the input extras |
| 11 | ISA Wild Sound | 0 | no repository, one undated player | not planned (P4) | Unchanged |
| 12 | Centronics printer | 0 | nothing. The BIOS commits about `LP_PR_LINE_DIR` are screen text output, not the printer | not planned (P3) | **Lower to P4** (on demand) |
| 13 | 512 KB video RAM / ROM (sp2022d), sp2000-light | 0 | nothing | not planned (P4) | Unchanged |

### 10.6 What changes for us

- **Network moves ahead of the NeoGS.** The NE2000 / ESP / 3C509B cards are where the active developer is. A model
  of them in unreal-ng, with TTD and automation, would be the only debugger-grade target for that work besides
  his own MAME fork.
- **ATAPI CD and CF move up.** The BIOS and DSS developer spends most of his time on them, and every running board
  boots from CF.
- **Logic-firmware research changes focus.** Verify the runtime configuration reload first. The tmkonf extension
  waits for evidence.
- **The printer drops to P4.**
- The rest of §8 stays as it is.

These are recommendations; the owner decides the order. They are entered in [TODO.md](TODO.md) ("Next" and
"Peripherals not yet planned") and in [roadmap-and-plan.md](roadmap-and-plan.md) §4.
