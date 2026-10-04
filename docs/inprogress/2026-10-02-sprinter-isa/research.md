# Sprinter ISA slots: research

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Research for the design in [tdd.md](tdd.md); owner decisions needed: [open-questions.md](open-questions.md) |
| **Machine** | Peters Plus Sprinter Sp2000 (`SPRINTER` model in unreal-ng) |
| **Parent program** | [2026-09-28-sprinter](../2026-09-28-sprinter/README.md), phase **S6b** ("ISA / ZX-bus / NeoGS: PROPLAY MOD playback") |

## Contents

1. [In short](#1-in-short)
2. [Glossary](#2-glossary)
3. [Sources](#3-sources)
4. [How the real Sprinter reaches its ISA slots](#4-how-the-real-sprinter-reaches-its-isa-slots)
5. [Signals on the slots, interrupts, DMA, waits](#5-signals-on-the-slots-interrupts-dma-waits)
6. [The ZX-bus adapter card](#6-the-zx-bus-adapter-card)
7. [Which cards existed and which software uses them](#7-which-cards-existed-and-which-software-uses-them)
8. [What MAME does, and its gaps](#8-what-mame-does-and-its-gaps)
9. [What unreal-ng has today](#9-what-unreal-ng-has-today)
10. [Corrections to earlier Sprinter documents](#10-corrections-to-earlier-sprinter-documents)

## 1. In short

- The Sprinter has **two ISA-8 slots** (the 8-bit PC expansion bus). There is **no I/O-port path** to
  them: a program maps a slot into CPU **window 3** (`#C000-#FFFF`) and reads or writes memory there.
  Each such memory access becomes one ISA cycle, either an ISA **I/O** cycle or an ISA **memory** cycle.
- Four page numbers select what window 3 shows, once port `#1FFD` bit 4 is set: `#D0` slot 1 memory,
  `#D2` slot 2 memory, `#D4` slot 1 I/O, `#D6` slot 2 I/O. Port `#9FBD` adds ISA address bits A19-A14
  and drives the slot lines AEN and RESET.
- **General Sound reaches the Sprinter only through an ISA card, the "ZX-bus adapter"**, which turns ISA
  I/O cycles into Spectrum port cycles. With it, GS port `#BB` is CPU address `#C0BB`. MAME models this
  (adapter in slot 1, NeoGS on it), and the owner's MAME setup uses exactly that.
- Software that uses ISA, found on the MAME-pack disks and in the community sources: **ProPlay** (MOD
  player, GS), **Neo Player Light** (NeoGS), **ESSMIXER** (ESS688 sound card mixer), **ISACHK** (bus
  checker), **ESPT / wterm** (Wi-Fi card: 16550 UART + ESP8266), **BC-Term** (ISA modem: 16550 UART),
  **TIMER** (an ISA RAM card), plus test programs for three community cards (Sega pads, FT812 video,
  dual RS-232).
- unreal-ng already has every chip the first cards need: the GS / NeoGS card with its own Z80, the 16550
  UART and the ESP AT module of the network work, ymfm's OPL3 for a later Sound Blaster, eve-emu's FT812.
  What is missing is the bus: today window 3 on `#D0-#D6` reads `#FF` and drops writes.

## 2. Glossary

| Term | Meaning |
|---|---|
| **ISA, ISA-8** | The expansion bus of the IBM PC / XT: a slot connector with 8 data lines, 20 address lines and separate strobes for memory and I/O. "ISA-8" is the 8-bit version (the 16-bit AT version adds a second connector) |
| **Slot, card** | A slot is the connector on the main board; a card is the board plugged into it |
| **ISA I/O cycle / memory cycle** | An ISA card sees either an I/O cycle (strobes `/IOR`, `/IOW`, 10-16 address bits used) or a memory cycle (`/MEMR`, `/MEMW`, 20 address bits). On a PC the CPU's `IN`/`OUT` make I/O cycles; on the Sprinter both kinds come from memory accesses in window 3 |
| **Window 3** | The top quarter of the Z80's 64 KB, `#C000-#FFFF`. The Sprinter maps 16 KB "pages" into each of four windows; page numbers `#D0-#DF` mean "ISA" when port `#1FFD` bit 4 is set |
| **AEN** | "Address enable" ISA line. A PC raises it during DMA so that cards ignore the address; on the Sprinter software sets it with `#9FBD` bit 6 |
| **IOCHRDY** | ISA line a slow card pulls low to stretch the cycle; on the Sprinter it reaches the CPU `/WAIT` input |
| **IRQ** | Interrupt request line from a card |
| **DRQ / DACK, DMA** | A card asks for a direct memory transfer with DRQ; the DMA controller answers with DACK and moves the data. The Sprinter has no DMA controller (§5) |
| **Wait state** | An extra CPU clock inserted into a memory or port access so that slow hardware can answer |
| **PLD** | The Sprinter's programmable logic chip (Altera ACEX), which implements memory paging, the port decoder, video and the ISA select |
| **Port decoder table (DCP)** | Page `#40` of RAM: for each port access the PLD reads a "code" byte there that names the device. Codes are written `#1B`, `#C0`, ... |
| **ZX-bus** | The edge connector of ZX Spectrum clones (Pentagon, Scorpion, ATM, ZX-Evo) on which cards such as the General Sound sit. It carries the Z80 signals: `/IORQ`, `/RD`, `/WR`, A15-A0, D7-D0, `/RESET`, `/INT` |
| **ZX-bus adapter** | An ISA card with a ZX-bus connector on it: it converts ISA cycles into Z80-style cycles so a Spectrum card works on the Sprinter |
| **General Sound (GS)** | A Spectrum sound card with its own Z80 (12 MHz), 4 sample channels and a mailbox on host ports `#B3` (data), `#BB` (command / status), `#33` (control). The host sends commands; the card's firmware plays MOD music or sound effects on its own |
| **NeoGS** | A newer GS-compatible card: faster Z80, more RAM, an SD card and an MP3 decoder chip |
| **MOD** | The Amiga "module" music format: samples plus a pattern list. GS firmware plays MOD files natively |
| **TTD** | unreal-ng's time-travel debugger: it records the machine and can replay or rewind it exactly. Each device saves its state as a numbered **blob** |
| **Mirror** | A device that decodes only some address lines answers at several addresses; e.g. GS decodes only A7-A0, so `#00BB`, `#01BB`, ... `#FFBB` all reach it |

## 3. Sources

Short names as in the Sprinter program's [materials.md](../2026-09-28-sprinter/materials.md); new ones
marked *new*.

| Short name | What | Upstream |
|---|---|---|
| **MAN** | Ivan Mak, designer's programming manual, §1.1, §8, §9.3, §13.1 | [SaymanNsk/Sprinter200x](https://github.com/SaymanNsk/Sprinter200x) `docs/sp2000_man.pdf` |
| **INC** | `constants/SP2000.inc` (ISA notes at `:566-601`) | [Tolik-Trek/Shared_Includes](https://zxgit.org/Tolik-Trek/Shared_Includes) |
| **PLD** | ACEX AHDL source `ACEX/SP2_ACEX.TDF` (`:400-412`, `:565-620`, `:675-691`) | [gitlab.com/sprinter-computer/hard](https://gitlab.com/sprinter-computer/hard) |
| **SCH** *new* | Peters Plus Sp2000 schematic, version 1.62 (13.02.2003): decoder DD7, latch DD6, slots J6 / J7 | [petersplus.com `sp2k_sch.pdf` (archive)](https://web.archive.org/web/20031117033312/http://www.petersplus.com/download/sp2k_sch.pdf) |
| **INFO** *new* | `DOCS/INFO_012.TXT`, `INFO_014.TXT` on the system disk: Peters-era port and slot tables, option list | MAME-pack system disk (`sp_hdd_sys`) |
| **DOC-RU** | doc.sprinter.ru page "ISA interrupts" (`blocks/z84c15/isa-interrupts`) | [doc.sprinter.ru](https://doc.sprinter.ru/) |
| **MAME** | `src/mame/sinclair/sprinter.cpp`, `src/devices/bus/isa/zxbus_adapter.cpp`, `src/devices/bus/spectrum/zxbus/{bus,neogs}.cpp` at `f43983b6` | [mamedev/mame](https://github.com/mamedev/mame) |
| **ESPKIT** *new* | romych's ESP library `sources/DSS/isa.asm`, `esplib.asm` | [zxgit.org/romych/ESPKit](https://zxgit.org/romych/ESPKit) |
| **CARDS** *new* | romych's ISA-8 card projects: [Sprinter-FT](https://zxgit.org/romych/Sprinter-FT) (FT812 video), [SprinterESP](https://zxgit.org/romych/SprinterESP) (Wi-Fi), [SprinterJoy](https://zxgit.org/romych/SprinterJoy) (Sega pads), [SprinterSerial](https://zxgit.org/romych/SprinterSerial) (RS-232) | as linked |
| **APPS** *new* | Sprinter application sources incl. Shaos's `Timer/TIMER.ASZ` (ISA RAM) | [gitlab.com/sprinter-computer/apps](https://gitlab.com/sprinter-computer/apps) |
| **DISK** | The owner's MAME-pack hard disks: `sp_hdd_sys` (DSS 1.71, 1 261 files), `sp_hdd_media` (MOD, WAV, pictures) | owner's MAME pack, kept outside the repository |

## 4. How the real Sprinter reaches its ISA slots

### 4.1 The sequence every program uses

| Step | Instruction | Why |
|---|---|---|
| 1 | `IN A,(#E2)` | save the page now in window 3 (port `#E2` = window-3 page, code `#F0`) |
| 2 | `#1FFD` <- `#11` | bit 4 (Scorpion "extended page") makes pages `#D0-#DF` mean ISA |
| 3 | `OUT (#E2),#D4` (or `#D6`, `#D0`, `#D2`) | choose slot and space |
| 4 | `#9FBD` <- `A19-A14` (+ AEN, RESET) | upper ISA address bits |
| 5 | memory reads / writes in `#C000-#FFFF` | one ISA cycle each |
| 6 | `#1FFD` <- `#01`, restore `#E2` | back to RAM |

Sources: INC `:566-601` ("1) send 10h to 1FFDh; 2) send control byte to port 0E2h ..."); the same code
in ProPlay (`#8108`, [disassembly](../../disasm/software/sprinter/proplay/README.md)), ESSMIXER, ISACHK,
ESPKIT `isa.asm` (`ISA_OPEN`), BC-Term, Shaos's TIMER.

### 4.2 The page byte

| Page | Bit 2 | Bit 1 | Window 3 shows |
|---|---|---|---|
| `#D0` | 0 | 0 | slot 1, ISA **memory** |
| `#D2` | 0 | 1 | slot 2, ISA memory |
| `#D4` | 1 | 0 | slot 1, ISA **I/O** |
| `#D6` | 1 | 1 | slot 2, ISA I/O |

The PLD selects ISA when the latched window-3 page matches `1101 xxxx` and the access is in window 3
(PLD `:579`: `PRE_ISA = ... ISA_PORT[] == B"1101XXXX" ... A14 ... A15`); MAME's test is `(page & #F9) ==
#D0` (`sprinter.cpp:368-379`), i.e. only `#D0/#D2/#D4/#D6` (bits 0, 3 clear). Which pages `#D1`, `#D8`
... do on the real board is not checked; nothing uses them.

**Which bit is which was disputed** and is now settled by the schematic:

- MAN §8 and INFO_012 swap the bits (MAN: "bit 1 = port or memory, bit 2 = slot"; INFO_012: "`#D0`
  ports ISA1, `#D2` ports ISA2, `#D4` memory ISA1, `#D6` memory ISA2").
- INC `:596-602` says "fixed bug with D2 and D1 bits (functional exchange, but not documented)": bit 2 =
  memory / I/O, bit 1 = slot.
- **SCH**: decoder DD7 (74ALS138) takes RA14 (A0 input), `/WR` (A1), RA15 (A2), is enabled by RA17, `/RA16`,
  `/CS_CASH`, and its outputs are `/MEMW1, /MEMW2, /MEMR1, /MEMR2, /IOWR1, /IOWR2, /IORD1, /IORD2`: RA14 =
  slot, RA15 = I/O. PLD `:684-688` drives `ISA_A[3..2] = (!PAGE5, PAGE5)` (RA17, RA16) and `ISA_A[1..0] =
  (PAGE2, PAGE1)` (RA15, RA14): **page bit 2 = I/O, bit 1 = slot**.
- MAME `sprinter.cpp:1246-1276` and every program found agree with the schematic (ProPlay writes `#D4`
  for slot 1 I/O; Shaos's TIMER writes `#D0` / `#D2` for ISA memory; ESPKIT comments "D4 - ISA1, D6 - ISA2").

### 4.3 The address

```
ISA address (20 bits) = (#9FBD bits 5-0) << 14  |  CPU A13-A0
```

Port `#9FBD` (port-table code `#1B`, write-only) is a 74HC374 latch (DD6 in SCH), clocked by the PLD on
code `#1B` (PLD `:571`). Its outputs: bits 0-5 = BA14-BA19, bit 6 = **AEN**, bit 7 = **ISA RESET** (1 =
cards held in reset). The latch has no reset input: after power-on it holds an undefined value until
software writes it (MAME and unreal-ng start at 0). The BIOS never writes it (no reference in the BIOS-TT
or BIOS-PP sources).

Worked examples:

| Wanted | `#9FBD` | Page | CPU address |
|---|---|---|---|
| GS status, ISA I/O `#000BB`, slot 1 | `#00` | `#D4` | `#C0BB` |
| 16550 UART at COM3 base `#3E8`, slot 2 | `#00` | `#D6` | `#C3E8` |
| Sound Blaster mixer `#224`, slot 1 | `#00` | `#D4` | `#C224` |
| ISA memory `#DC000-#DFFFF` (16 KB), slot 1 | `#37` (`#DC000 >> 14`) | `#D0` | `#C000-#FFFF` |

ISA I/O addresses on a PC use 10 bits (`#000-#3FF`); here CPU A13-A0 gives 14 bits and `#9FBD` six more,
so a card that decodes 10 bits answers at many CPU addresses (mirrors), e.g. GS at `#C0BB`, `#C1BB`, ...

### 4.4 No direct port path on the Sp2000

The older Sp97 board listed port-table codes `#30-#33` as "ISA slot 1/2 memory/ports" (MAN §13.1), and
INFO_012 lists `P_XTR xxBx code 32 "XTR-modem and GS port redirected to ISA"`. BIOS 3.04 still maps
`xxxx xxxx 101x xx11` (DOS off: `#A3`, `#AB`, `#B3`, `#BB` ...) to code `#32`
([hardware-reference.md](../2026-09-28-sprinter/hardware-reference.md) §4.4). On the Sp2000, however, DD7
is fed only by RA14-RA17 and `/CS_CASH` (SCH), and the PLD only stops driving the data bus for codes
`#0x` / `#3x` (PLD `:408`); nothing connects code `#32` to a slot. **A plain `IN A,(#BB)` most likely
reads `#FF`** (the data bus pull-ups R48-R59). No Sp2000 program found uses that path. MAME has no such
path either. *(Inference from the schematic and the PLD; a real-board check would settle it.)*

### 4.5 Refresh and the accelerator

The PLD gates the ISA select with `/RF` (PLD `:599`, `CS_ISA = DFF((!/RF or PRE_ISA) ...)`): Z80 refresh
cycles never start an ISA cycle. The graphics accelerator does not reach ISA windows (unreal-ng's
`SprinterMemory::AcceleratorReaches` already excludes them).

## 5. Signals on the slots, interrupts, DMA, waits

| Signal | On the Sprinter | Source |
|---|---|---|
| D7-D0 | through a '245 buffer, enabled by the ISA select, direction from `/RD` | SCH (U5) |
| A13-A0 | CPU A13-A0 through '244 buffers, always enabled | SCH (U8, U11) |
| A19-A14 | `#9FBD` bits 5-0 | SCH (DD6), INC |
| `/MEMR`, `/MEMW`, `/IOR`, `/IOW` | from DD7, per slot (each slot has its own four strobes) | SCH |
| AEN | `#9FBD` bit 6, set by software | SCH, INC |
| RESET DRV | `#9FBD` bit 7, set by software (1 = reset) | SCH, INC, ESPKIT `ISA_RESET` (`#C0`, 1 ms, `#00`) |
| BALE | buffered `/MREQ` | INFO_012 |
| CLK (B20) | the CPU clock through 300 Ω: 3.5 MHz, or **21 MHz in turbo** (above the ISA standard's 8.33 MHz) | SCH, INFO_012 |
| OSC 14.318 MHz (B30), TC, `/0WS` | absent | INFO_012 |
| IOCHRDY | 3.9 kΩ pull-up, to the CPU `/WAIT` through a diode: a card can stretch the cycle | SCH (VD9), INFO_012 |
| IRQ2-IRQ7 | all IRQ pins of a slot tied together: **one interrupt line per slot** | SCH, INFO_012 |
| DRQ1-3, DACK1-3 | tied per slot: one DRQ and one DACK per slot | SCH |

**Interrupts and DMA go through the CPU's PIO port B** (Z84C15 on-chip PIO, port `#1E` data, `#1F` control):
bit 0 = IRQ slot 1, bit 1 = IRQ slot 2, bit 2 = DRQ slot 2 (in), bit 3 = DACK slot 2 (out), bit 4 = DRQ
slot 1 (in), bit 5 = DACK slot 1 (out), bits 6-7 printer (DOC-RU "ISA interrupts"; MAN §9.3). The PIO can
raise a Z80 mode-2 interrupt on an input change (bit-control mode), so a card's IRQ can interrupt the CPU
through the Z84C15 daisy chain. **There is no DMA controller**: a program would move DMA data itself.
*(Checked on the schematic for phase I4: the six nets IRQ1 / IRQ2 / DRQ1 / DRQ2 / DACK1 / DACK2 have 3.9 kOhm pull-ups,
R165-R170, and go straight to PB0-PB5 with no inverter; ISA IRQs are active high, so an undriven pin reads 1. IEI is
tied high and IEO is not connected: the PLD's `/INT` is not part of the daisy chain. BC-Term programs `#00` vector,
`#CF`, `#01` / `#02` direction, `#B7` (enabled, OR, active high, mask follows), `#FE` / `#FD` mask, `#83`.)*
*(Inference: with AEN and DACK under software control, a program can make a DMA-style cycle by setting
AEN = 1 and DACK = 0 and then reading or writing window 3; nothing found does this.)*

**Wait states.** PLD `:587-596`: when an ISA (or ROM) select is active, a 3-bit counter on the 42 MHz
clock is loaded with 4 and holds `WAIT` until it counts down: about 4-5 periods of 42 MHz, roughly
**100-120 ns, i.e. 2-3 CPU clocks at 21 MHz** and nothing at 3.5 MHz (one clock is 286 ns there).
*(Reading of the AHDL; not measured.)* A card can add more through IOCHRDY. MAME charges an ISA access
like a RAM access in turbo (`do_mem_wait(3)`: align to a 6-clock slot, `sprinter.cpp:1720-1731`,
`:1251`), and has no IOCHRDY. A turbo ISA cycle is therefore far shorter than a PC's (an 8 MHz ISA I/O
cycle is about 500 ns or more): slow cards must use IOCHRDY or fail at 21 MHz. This is a property of the
real machine, not an emulation choice.

## 6. The ZX-bus adapter card

| Fact | Source |
|---|---|
| Peters Plus offered "ISA -> Spectrum-BUS adapter" as option 9 | INFO_014 |
| Users asked for GS; "Sprinter cannot take a GS" was a period complaint | DISK `DOCS/IM2.TXT:1269`; [zxpress article "Спринтер: для чего?"](https://zxpress.ru/article.php?id=11761) |
| No schematic or port document of the adapter was found | collection-wide search (`zxbus`, `adapter`, `Spectrum-bus`) |
| MAME: `ISA8_ZXBUS` installs the **whole ISA I/O space** `#0000-#FFFF` as ZX-bus I/O, one ZX-bus slot on it, no memory, IRQ, NMI or WAIT passed; "FIXME: determine ZXBUS clock" | MAME `zxbus_adapter.cpp:13-38` |
| MAME's ZX-bus cards: `nemoide`, `neogs` (and `smuc` for GMX) | MAME `bus.cpp:84-93` |
| NeoGS host ports in MAME: `#xxBB` status / command, `#xxB3` data, `#xx33` control (A7-A0 decoded) | MAME `neogs.cpp:440-445` |
| ProPlay finds the GS by reading ISA I/O `#00BB` in slot 1 then slot 2 | [ProPlay](../../disasm/software/sprinter/proplay/README.md) `#8204` |

So for the design the adapter is **transparent**: ISA I/O address A15-A0 = ZX port, ISA `/IOR` = ZX `IN`,
`/IOW` = ZX `OUT`. Unknown (no schematic): whether it passes ISA RESET to the ZX-bus `/RESET` (then
`#9FBD` bit 7 resets the GS), whether it maps ISA memory cycles to anything (ZX `/MREQ` cards such as the
NeoGS "ZX-DMA" or a ZXNETUSB memory window), and whether the GS `/INT` or `/WAIT` reach the ISA IRQ /
IOCHRDY. The [open questions](open-questions.md) carry the recommended defaults.

**Timing through the adapter.** A GS mailbox access is a latch: the card stores a host write at once and
its own Z80 picks it up later. No wait is needed, which fits the Sprinter's short turbo ISA cycle. GS
software polls the status register (bit 0 = command busy, bit 7 = data busy), so host speed only changes
how many polls happen, not the result.

## 7. Which cards existed and which software uses them

### 7.1 Software that drives ISA cards

Found on the MAME-pack disks (DISK) and in the community sources (DSS EXE files are named as they appear
on the disk).

| Program | File | Card | Ports / protocol | Evidence |
|---|---|---|---|---|
| **ProPlay 0.5.91** (Sayman, 2023), "General Sound MOD player" | `BIN/PROPLAY.EXE` (1 126 B, CRC `2d06da86`), also on the DSS 1.71 floppy | GS / NeoGS on the ZX-bus adapter, slot 1 or 2 | page `#D4`/`#D6`, `#9FBD` = 0; commands at `#C0BB`, data at `#C0B3`; `#F3` warm restart, `#30` + `#D1` load module / open stream, bytes, `#D2` close, `#31` play | [disassembly](../../disasm/software/sprinter/proplay/README.md) |
| **Neo Player Light 0.58** | `BIN/npl.exe` (6 470 B, packed) | NeoGS (strings "kbps": MP3, *inference*) | the same open sequence and `LD (#C0B3),A` at file offset `#9C1` | byte scan |
| **ESSMIXER** (Sayman 2020), "ESS688 AUX mixer initializator" | `UTILS/ESSMIXER.EXE` (791 B), also DSS 1.62.92 floppy `BIN/` | ESS ES688 (Sound Blaster Pro compatible), slot 1 | `#9FBD` <- `#C0` (RESET + AEN), delay, `#00`; page `#D4`; SB mixer index / data at `#C224` / `#C225` (ISA `#224/#225`, base `#220`); writes registers `#22`, `#00`, `#28` | disassembly (scratch) |
| **ISACHK 0.1.12** (Sayman 2023), "Sprinter ISA-BUS checker" | `TESTS/ISACHK.EXE` | none ("check without any devices") | dumps ISA I/O `#000-#03F` of both slots | disassembly (scratch) |
| **ESPT**, "Sprinter WiFi ISA-8 Card (ESP-12f) Terminal"; `UTILS/wterm.exe` | `UTILS/ESPT.exe` | SprinterESP: TL16C550 UART + ESP8266 with AT firmware | page `#D4`/`#D6`; UART at ISA `#3E8` (COM3) = `#C3E8`; ISA reset `#C0`, 1 ms, `#00` | ESPKIT `isa.asm`, `esplib.asm:16` |
| **BC-Term 1.11** (Alexey Gavrilov), "support of ISA modem" | `MODEM/bcterm.exe`, `BCT111.TRD` | ISA modem (16550 UART at COM1 `#3F8`) | `#1FFD` <- `#11`, page `#D4`; six references to `#C3F8` | byte scan; `readme.eng` |
| **TIMER** (Shaos, 2021) | APPS `Timer/TIMER.ASZ:48-80` | ISA RAM card | `#9FBD` <- `#37` (`#DC000`), page `#D0` / `#D2`; tests RAM at `#C800`, then **runs code from ISA memory** | source |
| SprinterJoy test | CARDS SprinterJoy `Software/TESTSD.C` | Sega Mega Drive pad card | ISA I/O `#250` = `#C250` | source |
| Sprinter-FT tester | CARDS Sprinter-FT `Tester/isa.asm`, `ftlib.asm` | FT812 video card | the same open sequence, scans both slots | source |
| SprinterSerial | CARDS SprinterSerial `README.md` | PC16552D dual UART | COM1 `#3F8`, COM2 `#2F8` | README |
| Wild Sound XM player ("prosiak") | `DEMOS/WILDSND/prosiak.exe` (packed) | unknown ("Wild Sound ... ISA #" string) | `#9FBD` and page `#D4` fragments at file offsets `#4FB`, `#588` | byte scan; card identity is a guess |

Other byte-pattern hits (TASM, DEMON, demos, CC1/CC2, GFXVIEW, the TITD installer, FN) are false: they
lack the open sequence. No program uses a plain `OUT (#BB)` / `OUT (#B3)`.

### 7.2 Cards by importance for the emulator

| Card | Real? | Software | Chips in unreal-ng already | Priority |
|---|---|---|---|---|
| ZX-bus adapter + **General Sound / NeoGS** | Peters option + Spectrum cards; the owner's MAME setup | ProPlay, Neo Player Light | `GeneralSoundCard` (classic Z80, lightweight, NeoGS) | **1** |
| ISA RAM | generic PC memory card | TIMER (runs code from it) | none needed (a byte array) | 2 (small, exercises the memory side) |
| 16550 UART: SprinterESP Wi-Fi, ISA modem, SprinterSerial | community cards (2022-2024); period modems | ESPT, wterm, BC-Term | `Uart16550`, `ComPort` peers, ESP `AtModule`, virtual network | 3 |
| ESS688 / Sound Blaster Pro | PC card | ESSMIXER (mixer only); maybe "Wild Sound" | ymfm OPL3 (FM); the DSP, mixer and DMA-less sample path are new | 4 |
| SprinterJoy (Sega pads) | community card, "in development" | its test program | joystick manager | 5 |
| Sprinter-FT (FT812 video) | community card, experimental | its tester | eve-emu FT812 (TS-Conf VDAC2 work) | 6 |

The period context: ISA was put on the Sprinter for PC modems ([zxpress Sprinter FAQ](https://zxpress.ru/article.php?id=6486):
"the ISA bus was needed for ISA modems"); sound and network cards came later from the community.

## 8. What MAME does, and its gaps

| Topic | MAME `sprinter.cpp` (`f43983b6`) | Gap |
|---|---|---|
| Buses | two separate `isa8` buses, one slot each, clock `X_SP / 5` = 8.4 MHz ("FIXME: determine ISA bus clock") (`:1973-1979`) | the real slot clock is the CPU clock |
| Cards | any of `pc_isa8_cards`; slot 1 defaults to `zxbus_adapter`, slot 2 empty | - |
| Window | `(page & #F9) == #D0` in window 3 with `#1FFD` bit 4 (`:368-379`); bit 2 = I/O, bit 1 = slot | - |
| ISA memory | reads `#FF`, writes dropped (`:1256`, `:1273`; TODO `:43` "ISA memory slots") | ISA RAM / ROM cards do not work |
| Address | `(#1B latch & #3F) << 14 \| offset` | - |
| AEN, RESET | `#1B` bits 6 / 7 are no-ops (`:736-746`) | ISA reset does not reset cards |
| IRQ, DRQ, IOCHRDY | not wired; the PIO port B inputs see nothing | interrupt-driven cards (UART, SB) cannot interrupt |
| Waits | `do_mem_wait(3)` like RAM (`:1251`) | the PLD's ISA counter is not modeled |
| GS | only through `zxbus_adapter` -> `neogs` (NeoGS at 10 MHz, INT 37.5 kHz; `neogs.cpp:419-423`) | no classic GS; the adapter's own behavior is a guess (transparent) |

The owner's launcher uses `-isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs -hard4 neogs.chd` (MAME
0.277); on MAME 0.289 the NeoGS SD card is `-hard3` when `ata2:0` holds a CD
([mame-gap-analysis.md](../2026-09-28-sprinter/mame-gap-analysis.md) §1). MAME is therefore a usable
reference for ProPlay MOD playback (the S6 sound work compared WAV captures the same way,
`tools/machines/sprinter/mame-capture/` `SPC_WAV`).

## 9. What unreal-ng has today

| Piece | State (master, plus branch `sprinter-s6` not merged yet) | Where |
|---|---|---|
| `#9FBD` (code `#1B`) | keeps A19-A14 in `_pld.isaAddrExt`; AEN and RESET dropped | `core/src/emulator/ports/models/portdecoder_sprinter.cpp` |
| Window 3 ISA view | `BankAction::Isa` / `ReadRedirect::Isa` on pages `#D0/#D2/#D4/#D6` with `#1FFD` bit 4: reads `#FF`, writes dropped; `Redirect()` is `const` (no side effects possible yet) | `core/src/emulator/memory/sprinter/sprintermemory.cpp:111-116`, `:185`, `:270` |
| PIO port B inputs | `#FF` (no IRQ / DRQ) | `core/src/3rdparty/z84c15/z84pio.cpp`; tdd-accel-sound-input §4 |
| GS / NeoGS | built on the Sprinter (`[SOUND] GSType=NGS` in `data/configs/sprinter/unreal.ini`), owned by `SoundManager`, registered on the exact port map (`#00B3/#00BB/#0033`), mixed, TTD-registered, but **unreachable**: the Sprinter decoder never calls `PeripheralPortIn/Out` for them. The card's Z80 runs every frame for nothing | `core/src/emulator/sound/chips/gs/`, `.../neogs/`, `soundmanager.cpp` `attachToPorts` |
| GS clock domain | lazy catch-up on the emulator thread: each host access runs the card to "now" (`GSHostClock`, `AudioTstate` removes the hardware turbo), the rest at frame end | `gshostclock.{h,cpp}`, `gscardrunner.h` |
| Other ZX-bus cards | ZXM-MoonSound and ZXNETUSB attach as **full-decode observers** in the Z80 I/O funnel (`z80.cpp:1225`, `:1330`), i.e. on every machine's native port path; the Sprinter config has `MoonSound=0` and network `Card=NONE` | `portdecoder.h:765-925` |
| "Bus / slot" notion | only `PortDecoder::DescribeNetwork()` (`zxBus = true` by default, the Sprinter does not override it) - "a first step towards machine -> buses / extension slots -> devices" | `portdecoder.h:795-833` |
| TTD id | `PeripheralId::SprinterIsa = 33` reserved, no serializer | `core/src/debugger/ttd/ttdserializable.h:82` |
| Shared chips usable by ISA cards | `Uart16550` + `ComPort` + ESP `AtModule` (`core/src/emulator/io/serial/`), ymfm OPL (`core/src/3rdparty/ymfm/`), eve-emu FT812 (`core/src/3rdparty/eve-emu/`), `Flash29F040B` | as named |

## 10. Corrections to earlier Sprinter documents

For [hardware-reference.md](../2026-09-28-sprinter/hardware-reference.md) §11 and §4.5, to apply when the
S6b work lands (this folder does not edit the parent folder):

- The bit assignment "bit 2 = I/O, bit 1 = slot" (INC / MAME) is confirmed by the schematic (DD7 74ALS138);
  MAN §8 and INFO_012 are wrong.
- `#9FBD` bits: 0-5 = A14-A19, 6 = AEN, 7 = RESET (74HC374 DD6), confirmed by the wiring.
- No direct port path to ISA on the Sp2000 (§4.4), although BIOS 3.04 maps `#A3-#BF` (DOS off) to code `#32`.
- ISA RAM cards exist (Shaos TIMER), so "ISA memory reads `#FF`" is a MAME gap, not the hardware.
- ISA interrupts and DRQ / DACK reach PIO port B (bits 0-5); the IRQ pins of each slot are tied together.
