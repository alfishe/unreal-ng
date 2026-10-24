# Research: ZX Spectrum-family input peripherals

| | |
|---|---|
| **Date** | 2026-10-17 |
| **Status** | Research, for the controller manager design |
| **Used by** | [retro-devices.md](retro-devices.md), [mapping-and-profiles.md](mapping-and-profiles.md) |

What each input device looks like to the Z80: ports, decode, bits, timing. Claims are checked against VHDL, firmware
and emulator sources where they exist; each carries a path. **[unc]** = not confirmed; **[mem]** = from general
knowledge, no local source. Five key citations were re-read for this document (marked "re-checked").

Abbreviations:

| Short | Path |
|:--|:--|
| `NXT` | `/Volumes/TB4-4Tb/Projects/emulators/gitlab/ZX_Spectrum_Next_FPGA/cores/zxnext/` (core 3.02.03) |
| `ZES` | `/Volumes/TB4-4Tb/Projects/emulators/github/zesarux/src/` |
| `US` | `/Volumes/TB4-4Tb/Projects/emulators/github/unreal-speccy/` |
| `EVO-U` | `/Volumes/TB4-4Tb/Projects/emulators/github/zx-evo/pentevo/avr/current/` (tslabs unified AVR firmware) |
| `EVO-N` | `/Volumes/TB4-4Tb/Projects/emulators/github/pentevo/avr/baseconf/trunk/src/` (NedoPC BaseConf firmware) |
| `FB` / `FT` | `.../zx-evo/pentevo/fpga/base/` and `.../fpga/current/` (TSConf) |
| `SPH` | `/Volumes/TB4-4Tb/Projects/emulators/gitlab/sprinter-computer-hard/ACEX/` |
| `KP` | `/Volumes/TB4-4Tb/Projects/emulators/github/karabas-pro/firmware/src/` |
| `BC4` | `/Volumes/TB4-4Tb/Projects/emulators/github/zx-evo/pentevo/docs/ZX/zx-ports-full-table.txt` (CP1251) |
| `REPO` | `/Volumes/TB4-4Tb/Projects/Test/unreal-ng/` |

## Contents

