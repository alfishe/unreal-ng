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
    mode and clipping, and a scale port. Tolik-Trek's own MAME fork has them; upstream MAME and unreal-ng do not.
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
| Tape input / output | `KMPS` connector, `#FE` bit 6 / bit 3 | Spectrum loaders | rare | input yes | input test queued (I5); output follows the shared tape path |
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
| Tape | input from the Spectrum parent | input untested on the Sprinter (I5 queued) |
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
network or ZX-mode designs.

| # | Item | Why | Size | Priority |
|---|---|---|---|---|
| 1 | **Centronics printer port** with a "print to file" printer. Wire PIO A data and the RDY strobe, PIO B bits 6 / 7 and the SIO A / B status lines (LPT-WIRING), and add a printer device that writes the bytes to a host file with BUSY / ACK handshaking. The connector is shared with the LPT Sega pad (I10), so it is one slot with three possible devices: printer, pad or nothing (PLAN #82) | the only built-in port nobody emulates; DSS `#5F` drives it; also useful for automation (capture printed text) | S-M | **P3** |
| 2 | **Community logic firmware 2026 features**: identify them in the 2026-09-24 bitstream (`k30.acx` / `k50.acx`): accelerator control register `#80` / `#81`, 1 KB buffer, rectangle mode, clipping, scale port. If they are present, add them as an option of the Standard configuration module | the newest BIOS and software may come to rely on them; MAME upstream lacks them too | S (research), M (build) | **P2** (research), then by finding |
| 3 | **ISA Wild Sound** card. First get the protocol: its ISA ports, the AYX-32 register map, the SD and XM player commands. Then decide whether to build it | the demo `prosiak.exe` on the owner's disk needs it; the card is sold today | L (firmware behavior to reproduce) | **P4** (research P3) |
| 4 | Correct the ISA research: its §7.1 row "Wild Sound XM player ... card identity is a guess" names the ISA Wild Sound (2022D item 11) | one-line documentation fix | S | P2 (with the next ISA design edit) |
| 5 | **CF identity check**: boot DSS 1.71 from a disk that identifies itself as a CompactFlash card (CFA signature `#848A`, no IORDY, 8-bit transfer feature) and check that BIOS-TT `AUTOIDE` handles it | CF is the usual disk on running boards; the BIOS has CF-specific code | S | P3 |
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