- [0. Findings that change the design](#0-findings-that-change-the-design)
- [1. Classic joysticks](#1-classic-joysticks)
- [2. Mice](#2-mice)
- [3. Light guns, pens, tablets](#3-light-guns-pens-tablets)
- [4. ZX Spectrum Next](#4-zx-spectrum-next)
- [5. ZX Evolution: BaseConf and TSConf](#5-zx-evolution-baseconf-and-tsconf)
- [6. Sprinter](#6-sprinter)
- [7. Other clones](#7-other-clones)
- [8. Use cases by title](#8-use-cases-by-title)
- [9. What the model needs](#9-what-the-model-needs)
- [10. Errata in other unreal-ng docs](#10-errata-in-other-unreal-ng-docs)

## 0. Findings that change the design

1. **The Kempston mouse button order is per board.** There is no single standard, so it is a property of the board,
   not of the device.

   | D0 = **Right**, D1 = Left, D2 = Middle | D0 = **Left**, D1 = Right, D2 = Middle |
   |:--|:--|
   | Kondratyev's 1994-95 "USSR Kempston mouse" schematic (`/Volumes/TB4-4Tb/Data/zx-spectrum/hardware/KEMPMOUS.LZH`, member `kempmous.txt` ~:103-118: RIGHB→D0, LEFTB→D1, MIDLB→D2) | ZX-Evo AVR (`EVO-U/zx.h:187-196`, `ps2.c:599-632`) |
   | Next VHDL (`NXT/src/zxnext.vhd:3560`, re-checked: `i_MOUSE_WHEEL & '1' & not BUTTON(2) & not BUTTON(0) & not BUTTON(1)`, PS/2 button 0 = left lands on D1) | Sprinter PLD (`SPH/SP2_ACEX.TDF:1205-1214`) |
   | Karabas-Pro (`KP/fpga/profi/rtl/karabas_pro.vhd:1795`) | Unreal Speccy (`US/input.cpp:302-316`, DirectInput button 0 → bit 0) |
   | ZEsarUX (`ZES/operaciones.c:7192-7195`) | ZX-Poly emulator (`KempstonMouse.java:30-33`) |
   | Scorpion ProfROM Service Monitor (`REPO/docs/inprogress/2026-09-07-scorpion-zs256-clone/profrom-input-subsystem-and-driver-disassembly.md:201-230`) | WoS FAQ: 254 = left ([WoS peripherals FAQ](https://worldofspectrum.org/faq/reference/peripherals.htm)) |
   | | unreal-ng today, every board (`REPO/core/src/emulator/io/mouse/mouse.h:166`, re-checked) |

2. **The wheel nibble (`#FADF` bits 7:4) is an absolute 4-bit wrapping counter, not a delta.** Next
   (`zxnext.vhd:3560`, `src/input/ps2_mouse.v`) and Evo (`EVO-U/ps2.c`: `btn += (b4<<4)&0xF0`). Bit 3 is always 1. The
   Next wiki is wrong on three counts: wheel as a delta, bit 3 as a fourth button, 1 = pressed.

3. **unreal-ng today** has the Kempston joystick and the Kempston mouse, the Sprinter serial mouse and the Evo AVR
   mouse. Spectrum 48 / 128 / +3 decode no joystick (`portdecoder_spectrum48.cpp:104-107`, `spectrum128.cpp:139-142`,
   `spectrum3.cpp:128-131`). The Next's NR #05 Sinclair / Cursor modes are reported, not emulated
   (`portdecoder_next.cpp:604-627`). Missing: Sinclair, Cursor, Fuller, Timex joysticks, AMX mouse, light guns.

4. **The ATM Turbo 2+ has no Kempston joystick or mouse on the board.** Its feature table: "22. Kempston joystick and
   mouse — Not supported" (`/Volumes/TB4-4Tb/Projects/emulators/svn/atmturbo/doc/ver_7_10/TURBO 2+ Assembly and
   Configuration Manual.doc`, textutil lines 201-202, 502, 540). Unreal Speccy answers Kempston there anyway
   (`US/io.cpp:959-977`).

5. Two unreal-ng docs misread BC#4 and one misplaces the Timex AY ports: [§10](#10-errata-in-other-unreal-ng-docs).

## 1. Classic joysticks

### 1.1 Kempston joystick (`#1F`)

Active high, idle 0.

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| Meaning | (Start, MD) | (A, MD) | Fire 2 / C | Fire / B | Up | Down | Left | Right |

- The original interface drives bits 0-4. Bits 5-7 must read 0 for detection; board builders grounded D5-D7
  (`/Volumes/TB4-4Tb/Data/zx-spectrum/hardware/ZX_EXTEN.LZH`, ZX_EXTEN.TXT:46-80, 346-352).
- Pin 9 = Fire 2 on some Kempston-compatible interfaces ([forum](https://spectrumcomputing.co.uk/forums/viewtopic.php?p=104164)).
  ZEsarUX treats b5 / b6 / b7 as Fire 2 / 3 / 4 (`ZES/joystick.c:212-213`).
- The Evo 8-bit layout, re-checked (`EVO-U/joystick.h:11-19`): b4 fire (Sega B), b5 add. fire (Sega C), b6 Sega A, b7
  Sega Start.

Decode variants:

| Variant | Address condition | Where |
|:--|:--|:--|
| A5 = 0 only (original 74LS365, no /RD, breaks IM2) | A5=0 | `REPO/docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md:744-783`; Unreal Speccy non-ATM (`US/io.cpp:959`); ZEsarUX "Inves" (`ZES/operaciones.c:7284`) |
| A7..A5 = 000 (two chips; MAME `kempjoy`) | A7=A6=A5=0 | research-cards.md as above |
| A7=A6=A5=0, A0=1 | `(lo & 0xE1)==0x01` | ZEsarUX default (`ZES/operaciones.c:7296`) |
| A5=0, A0=1 | Pentagon-1024SL, "Kempston-M" | `BC4:52, :132` |
| A7=0, A5=0, A1=A0=1 | Scorpion | `BC4:54` |
| A0=1 (every odd port) | Kay-1024 | `REPO/docs/inprogress/2026-10-07-kay/requirements.md:30` |
| full low byte `#1F` | Evo Base / TS (`FB/z80/zports.v:201,429`; `FT/z80/zports.v:450-455`); Karabas-Pro (`karabas_pro.vhd:1822`); Next (`zxnext.vhd:2454`) | |
| full 16 bits `#FF1F` | Scorpion as polled by ProfROM | profrom doc :136-145 |

- Most clones gate `#1F` off inside TR-DOS, where `#1F` is the WD1793 status port. On Scorpion, Unreal Speccy returns
  bits 7..5 from the WD `#FF` port (`US/io.cpp:974-975`). On TSConf `#1F` is also masked by FDDVirt bit 7.
- **Cheap-interface quirk.** An interface that answers during the IM2 acknowledge puts the joystick byte on the bus as
  the vector low byte (`ZES/joystick.c:53-78`, `cpu.c:459-475`). Affects IM2 games; an emulation option.

### 1.2 Sinclair Interface 2, +2, +2A, +3 (keyboard half-rows, active low)

The [ZX Interface 2 manual](https://spectrumcomputing.co.uk/pub/sinclair/hardware-info/z/ZXInterface2_Manual.html):

- **Joystick 1** = keys 6 L, 7 R, 8 D, 9 U, 0 F, half-row `#EFFE`.
- **Joystick 2** = keys 1 L, 2 R, 3 D, 4 U, 5 F, half-row `#F7FE`.

Naming is inconsistent. The WoS FAQ calls 1-5 "left" and 6-0 "right". Next NR #05 code 011 = "Sinclair 1" = 6-0, code
000 = "Sinclair 2" = 1-5 (`NXT/nextreg.txt:92-119`, re-checked; key ROM `NXT/src/ram/init/keyjoy_64_6.coe`). A VHDL
comment (`zxnext.vhd:3431-3434`) and the firmware's `editor.c:55-59` have them reversed. **We use 6-0 = Sinclair 1.**

Bits within the half-rows (`ZES/operaciones.c:6567-6610`):

| Half-row | b4 | b3 | b2 | b1 | b0 |
|:--|:--|:--|:--|:--|:--|
| `#EFFE` (Sinclair 1) | 6 Left | 7 Right | 8 Down | 9 Up | 0 Fire |
| `#F7FE` (Sinclair 2) | 5 Fire | 4 Up | 3 Down | 2 Right | 1 Left |

+2A / +3 connector (non-Atari): 2 = ground (common), 4 = Fire, 5 = Up, 6 = Right, 7 = Left, 8 = ground / common,
9 = Down. An Atari stick (fire on 6, common on 8) misbehaves
([Linux joystick-parport](https://docs.kernel.org/next/input/devices/joystick-parport.html)). **[unc]**: decoded from an
ASCII diagram; which ground pin is the scan line is not certain.

### 1.3 Cursor / Protek / AGF

Keys 5 L, 6 D, 7 U, 8 R, 0 F. Left = `#F7FE` b4; on `#EFFE`: 6 = b4, 7 = b3, 8 = b2, 0 = b0. ZEsarUX has a
"Cursor & Shift" variant that also presses CAPS SHIFT (`ZES/operaciones.c:6528-6530, 6576-6636`) and uses it for the
Next's Cursor mode (`ZES/machines/tbblue.c:4742-4820`); the Next VHDL presses the plain keys only
(`NXT/src/input/membrane/membrane_stick.vhd:121-149`).

### 1.4 Fuller Box (`#7F`, active low)

| Bit | 7 | 6..4 | 3 | 2 | 1 | 0 |
|:--|:--|:--|:--|:--|:--|:--|
| Meaning | Fire | 1 | Right | Left | Down | Up |

Re-checked in `ZES/operaciones.c:7091-7107` ("F---RLDU, active bits low", exact low byte `#7F`). The decode width is
disputed: FUSE `#FF/#7F` vs zxsp A7-A5 = 011 (`REPO/docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md:128-209`).
The same box has an AY at `#3F` (register select) and `#5F` (data) (`ZES/operaciones.c:9609-9625`; `US/io.cpp:360-365`).

### 1.5 Timex TC2048, TS2068, TC2068

- **TC2048**: built-in Kempston on A5 = 0 (`zxsp/Source/Uni/Items/Joy/Tc2048Joy.cpp:13-34`).
- **TS / TC2068**: two sticks on AY port A (R14). Select R14 with `OUT #F5`, then `IN #F6`. The high byte picks the
  stick: A8 = 1 left (`#01F6`), A9 = 1 right (`#02F6`); both set gives the AND
  (`zxsp/.../Ay/AySubclasses.cpp:29-79`, `Joy/Tc2068Joy.h:9-34`). Active low: b0 Up, b1 Down, b2 Left, b3 Right; Fire
  is b7 (zxsp) or b4 (WoS FAQ) **[unc]**. R7 bit 6 must be 0 (port A input). ZEsarUX does not implement it.

### 1.6 Other Sinclair-era interfaces

| Device | Model | Confidence |
|:--|:--|:--|
| Kempston Pro, DK'Tronics dual, Ram Turbo | `#1F` Kempston plus Sinclair / Cursor key emulation on a second socket; no local source models them separately (ZES §B9). Model as "Kempston + Sinclair" connectors on one card | **[mem / unc]** |
| DK'Tronics / AGF programmable | stick lines onto arbitrary keys; equivalent to Next UDK mode or the Evo key maps | WoS gives no ports |
| Trickstick (East London Robotics) | no local source | **[unc]** |
| Currah µSpeech | not an input device | |
| COMCON | a second fire button; SZX joystick type 5 (`REPO/docs/inprogress/2026-09-29-szx-snapshots/szx-format-reference.md:344-362`) | forum above |

The SZX format lists the joystick types worth supporting: JOYSTICK 0 Kempston, 1 Fuller, 2 Cursor, 3 Sinclair 1,
4 Sinclair 2, 5 Comcon, 6 Timex 1, 7 Timex 2, 8 disabled; AMOUSE 0 none, 1 AMX, 2 Kempston (same file).

### 1.7 Spectrum 128 keypad

Serial on the AY port A lines (the KEYPAD socket shares them with RS232 / MIDI), polled by the 128 ROM. No local
source implements it **[mem / unc]**.

## 2. Mice

### 2.1 Kempston mouse

Ports `#FADF` buttons, `#FBDF` X, `#FFDF` Y.

Decode in the Kondratyev schematic: a 74138 with A = A10, B = A8, C = A7; G1 = A0; /G2A = /IORQ and A5 = 0; /G2B = /RD;
Y4 = buttons, Y6 = X, Y7 = Y. The note says the host must block its own `#DF`.

| Implementation | Decode |
|:--|:--|
| Evo | low byte `#DF`; A8 = 0 buttons; A8 = 1 with A10 = 0 X, A10 = 1 Y (`FB/z80/zkbdmus.v:125-128`) |
| Next | low byte `#DF` with A11:8 = A / B / F; needs the NR #82-85 enable bit 13 (`zxnext.vhd:2668-2670`) |
| Karabas-Pro, ZX-Poly | exact 16 bits |
| ZEsarUX | A5 = 0 only (`ZES/operaciones.c:7151`) |
| Unreal Speccy | `port \| 0xFA00`: A13, A15 ignored (`US/io.cpp:961-963`) |
| unreal-ng | A9 = 1 and low 6 bits = 011111 (`REPO/core/src/emulator/ports/portdecoder.cpp:1310-1338`) |

Buttons byte (`#FADF`):

| Bit | 7:4 | 3 | 2 | 1 | 0 |
|:--|:--|:--|:--|:--|:--|
| Meaning | wheel counter (1111 with no wheel on Evo; forced 0 by ZEsarUX TBBlue) | 1 | Middle | L or R | R or L |

- Buttons active low, D0 / D1 order per board ([§0](#0-findings-that-change-the-design)).
- X grows to the right. Y grows **upward** on Next, Evo and Karabas (PS/2 +Y is up); the Sprinter returns NOT Y.
- Resolution: Next NR #0A bits 1:0 scale {00 ×2, 01 ×1, 10 ÷2, 11 ÷4}, bit 3 swaps L/R (re-checked,
  `NXT/nextreg.txt:179-181`; `NXT/src/input/ps2_mouse.v:170-184`). Evo: hold LMB + RMB and press keypad `*` / `+` / `-` (`EVO-U/ps2.c:762-802`).
- Detection: Evo with no mouse reads X = Y = buttons = `#FF`; with a mouse X = 0, Y = 1 (`EVO-U/zx.c:915-932`).
  unreal-ng resets to X = 31, Y = 85 so software sees a mouse (`mouse.h:87-88`).

### 2.2 AMX mouse (Z80 PIO, interrupts)

Source: [velesoft amx_mouse.txt](https://velesoft.speccy.cz/othermouse/amx_mouse/from_internet/amx_mouse.txt) (secondary,
written as an emulator note).

- Z80 PIO: `#1F` data A (X), `#3F` data B (Y), `#5F` control A, `#7F` control B. Buttons on `IN #DF` (`#FF` / `#00`;
  polarity per button undocumented).
- PIO in mode 1 (control word bits 7:6 = 01, low nibble 1111); vector byte bit 0 = 0; interrupt-enable word bit 0 = 1,
  bit 7 = enable.
- Each mickey raises an IM2 interrupt, A for X, B for Y; the sign is in the data register (0 = +, 1 = -). Up to about
  32 interrupts per frame per axis in emulation.
- Needs a daisy-chained, RETI-aware IM2 interrupt model. Port clashes: `#1F` (Kempston, DISCiPLE), `#3F` (Multiface),
  `#DF`. Not in ZEsarUX; zxsp has a TODO class (`zxsp/.../AmxMouse.cpp:9-14`).

Primary facts (2026-10-23, built in C11 as the slot card `amx-mouse`):

- [Schematic](https://velesoft.speccy.cz/othermouse/amx_mouse/schematic/amx-sch-color.png) (velesoft's redrawn
  original): PIO /CE = A7, B/A = A5, C/D = A6, nothing else decoded (the PIO answers any port with A7 = 0; the
  [Sinclair Wiki](https://sinclair.wiki.zxnet.co.uk/wiki/AMX_Mouse) says the same). X phase A on /ASTB, phase B on A0;
  Y phase A on /BSTB, phase B on B0; ARDY / BRDY open; /INT on the edge's /INT, IEI tied high, IEO open.
- Buttons: a 74LS00 / 74LS32 select on A7 = 1, A6 = 1, A5 = 0 with /IORQ and /RD (no M1); a held button pulls its line
  low through a diode, D7 left, D6 middle, D5 right (schematic, Sinclair Wiki; z88dk's driver reads D6 right, D5
  middle); released buttons and the other bits are the idle bus. The `#FF` / `#00` above is wrong (z88dk says so too).
- AMX Art 1.1 (disassembled): `I = #EE`, vectors `#F0` (X) and `#F2` (Y), `OUT (#5F) / (#7F)`: `#4F` (mode 1), the
  vector, `#87` (interrupts on, no mask); the handlers read the data port, `AND 1` (X: 1 = left; Y: 0 = up, BASIC PLOT
  coordinates) and end with `EI : RETI`; the frame interrupt's vector is the idle bus `#FF` and its handler ends with
  `RET`. z88dk writes the vector, `#97`, `#4F`: the `#4F` is then the mask word (UM0081, MAME), the port stays in mode 1
  from reset; it takes any non-zero data byte as the negative direction, so the other data lines must read 0.
- One interrupt per quadrature cycle (phase A's rising edge) is read from the schematic, unconfirmed. The SZX `AMXM`
  block's exact layout is unconfirmed (the spec page is gone); the loader still reports it as not emulated.

### 2.3 AY mouse (V.M.G. / Korvet style, Unreal Speccy `Mouse=AY`)

Not a Scorpion feature in any local source **[unc]**. AY port A, R14. Writing R14 bit 6 picks the axis (0 X, 1 Y); a
1→0 edge on bit 6 resets both counters to 8. Read = `0xC0 | 4-bit counter | buttons<<4` (`US/input.cpp:367-397`,
`US/io.cpp:608-614, 1029-1037`; ZXMAK2 `AyMouseDevice.cs:60-90`).

### 2.4 Serial mice

- **Sprinter**: Microsoft 3-byte packets, 1200 baud 7N1, Z84C15 SIO channel B, ports `#1A` / `#1B`, clocked by CTC0.
  The PLD also decodes the same line into a Kempston view (`SPH/MOUSE.TDF:32-72`, `SP2_ACEX.TDF:1205-1214`).
- **Karabas-Pro in CP/M**: an emulated 8251 at `#F3` / `#D3`, IM0 RST 20h / 28h (`KP/.../mouse/serial_mouse.vhd:126-135`,
  `hw_int.vhd:48-73`).

### 2.5 SAM Coupé mouse

`IN #FFFE` (high byte `#FF`) returns a nibble stream: `#F`, `#F`, buttons, Y as three nibbles, X as three nibbles. The
index resets after 50 µs without a read (`mame/src/devices/bus/samcoupe/mouse/mouse.cpp:101-136`).

### 2.6 MSX mouse

No ZX clone uses it locally **[unc]**. Protocol: `mame/src/devices/bus/msx/ctrl/mouse.cpp:73-114`.

## 3. Light guns, pens, tablets

| Device | Port | Bits |
|:--|:--|:--|
| Gunstick, Sinclair 1 / 2 | key rows | trigger = key 0 / 1; light = key 8 / 3 |
| Gunstick, Kempston | `#1F`, also any `#DF` | b4 trigger, b2 light |
| Defender | Kempston port | base `#18`; b3 = 0 trigger; b4 light |
| Magnum Light Phaser (AUX) | AY R14 | b5 = 0 trigger; b4 = 1 light |
| Magnum Light Phaser (EAR) | `#FE` b6 | |
| Trojan light pen | `#FE` b6 | light |
| Stack Light Rifle | `#DF` | b1 = 0 trigger; b4 = 0 light |
| Datel Lightwriter | `#3F` | b0 |
| Spectrum Doodler | `#7F` = Y, pen down on b7; `#BF` = X | |
| SAM light pen | `#F8` (LPEN / HPEN by A8); `#FE` b5 | `samcoupe.cpp:225, 584-610` |
| Grafpad (British Micro) | `#FF3F` pen up / down, `#FFBF` X, `#FF7F` Y | zxsp `Grafpad.cpp:9-13` (stub) |

Sources: `ZES/operaciones.c` and `ZES/joystick.c:545-648`. Every light gun needs a **beam-position test**: the pixel
under the pointer must be lit, within 0-2 scanlines of the current beam (`ZES/joystick.c:617-648`). The manager must
expose that coupling to the video timing. Trackballs: no local source; they are mice.

## 4. ZX Spectrum Next

Sources: `NXT/src/zxnext.vhd`, `nextreg.txt`, `ports.txt`.

### 4.1 NR #05 joystick modes

Left stick mode = `{bit 3, bits 7:6}`, right = `{bit 1, bits 5:4}` (re-checked, `nextreg.txt:92-119`). Power-up: left 001,
right 000; the value survives reset and the firmware sets it at boot (`zxnext.vhd:1105-1106, 5157`).

| Code | Mode | Read via |
|:--|:--|:--|
| 000 | Sinclair 2 | keys 1-5 |
| 001 | Kempston 1 | `#1F` bits 5:0 |
| 010 | Cursor | keys 5 6 7 8 0 |
| 011 | Sinclair 1 | keys 6-0 |
| 100 | Kempston 2 | `#37` bits 5:0 |
| 101 | MD 1 | `#1F` bits 7:0 |
| 110 | MD 2 | `#37` bits 7:0 |
| 111 | user-defined keys | keyjoy RAM |

Both sticks on the same port are ORed (`zxnext.vhd:3472-3506`). `#1F` / `#37` decode only when a stick uses them and
the NR #82-85 enable bit 6 / 7 is set. With the mouse disabled a `#DF` alias of `#1F` exists (enable bit 23).

### 4.2 `#1F` / `#37` and the MD pad

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| Meaning | START | A | C / Fire 2 | B / Fire 1 | U | D | L | R |

NR #B2 (read only): b7 right X, b6 right Z, b5 right Y, b4 right MODE, b3 left X, b2 left Z, b1 left Y, b0 left MODE
(`zxnext.vhd:6215`).

MD reader (`NXT/src/input/md6_joystick_connector_x2.vhd`): one state machine reads both connectors through a
multiplexer, ticking every 4.57 µs; select (pin 7) phases last about 9.14 µs, then about 2.27 ms of rest, a cycle of
about 2.34 ms. A 3-button pad is detected when pins 3 and 4 are both low during a select-low phase (A and START latched
only then); a 6-button pad when Up and Down are both low on the third low pulse (X, Y, Z, MODE latched only then).

Undocumented: A + B + C + START on either pad is the reset button; short = soft reset, held over 1 s = hard reset
(`zxnext_top_issue4.vhd:719-721`).

### 4.3 Buttons that produce keys (keyjoy)

- 64 entries of 6 bits: row (bits 5:3), column (bits 2:0), column 7 = none.
- Programming: write `#80` to NR #28, then 0 (left) or 16 (right) to NR #29, then twelve bytes to NR #2B
  (auto-increment). Only the UDK half is writable. Order (re-checked): R, L, D, U, B, C, A, START, Y, Z, X, MODE
  (`nextreg.txt:115-117`: "MODE=11 X Z Y START A C B U D L R=0").
- Which buttons make keys (`membrane_stick.vhd:121-196`): UDK mode all 12; Kempston modes buttons 5-11 from C on, so C
  can be both port bit 5 and a key (NR #05's text says otherwise); MD modes Y, Z, X, MODE. `nextreg.txt` itself says
  "in kempston and md modes, excess buttons ... will generate key input if so programmed".
- Boot defaults: P, O, A, Q, SPACE, M, ENTER, S, Y, Z, X (tbblue firmware `config.c:241-272`).

### 4.4 NR #0B joystick I/O mode

Bit 7 = enable. Bits 5:4: 00 bit-bang pin 7, 01 pin 7 clocked by CTC channel 3, 10 / 11 UART on the left / right port
(TX pin 7, RX pin 9, CTS pin 6); bit 0 picks ESP or Pi. While enabled the ports return raw inverted pins and pad reading
stops; it resumes about 2.3 ms after I/O mode ends (`zxnext.vhd:3510-3539`). Port `#37` writes for I/O mode (wiki, core
3.1.5) are obsolete. In unreal-ng this is `TTDDeviceType::JoystickSerialPeer` (65).

### 4.5 Mouse and PS/2

Mouse: [§2.1](#21-kempston-mouse); NR #0A bits 1:0 scale, bit 3 swap. The PS/2 mouse is initialized with sample rates
200 / 100 / 80 to unlock the IntelliMouse wheel. NR #06 bit 2 (config mode only) swaps the keyboard and mouse PS/2 pins.

### 4.6 Keyboard

- PS/2 keymap (`NXT/src/input/keyboard/keymaps.vhd`, `ps2_keyb.vhd`): 512 entries indexed by {E0 flag, set-2 code};
  entry b7 = SYM SHIFT, b6 = CAPS SHIFT, b5:3 row, b2:0 column (7 = none); b7:6 = 11 are F1..F8. Programmed through
  NR #28 bit 7 = 0, NR #29, NR #2B; NR #2A is ignored by the VHDL.
- Hotkeys: F9 or LWin = M1 NMI; F10 or RWin = DRIVE NMI; Pause clears the matrix; holding M1 turns membrane 1-0 into
  F1-F10.
- 8×7 matrix: extra columns 5-6 hold EXTEND, UP, CAPS LOCK, GRAPH, TRUE VIDEO, INV VIDEO, BREAK, EDIT, `; " , .`,
  DELETE, RIGHT, LEFT, DOWN. They become CS / SS combinations unless NR #68 bit 4 is set (`membrane.vhd:164-219`). Raw
  state: NR #B0 = `; " , .` U D L R; NR #B1 = DEL EDIT BREAK INV TRUE GRAPH CAPSLOCK EXTEND (1 = pressed).
- `#FE` read: `1 & EAR & 1 & cols` (`zxnext.vhd:3455-3468`).
- The wiki is stale on the MD extra buttons, the `#FADF` bits, mode 111 and `#37` writes.

## 5. ZX Evolution: BaseConf and TSConf

AVR ATmega128 plus FPGA.

**Keyboard.**

- PS/2 set-2 codes go through `kbmap.c` tables of two ZX keys per scan code, a normal and an E0 table, overridable in
  EEPROM when the signature 'K','B' is present (`EVO-U/kbmap.c:13-379`).
- Arrows = CS + 5/6/7/8, Del = CS + 9, Backspace = CS + 0, CapsLock = CS + 2, PgUp / PgDn = CS + 3/4, punctuation =
  SS + key. Esc releases all keys (`CLRKYS`). Ctrl / Alt: NedoPC maps Ctrl = SS and Alt = CS + SS; tslabs leaves them
  unmapped.
- Each ZX key has a reference count, and a CS / SS interlock delay applies (`EVO-U/zx.c:469-665, 853-887`).
- The matrix goes to the FPGA as SPI register `#10` ×5 bytes, then a `#11` strobe.
- `#FE` bit 5 differs: 0 on Base, 1 on TS (`FB/z80/zports.v:411`, `FT/z80/zports.v:406`).

**Joystick.**

- On-board DB9 on PG0-4 (R, L, D, U, Fire); a second stick on the 40-pin keyboard connector (`EVO-U/pins.h:142-186`).
- `#1F`: full low byte outside DOS; 8-bit (`KEMPSTON_8BIT`, tslabs) or 5-bit (NedoPC). 8-bit layout:
  b4 Fire / Sega B, b5 Sega C, b6 Sega A, b7 Start (`EVO-U/joystick.h:11-19`).
- Pad modes: Kempston, Sega 3-button, Sega 6-button aliased to 3-button, NES (enum only).
- Mapping options (tslabs): pad 0 Kempston / pad 1 keys, or both as keys; per-button key bytes with +CS / +SS flags;
  per-button autofire. Stored in an EEPROM TLV entry ("JOY", tag `#10`), set through CFGIF (`EVO-U/config.h:56-154`).
- NedoPC BaseConf instead auto-detects 8 / 12-button Sega pads and has 4 key maps (P / O / A / Q default, Kempston,
  Super Mario, Elite), switched with X + Y + Z plus B / C (`EVO-N/joystick.c:23-161`).
- Neither the AVR setup nor TS-BIOS has a joystick menu.

**Mouse**: [§2.1](#21-kempston-mouse) (AVR registers `#20` / `#21` / `#22`; D0 = LMB).

**Gluk CMOS.**

- `#DFF7` address, `#BFF7` data; `#EFF7` bit 7 enables the clock; in shadow `#DEF7` / `#BEF7`.
- Register `#0C`: NumLock LED / SD status on read; PS/2 log clear and Caps LED on write. `#0D`: modifier flags (Ctrl,
  Alt, Shift L/R, F12). `#0E` (tslabs only): Win and Menu keys.
- Extension window: write a type to register `#F0`, read `#F0-#FF`. Type 2 = **PS/2 keyboard log**, a 16-byte ring of
  raw set-2 bytes including E0 / F0 (0 = empty, `#FF` = overflow); type `#0E` = CFGIF (`EVO-U/rtc.c:230-410`,
  `version.c:16-74`, `ps2.c:63-136`).
- NedoOS uses the log: `OUT(DEF7),F7 / OUT(BEF7),2`, then polls
  (`/Volumes/TB4-4Tb/Projects/emulators/github/NedoOS/src/kernel/main.asm:287-292`, `kernel/ps2drv.asm:47-80`). Wild
  Commander's local sources do not.

**Hotkeys**: Ctrl+Alt+Del hard reset; Ctrl+Alt+F12 Base / TS bitstream switch; F12 short = reset, long = ATX power off;
BaseConf PrintScreen = NMI, ScrollLock = video mode; TS Menu + F1 / F2 / F3 (`EVO-U/zx.c:689-842`).

TSConf has no extra input ports beyond these.

## 6. Sprinter

Peters Plus Sp2000.

**Keyboard**: AT / PS/2 set 2, to two places at once:

- Z84C15 SIO channel A (`#18` data, `#19` control): raw scan codes for DSS / BIOS; the 3-byte FIFO can overflow.
- ACEX PLD (`SPH/KBD.TDF`, table `KBD_INI2.MIF`): codes to a ZX matrix read at `#FE` through PLD code `#40`, in Spectrum
  mode only (ALL_MODE bit 0 = 0).

`#FE` read: b7 = the "Function Shift" column (F1-F10 = FS + 1..0, NumLock, ScrollLock; reads 0 at `#7FFE` while one is
held); b6 tape; b5 = 0 when CBL is off **[unc: MAME and unreal-ng return 1]**. E0 prefixes are ignored. F12 toggles
turbo; Ctrl+Alt+Del resets.

**Mouse**: serial on SIO B ([§2.4](#24-serial-mice)); Kempston view at PLD code `#58` with D0 = L.

**Joystick**: Kempston, PLD code `#15`; `#1F` with DOS off, `#FF` with DOS on; value = `FDC INTRQ / DRQ (b7:6) |
joystick`. MAME adds two Sega-style pads (one multiplexed by SIO DTR, one on PIO A) **[unc]**
(`mame/src/mame/sinclair/sprinter.cpp:1330-1374`).

**DSS API**: `RST #10` functions `#30-#37` (WaitKey, ScanKey, ...), returning A = char, D = position code, E = ASCII,
C = lock flags, B = shift flags
(`/Volumes/TB4-4Tb/Data/zx-spectrum/sprinter/documentation/bios-dss-api/dss-1.71-functions.asm:365-447`). Mouse: `RST
#30`, C = 03 read, HL = X, DE = Y, A = buttons D0 L / D1 R / D2 M
(`/Volumes/TB4-4Tb/Projects/emulators/github/SprinterFirmware/DOS/intmouse.asm:12-105`).

**unreal-ng gaps**: the PLD scan-code-to-matrix table is not modelled (`REPO/docs/inprogress/2026-09-28-sprinter/TODO.md:290-293`);
no Sega pads; the Kempston mouse view exposes a middle button on b2, which the TDF forces to 1.

## 7. Other clones

| Machine | Input facts | Source |
|:--|:--|:--|
| **ATM Turbo 2+** | `#FE` reads (A0 = 0, A1 = A2 = 1) stall the Z80 with WAIT while an 8031 / AT89S52 answers. Mode 0 emulated matrix ANDed with the native port; 1 CP/M key code; 2 CP/M code, flags or extra register by A15 / A14; 3 raw XT scan code. Scan address `#55` answers `#AA`; the next read's high byte is a command (`#x8` set mode, `#xA` / `#xB` RUS / LAT). `#FF77` D6 = 1 disables the controller. Sinclair joysticks only, through the matrix; **no Kempston** | `/Volumes/TB4-4Tb/Data/zx-spectrum/atm-turbo/keyboard-controller/at-keyboard-v2.2/extracted/atm_xt.txt:1-340`; `at-keyboard-v4.1/extracted/atm_at41.asm:791-836`; `REPO/docs/inprogress/2026-10-01-atm2-keyboard-controller/reference-atm2-kbc.md` |
| **Profi** | XT controller (8035 "JV KRAMIS 1.27") as a virtual matrix with WAIT; `#FE` bit 5 = EXT; `#AAFE` / `#55FE` switch modes. Kempston = 8255 port A at `#1F`, outside DOS / CP/M; Kempston mouse with CP/M off | `REPO/docs/inprogress/2026-10-01-profi-v3-v5/research-profi-keyboard.md` |
| **Karabas-Pro** (Profi clone) | PS/2 through an AVR into the matrix. `#1F`: D5 fire 2, D6 A, D7 B. `JOY_MODE` Kempston / Sinclair 1 / Sinclair 2 / Cursor / QAOP; non-Kempston modes press matrix keys | `KP/fpga/profi/rtl/avr/avr.vhd:58-59, 260-268`; `avr/src/main.cpp:223-251` |
| **Pentagon 128** | Kempston decode A7 = 0, A6 / A5 optional | |
| **Pentagon-1024SL** | Kempston A5 = 0, A0 = 1; no built-in mouse. Mouse and PS/2 from cards: Z-Controller; ZXMC-1/2 by **Karimov** (not Kondratyev), PC key data at `#80DF-#87DF` | `BC4:228-254` |
| **Scorpion ZS-256** | `#FE` decode A1 = A2 = A5 = 1, A0 = 0 outside DOS; b5 RS-232 in, b7 printer BUSY. ProfROM polls `#FF1F` and the mouse ports | `/Volumes/TB4-4Tb/Data/zx-spectrum/scorpion/extracted/SCORPION/scorpion.doc:62-67` |
| **Kay-1024** | Kempston on every odd port; the passport bit order (D0 L, D1 R, D2 U, D3 D) is not traced **[unc]**. The IBM keyboard needs a NemoBus card | `REPO/docs/inprogress/2026-10-07-kay/requirements.md:30` |
| **Didaktik D40 / 80** | 8255 port A as Kempston, active high, on `#1F / #3F / #5F / #7F` with A7 = 0; Gama / M / Kompakt wiring **[unc]** | `mame/src/devices/bus/spectrum/d40.cpp:261-285` |
| **ZX-Poly** | `#FE` / `#1F` need a full low byte in ZX-Poly mode; in 128 mode full `#1F` when the mouse is on, A5 = 0 otherwise | `zxpoly-emul/.../KeyboardKempstonAndTapeIn.java:455-486` |
| **SAM Coupé** | one joystick port; with a splitter, sticks on keys 6-0 / 1-5; mouse on `IN #FFFE` ([§2.5](#25-sam-coupé-mouse)) | |
| **Generic XT adapter** (Shepelev 1995) | `IN #BFFD` returns the raw scan code, 0 when nothing is new, bit 7 = release | `/Volumes/TB4-4Tb/Data/zx-spectrum/hardware/XT-KEYB.LZH`, member `xt-keyb.doc:80-98` |

## 8. Use cases by title

Titles marked **[mem]** need checking per title in ZXDB (Spectrum Computing "Controls" field).

| Area | Software | Needs |
|:--|:--|:--|
| Graphics editors | *The Art Studio* (OCP / Rainbird): AMX mouse; a Kempston mouse version came with the Kempston mouse offer and the Datel re-release; joystick options Cursor, Interface 2, Kempston ([Spectrum Computing](https://spectrumcomputing.co.uk/entry/7915/ZX-Spectrum/The_Art_Studio)). *AMX Art* (bundled with the AMX mouse), *Artist II* (AMX / Kempston mouse) **[mem]** | AMX PIO interrupts and the Kempston mouse |
| Next | NextZXOS `mouse.drv`: Kempston mouse with the wheel in the top nibble, buttons masked `&7` (`/Volumes/TB4-4Tb/Projects/emulators/gitlab/tbblue/src/asm/mouse/mouse.asm:55-56, 393-404`); SprEdit uses `#FADF` (`gitlab/tbblue/src/asm/spredit/SprEdit.asm`); Next games use MD pads (A / C / START, NR #B2 for X Y Z MODE) **[mem: no local title list]** | MD 6-button pad, mouse with wheel |
| NedoOS (Evo / ATM) | PS/2 log for the keyboard; Kempston mouse in apps and games, e.g. worms (`NedoOS/src/games/worms/input.asm:29`), ufo2 (`src/games/ufo2/xmouse.asm`) | Gluk log, Kempston mouse |
| Desktops | Wild Commander, Sprinter DSS desktop / Flex Navigator: Kempston or DSS mouse **[mem for Flex]** | mouse |
| Service | Scorpion ProfROM Service Monitor 3.0b and later: joystick and mouse (`scorp.doc:126-128`) | `#FF1F`, mouse |
| Two fire buttons | essentially none in the commercial era; Velesoft patched *Green Beret* and *Commando* for a second button (forum above); modern homebrew and Next titles use C / fire 2 (`#1F` b5) | Kempston b5 |
| Two players | Sinclair 1 + 2, or Kempston + Sinclair; Next, Karabas and Evo can configure two sticks; specific simultaneous titles not verified **[unc]** | two connectors |
| Keys-only games | QAOP / Space, OPQA / M, 5-8 / 0 (Cursor). Next UDK, Evo key maps and Karabas QAOP mode exist because so many games are keys-only | keyboard overlay |
| Light guns | *Magnum Light Phaser* titles; Gunstick (MHT) titles such as *Solo* and *Bestial Warrior* **[mem]** | beam test |

## 9. What the model needs

- A per-board decode mask for each device ([§1.1](#11-kempston-joystick-1f), [§2.1](#21-kempston-mouse)).
- Per-board polarity and bit order ([§0](#0-findings-that-change-the-design)).
- Port arbitration: the `#1F` / `#DF` / `#3F` clashes with TR-DOS, Multiface, DISCiPLE and AMX.
- Keyboard-matrix injection for Sinclair, Cursor and UDK modes, ANDed with the matrix (the Next ANDs into the column
  scan; its CS / SS combinations are asserted one scan early, `membrane.vhd`).
- AY R14 sources: Timex, Magnum, AY mouse, 128 keypad.
- IM2 PIO interrupts (AMX).
- Beam-position queries (light guns).
- Raw scan-code streams: Gluk log, ATM mode 3, Sprinter SIO, `#BFFD` adapter.
- A 12-button pad state with board-specific projections: Next `#1F` / `#37` plus NR #B2; Evo 8-bit plus CFGIF maps;
  the Karabas byte. `DebugJoystickManager` already carries 12 buttons on two connectors
  (`REPO/core/src/debugger/joystick/debugjoystickmanager.h:93-99`).

## 10. Errata in other unreal-ng docs

Found during this research; not fixed here (TODO E-1..E-6).

| # | Doc | Says | Correct |
|:--|:--|:--|:--|
| E-1 | `docs/inprogress/2026-09-12-kempston-mouse/hardware-reference.md` §4; `docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md:781` | Kempston mouse D0 = Left on every board | per board: D0 = Right on the Kondratyev schematic, Next, Karabas-Pro, ZEsarUX, ProfROM; D0 = Left on Evo, Sprinter ([§0](#0-findings-that-change-the-design)). `mouse.h:166` follows the Left variant everywhere |
| E-2 | `docs/inprogress/2026-09-12-kempston-mouse/hardware-reference.md` §2.1-2.2; `docs/inprogress/2026-10-07-kay/design.md:53` | BC#4 limits the Kempston mouse to machines "(1,7-9)" | BC#4 gives the mouse no machine list (`BC4:311-321`); "(1,7-9)" belongs to the `#FE` row (`BC4:17`) |
| E-3 | `docs/ports/ports.md:252-258` | Timex AY at `#F4` / `#F5` | `#F5` = register select, `#F6` = data read and write; `#F4` is not an AY port (`mame/src/mame/sinclair/timex.cpp:529-530`) |
| E-4 | MCP `joystick_input` description (`core/automation/mcp/src/mcp-tools.cpp:4294-4394`) | "ATM3 / Scorpion / TS-Conf" only | Pentagon, Profi, Sprinter and Next answer too |
| E-5 | `docs/inprogress/2026-10-07-zx-next/design-peripherals.md:90` | NR `#0A` "button swap bit 2" | bit 3 reverses the buttons, bit 2 is reserved (`NXT/nextreg.txt:179-180`; `zxnext.vhd:1127, 1599`) |
| E-6 | Next wiki (external) | wheel is a delta, `#FADF` bit 3 = fourth button, 1 = pressed | absolute 4-bit counter, bit 3 = 1, buttons active low ([§0](#0-findings-that-change-the-design)) |
