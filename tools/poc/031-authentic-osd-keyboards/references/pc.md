# POC 031 references: the PC-keyboard clones

**Date:** 2026-10-19 · group: ATM-Turbo v4.50, ATM-Turbo 2+ v7.10, ZX-Evo (ATM3 / Pentagon Evolution), TS-Conf,
ZXM-Phoenix v1.0, Sprinter Sp2000.

Matrix notation: half-row by address line, `A8` = `#FEFE` (CS Z X C V), `A9` = `#FDFE` (A S D F G), `A10` = `#FBFE`
(Q W E R T), `A11` = `#F7FE` (1 2 3 4 5), `A12` = `#EFFE` (0 9 8 7 6), `A13` = `#DFFE` (P O I U Y), `A14` = `#BFFE`
(ENT L K J H), `A15` = `#7FFE` (SP SS M N B). "CS+7" means the controller closes both Caps Shift and 7. The meaning
in brackets is the 48K K/L-mode meaning of the pair (for example SS+O = `;`).

## 0. Summary

| Machine (`Config::mem_model`) | Keyboard it really had | What the machine reads | unreal-ng today |
|:--|:--|:--|:--|
| ATM-Turbo 512 v4.50 (`MM_ATM450`) | **No PC keyboard.** The 40-key ZX matrix, or an "extended mechanical matrix" of up to 64 keys | the plain `#FE` matrix | plain matrix, no controller |
| ATM-Turbo 2+ v7.10 (`MM_ATM710`) | XT keyboard (83 keys) on the first 7.00 boards (firmware v1.06), then AT 101/102 keys (AT firmware v1.00 to v4.1); 5-pin DIN | MCS-51 keyboard controller: the matrix, CP/M codes or raw XT set-1 codes | real controller firmware on an MCS-51 core, AT keyboard sending set 2 |
| ZX-Evo (`MM_ATM3`) | PS/2 keyboard (mini-DIN, purple), plus a header for the original ZX keyboard | AVR ATmega128: PS/2 to matrix through a keymap, and a 16-byte raw PS/2 log | AVR log and modifiers emulated; the matrix comes from the host keys |
| TS-Conf (`MM_TSL`) | the same ZX-Evo board: PS/2 plus the ZX keyboard header | the same AVR | as ZX-Evo |
| ZXM-Phoenix v1.0 (`MM_PHOENIX`) | PS/2 keyboard (mini-DIN) through Caro's ZX Multi Card 2 (ATmega8515) | the emulated matrix at `#FE` | **not creatable**: no port decoder |
| Sprinter Sp2000 (`MM_SPRINTER`) | AT 101-key keyboard, 5-pin DIN on the board (AT mini-tower case) | raw set-2 codes on the Z84C15 SIO A, and the PLD's RAM table that turns them into the matrix | SIO path and PLD actions emulated; the matrix comes from the host keys |

Owner decision (2026-10-19) says every machine of this group gets both pictures. The research says the ATM-Turbo
v4.50 should not: it had no PC keyboard. It is a 40-key matrix machine (section 1). That is a question for the
owner.

## 1. ATM-Turbo 512 (v4.50 board)

**Keyboard.** The NedoPC description table "Технические характеристики разных моделей ATM-turbo" gives the keyboard
controller row per board:

| | ATM-turbo 512 (v4.50) | ATM-turbo 2 (v6.40) | TURBO 2+ (v7.10) |
|:--|:--|:--|:--|
| a) | standard mechanical 8x5 matrix (40 keys) | standard 8x5 matrix | standard 8x5 matrix |
| b) | extended mechanical matrix (up to 64 keys) | - | - |
| c) | - | XT keyboard controller on a 537RU10 RAM | XT (AT) keyboard controller (depends on the ROM) on a 1816VE31 (i8031) |

Source: http://atmturbo.nedopc.com/atmdscr.htm. The ATM-1 ports section of the same page (boards 4.20-4.50 and 5.20)
says `IN #FE` is the standard matrix port with D5 = ADC ready, D6 = tape, D7 = the "Z" timing signal. It lists no
keyboard controller.

**Mapping.** None: the matrix as on any Spectrum.

**unreal-ng.** `core/src/emulator/ports/models/portdecoder_atm450.cpp:13-15`: "Not the v7.10 board: no keyboard
controller (#FE is the plain matrix port)". It builds `PortDecoder_ATM710(context, false)`, which skips the
controller (`portdecoder_atm710.cpp:36-44`).

**Picture.** A 40-key ZX keyboard. No source in reach says which case or keyboard was usual for ATM-1 (a Pentagon-style
mechanical keyboard is likely). UNVERIFIED. What the "extended 64-key matrix" looked like: UNVERIFIED.

Side notes:
- the ATM-turbo 2 v6.40 (not an unreal-ng model) read an XT keyboard through a 537RU10 RAM loaded by the Z80 (port
  `#FFE7`) and `#7FFD` D7 (same page);
- `klug-bbs/extracted/ATMSRC/ATMSRC/READ.ME` lists `KEYB.ASM`, "an insert into TR-DOS that handles the XT keyboard
  (without VE31)": a software XT path of those boards.

## 2. ATM-Turbo 2+ (v7.00 / v7.10 boards)

**Keyboard.** atmdscr.htm, section "Управление XT(AT)-клавиатурой на TURBO 2+ (платы 7.00-7.10)": "From version
7.00 the computer has an 8031 chip … it made it possible to connect an IBM XT keyboard (or a modern AT keyboard, you
only need another ROM)". The page talks of "the extended keyboard (101 keys)".

The firmwares (local collection `atm-turbo/keyboard-controller/`) are XT v1.06 (`RF2VE31.rom`, 573RF2 on an i8031),
AT v1.00 (`RFAT710`), AT v2.2, v3.0, v3.1(m), v3.2m (Kamil Karimov, 2005-2019), and AT v4.0 / v4.1 (LVD and Kulicheg,
then Maksagor, 2021 / 2023, AT89S52). There is also `mc7004-xt-adapter/MC04(XT).rom` (2002), a converter that made
the Soviet MS 7004 terminal keyboard (DVK / Elektronika 85) talk XT. People used such keyboards on the ATM.

Era picture: first the XT keyboard (83 keys), and from the AT firmwares on the 101/102-key AT keyboard on a 5-pin DIN.
The connector type on the v7.10 board is not confirmed from a photo here. UNVERIFIED. The local
`docs/inprogress/2026-10-01-atm2-keyboard-controller/` design names the schematic cp7_2 (`atm2kbc.h:12`).

**Mapping, mode 0 "Spectrum KBD".** The table is the same, byte for byte, in the XT v1.06 manual (`atm_xt.txt`) and in
the v4.1 source (`atm_at41.asm` `L_527`, indexed by the **set-1** code). The AT firmwares first turn set 2 into set 1
(table `at2xt`). The table is indexed by the set-1 code without the E0 prefix, so a grey key gives the same as its
keypad twin: grey Up (E0 48) = keypad 8.

Byte format: D7 Symbol Shift, D6..D4 half-row (A8 = 0 … A15 = 7), D3 Caps Shift, D2..D0 bit (D0 = 1 … D4 = 5).
The byte columns below are decoded from these bits. Some comments in the source are wrong: `91h` is commented
"CapSh+Kl_A" but it is SS+A.

| PC key (set 1) | ZX matrix | | PC key | ZX matrix |
|:--|:--|:--|:--|:--|
| Esc (01) | CS+1 (EDIT) | | F1..F5 (3B-3F) | SS+1 .. SS+5 (`! @ # $ %`) |
| 1..0 (02-0B) | 1..0 | | F6..F10 (40-44) | SS+6 .. SS+0 (`& ' ( ) _`) |
| - (0C) | SS+J (`-`) | | F11 (57) | SS+H (`↑`) |
| = (0D) | SS+L (`=`) | | F12 (58) | SS+F (TO) |
| Backspace (0E) | CS+0 (DELETE) | | Num Lock (45), Scroll Lock (46) | none |
| Tab (0F) | CS+3 (TRUE VIDEO) | | KP7 / Home (47) | CS+4 (INV VIDEO) |
| Q..P (10-19) | Q..P | | KP8 / Up (48) | CS+7 (up) |
| [ (1A) | SS+Y (AND) | | KP9 / PgUp (49) | CS+5 (left) |
| ] (1B) | SS+U (OR) | | KP- (4A) | SS+J (`-`) |
| Enter (1C) | ENTER | | KP4 / Left (4B) | CS+5 (left) |
| Ctrl (1D) | CS+SS (extended mode) | | KP5 (4C) | 5 |
| A..L (1E-26) | A..L | | KP6 / Right (4D) | CS+8 (right) |
| ; (27) | SS+O (`;`) | | KP+ (4E) | SS+K (`+`) |
| ' (28) | SS+P (`"`) | | KP1 / End (4F) | CS+9 (GRAPH) |
| ` (29) | SS+A (STOP) | | KP2 / Down (50) | CS+6 (down) |
| Left Shift (2A) | CS | | KP3 / PgDn (51) | CS+8 (right) |
| \ (2B) | SS+S (NOT) | | KP0 / Insert (52) | SS+C (`?`) |
| Z..M (2C-32) | Z..M | | KP. / Delete (53) | CS+0 (DELETE) |
| , (33) | SS+N (`,`) | | Right Shift (36) | SS |
| . (34) | SS+M (`.`) | | KP* / PrtSc (37) | SS+B (`*`) |
| / (35) | SS+V (`/`) | | Alt (38) | CS+4 (INV VIDEO) |
| Space (39) | SPACE | | Caps Lock (3A) | CS+2 (CAPS LOCK) |

Notes:
- PC Left Shift is Caps Shift and PC Right Shift is Symbol Shift. Shift+1 gives EDIT, not `!`.
- Right Ctrl and Right Alt (E0 1D, E0 38) map as Ctrl and Alt. The grey `/` (E0 35) is SS+V. Keypad Enter (E0 1C) is
  ENTER.
- KP9 = CS+5 and KP3 = CS+8 come straight from the table. They look like a slip in the original binary, but every
  version carries them.
- The controller ANDs its virtual matrix with the native port D45 ("Добавка идет командой OR", active-low), so a
  real ZX matrix keyboard works in parallel (`atm_xt.txt`, mode 0).

**Other functions** (`atm_xt.txt`, atmdscr.htm, `at40.txt`, `atm_at41.asm`):
- modes by command `#55 #08 <mode>` (the Z80 sends commands as the A15..A8 of an `IN #FE` after `#55`, and the
  controller answers `#AA`): 0 Spectrum matrix, 1 the CP/M code of the last key, 2 CP/M code plus modifier registers
  (Shift, Ctrl, Alt, Caps / Num / Scroll Lock, RUS/LAT; Right Shift), 3 raw XT set-1 code of the last key;
- RUS/LAT is a flag that **software** sets with commands `#0A` (RUS) and `#0B` (LAT). No key toggles it in the
  controller. The Cyrillic itself is in the CP/M / BIOS driver;
- Ctrl+Alt+Del (set-1 `53`: Delete or KP.) resets the Z80;
- Pause freezes the Z80 (WAIT held) until the next key (fixed in v2.2);
- v4.0+: Ctrl+Alt+Home stops answering the keyboard port (no WAIT: tape loading works), Ctrl+Alt+Ins resumes;
- CP/M codes (mode 1/2): arrows `#70-#73`, Home `#76`, End `#77`, PgUp `#74`, PgDn `#75`, Ins `#78`, Del `#79`,
  keypad `#B0-#B9`, and so on (atmdscr.htm table).

**unreal-ng.**
- `core/src/emulator/io/keyboard/atm2kbc.h:1-17, 34`: `Atm2Kbc` runs the **real firmware** on an MCS-51 core. It
  answers `IN #FE` under WAIT. `kDefaultFirmware = Firmware::V41` (`:52`), with presets V22-7 … V41 (`:38-51`). The
  XT v1.06 and AT v1.00 images are not in the list.
- `atm2kbc.cpp:355, 409`: the host key reaches the controller as **PS/2 set-2** frames from an emulated AT keyboard,
  with typematic repeat. The firmware's own `at2xt` and `L_527` tables do the mapping above.
- `core/src/emulator/ports/models/portdecoder_atm710.cpp:39-44, 53-67`: the decoder makes the controller and attaches
  it as the keyboard's PS/2 sink (`SetPs2Sink`, `:65`). The native port is `Default_Port_FE_In` (`:40`).
- `core/src/emulator/io/keyboard/keyboard.cpp:121-129`: `Atm2Kbc` does not override `ReplacesMatrix()`, so the Auto
  host route is `Both`. A host key presses the ZX matrix (the native port, ANDed in by the firmware) **and** goes to
  the controller as a PC key. For keys whose two translations differ (for example PC `[` = SS+Y through the
  controller, and whatever the host mapping gives on the matrix), both arrive. Worth a check for the OSD: a PC-picture
  key should go through the controller only (route `Ps2`), and a ZX-picture key to the matrix only.

## 3. ZX-Evo (ZX-Evolution, `MM_ATM3` decoder with the Evo AVR)

**Keyboard.** The NedoPC product page (http://nedopc.com/zxevo/zxevo_eng.php) lists "PS/2 keyboard and mouse support;
Original keyboard and joystick support", and a later revision note "Add texts to board (… 'ZX Keyboard/ZX
Joystick' …)". Photo `zxevo-board-front-connectors.jpg` shows the stacked PS/2 mini-DIN pair (purple keyboard, green
mouse). The era is 2009 onward: any PS/2 keyboard, 104/105 keys. So both pictures are authentic: a PS/2 PC keyboard
and a ZX matrix keyboard on the header.

**What the machine reads.** The AVR (ATmega128) decodes PS/2 set 2. It closes ZX matrix positions from a keymap: the
default map in flash, or a user map in EEPROM (signature `KB`), loadable from software. It also keeps a 16-byte raw
scan-code log that the Z80 reads through the gluk-clock extension cells (`#F0-#FF`, extension type 2). NedoOS reads
only that log.

Default keymap (tslabs/zx-evo `pentevo/avr/current/kbmap.c`, `default_kbmap` / `default_kbmap_E0`, last changed
2015-09-24). The index is the set-2 code. Up to two ZX keys per PC key:

| PC key (set 2) | ZX | | PC key | ZX |
|:--|:--|:--|:--|:--|
| letters, digits | the same key | | Backspace (66) | CS+0 |
| Space (29), Enter (5A) | SPACE, ENTER | | Esc (76) | CLRKYS: releases every key (not BREAK) |
| Left Shift (12) | CS | | Tab (0D) | CS+SP (BREAK) |
| Right Shift (59) | SS | | ` (0E) | CS+1 (EDIT) |
| Caps Lock (58) | CS+2 | | , (41) | SS+N (`,`) |
| Left Ctrl (14), Alt (11) | **none** | | . (49) | SS+M (`.`) |
| F1-F12 | none (F12 and Menu+Fn are service keys) | | / (4A) | SS+C (`?`) |
| ; (4C) | SS+Z (`:`) | | - (4E) | SS+J (`-`) |
| ' (52) | SS+P (`"`) | | = (55) | SS+K (`+`) |
| [ (54) | SS+8 (`(`) | | ] (5B) | SS+9 (`)`) |
| \ (5D), ISO \ (61) | SS+CS | | KP0-KP9 | 0-9 (no cursor keys) |
| KP. (71) | SS+M | | KP+ (79), KP- (7B), KP* (7C) | SS+K, SS+J, SS+B (also set the mouse resolution) |
| E0: KP/ (4A) | SS+V | | E0: KP Enter (5A) | ENTER |
| E0: arrows (6B/72/74/75) | CS+5 / CS+6 / CS+8 / CS+7 | | E0: Home (6C), End (69) | SS+Q (`<=`), SS+E (`>=`) |
| E0: Ins (70), Del (71) | SS+W (`<>`), CS+9 | | E0: PgUp (7D), PgDn (7A) | CS+3, CS+4 |

Note: `/` gives `?` (SS+C), `;` gives `:` (SS+Z), `=` gives `+`, `[ ]` give `( )`. The default map puts the
**shifted** PC symbol on some keys.

Service keys (`zx.c` `to_zx`): Ctrl+Alt+Del soft reset; Ctrl+Alt+any key a hard reset (FLAG_HARD_RESET); F12 short
press resets the Z80, long press is ATX power; Ctrl+Alt+F11 / F12 and Menu+F1..F5 / F11 / F12 are setup and
service functions; Print Screen is NMI; Scroll Lock toggles the video mode; Num Lock is a callback.

**unreal-ng.**
- `core/src/emulator/memory/atm/evoavr.h:48-56`, `evoavr.cpp:177-233, 235-239`: `EvoAvr` is the PS/2 sink. It parses
  set-2 bytes into the 16-byte log and register D (modifiers), with the Ctrl+Alt hard reset and F12. The comment at
  `evoavr.cpp:236-238` says: "The rest of to_zx (ZX matrix from the AVR keymap …) is not the Z80-visible log: the
  emulator's matrix comes from the host keys directly". The AVR keymap above is **not** applied. The matrix uses the
  host key decomposition (`unreal-qt/src/emulator/keyboardmanager.cpp`, the ZX key enums in
  `core/src/emulator/io/keyboard/keyboard.h:73-95`).
- `core/src/emulator/ports/models/portdecoder_atm3.cpp:54`: the AVR is attached as the PS/2 sink.
- Mismatch to note: `core/src/emulator/io/keyboard/pckey.h:126` and `pckey.cpp:406-415` map, for automation,
  SS -> Left Ctrl "as the ZX-Evo AVR maps them back", Break -> Escape, Edit -> Backquote. In the default AVR keymap
  above, Left Ctrl is unmapped (SS is Right Shift), BREAK is Tab and Escape clears the keys. Only Edit = ` matches.
  An OSD that drives the PC picture through the AVR needs the real map.

## 4. TS-Conf (`MM_TSL`)

**Keyboard.** TS-Conf is an FPGA configuration of the same ZX-Evolution board, so the hardware is that of section 3:
PS/2 mini-DIN keyboard plus the ZX keyboard header. The AVR firmware in tslabs/zx-evo `pentevo/avr/current` is the one
that goes with TS-Conf, so the keymap of section 3 applies.

**unreal-ng.** `core/src/emulator/ports/models/portdecoder_tsconf.cpp:104`: the same `EvoAvr` as PS/2 sink, with the
same matrix-from-host behaviour.

## 5. ZXM-Phoenix v1.0 (`MM_PHOENIX`)

**Keyboard.** Mick (Mikhail Tarasov), 2008-2009, design discussed on zx-pk.ru. Manual `ZXM-Phoenix_manual_v03.pdf`,
§5 "Клавиатура": "We decided not to fit a traditional keyboard controller [here: a matrix keyboard], because the
standard keyboard is hard to get. The answer is a PS/2 keyboard that the atmega8515 microcontroller program
(by Caro) turns into the standard keyboard. The keyboard plugs into connector XS2." Feature list: "Клавиатура –
внешняя PS2 (эмулируемая)", mouse PS/2 Kempston (XS3). Photo `phoenix-rev01-board-ps2-sockets.jpg` (micklab.ru,
rev 01, 2010) shows two PS/2 mini-DIN sockets at the bottom left and the ATmega8515 labelled "ZXM Phoenix Multi Card
Bios v2.7 by Caro". The era is 2009+, so a PS/2 104-key keyboard. **No ZX matrix keyboard connector** is mentioned in
the manual. Whether the board has one: UNVERIFIED. The ZX picture is then the emulated matrix, not a physical
keyboard the machine had.

**Mapping.** ZX Multi Card 2 firmware `MC2PH_27.HEX` (v2.7 for Phoenix, Kamil Karimov, 2009-04-11; techref archive at
pk8000.narod.ru). Read off the image here: a 128-byte table at flash `0x16FA`, indexed by the set-2 code. In the low
6 bits it holds `row*8 + bit + 1`. It matches every letter, digit, SPACE and ENTER (A = `09`, Q = `11`, 1 = `19`,
P = `29`, ENTER = `31`, SPACE = `39`), and KP0-KP9 give the digits.

The other entries carry flag bits 6-7 (`0x40`, `0x80`, `0xC0` + index). Probably `0x40` = SS+key and `0x80` = CS+key:
Backspace `A1` = CS+0, Tab `99` = CS+1, KP. `7B` = SS+M. `0xC0+n` looks like an index into a second table for the
symbol keys (`, . / ; - ' [ = ]` = `C1`..`C9`). The modifiers do not fit simply (Left Shift `3A`, Left Alt `01`,
Right Shift `00`), and neither do the F-keys. All of this beyond the letters and digits is UNVERIFIED and would need
the AVR code disassembled. Service keys from the manual: WinMenu+F1 / F2 / F3 run the controller's built-in programs
(F2 is the mouse test), WinMenu+1..4 set the mouse resolution, WinMenu+Pause enters the loader (`MC2PH_27.TXT`).

**unreal-ng.** `MM_PHOENIX` is in the model table (`core/src/emulator/config.h:73`) and the ROM layout
(`core/src/emulator/memory/rom.cpp:147, 295`), but `PortDecoder::IsModelSupported` (`core/src/emulator/ports/portdecoder.cpp:126-153`)
and the factory `GetPortDecoderForModel` (`:155-233`, default case throws) have no `MM_PHOENIX` case. The machine cannot be created. The design
`docs/inprogress/2026-10-07-phoenix/design.md:30` plans `#FE` "as the Pentagon decoder": a plain matrix, no PS/2
controller model. The PC picture then maps to ZX keys through the host mapping, not through the ZXMC2 table.

## 6. Sprinter Sp2000 (`MM_SPRINTER`)

**Keyboard.** Peters Plus `sp-guide.pdf`: "AT Keyboard controller". Standard configuration: "AT Mini Tower case with AT
power supply, Sp2000 Mainboard, FDD 3.5", AT keyboard, MS mouse". The 2003 site (`web-2003/arhitecture.htm`): "Keyboard
controller 101key/AT". The main configuration includes the "AT keyboard controller (built on an internal serial port of
the processor)". Photo `sprinter-sp2000-board-din-keyboard-socket.jpg` (local `schematics/photos/board2000.jpg`) shows
a Baby-AT style board with a large round socket at the top right edge, next to the AT power connector. That is the
5-pin DIN keyboard socket (read from the photo, the type is not labelled). Era: 1999-2003, a beige 101/102-key AT
keyboard.

**What the machine reads.**
1. Raw set-2 codes on SIO channel A of the Z84C15. BIOS SETUP and DSS translate them themselves. That is the PC
   keyboard proper.
2. The PLD (`KBD.TDF`) decodes the same stream into a ZX matrix through a **256x8 RAM table** that software can
   rewrite (`lpm_ram_dq … lpm_file="KBD_INI2.MIF"`, `WR_KBD`). The initial table is `KBD_INI2.MIF` (Peters Plus
   `sprinter-computer-hard/ACEX`; identical in the `sp97-flex` sources; the Quartus 2 copy decodes to the same
   entries).

Table byte: bits 7-6 the extra shift (`00` = FN, the 6th data line KD5 on A15; `01` = Caps Shift; `10` = Symbol Shift
(the `C0` entries "DATA FOR CAPS": FN, Left Shift, Ctrl); `11` = none), bits 5-3 the half-row, bits 2-0 the bit
(0-5). Decoded by script, from `KBD_INI2.MIF`:

| PC key (set 2) | ZX | | PC key | ZX |
|:--|:--|:--|:--|:--|
| letters, digits | the same | | F1-F10 | FN+1 .. FN+0 (the Sprinter's 6th line) |
| Left Shift (12), Right Shift (59) | CS | | F11 (78), F12 (07) | none (F12 = PLD turbo toggle) |
| Ctrl (14) | SS | | Alt (11) | CS+SS |
| Tab (0D) | CS+1 (EDIT) | | ` (0E) | SS+A |
| Caps Lock (58) | CS+2 | | Esc (76) | CS+SP (BREAK) |
| Backspace (66) | CS+0 | | Enter (5A) | ENTER |
| , (41) . (49) / (4A) | SS+N, SS+M, SS+V | | ; (4C) ' (52) | SS+O, SS+P |
| - (4E) = (55) | SS+J, SS+L | | [ (54) ] (5B) \ (5D) | SS+Y, SS+U, SS+D |
| Left Win (E0 1F) | CS+ENTER | | Right Win (E0 27) | SS+ENTER |
| Menu (E0 2F) | SS+SP | | Num Lock (77) | FN + invalid bit (no key) |
| Scroll Lock (7E) | FN+CS | | | |
| KP4 / Left (6B) | CS+5 | | KP6 / Right (74) | CS+8 |
| KP8 / Up (75) | CS+7 | | KP2 / Down (72) | CS+6 |
| KP7 / Home (6C) | SS+Q | | KP1 / End (69) | SS+E |
| KP0 / Ins (70) | SS+W | | KP. / Del (71) | CS+9 |
| KP9 / PgUp (7D) | CS+3 | | KP3 / PgDn (7A) | CS+4 |
| KP5 (73) | SS+I | | KP+ (79), KP- (7B), KP* (7C) | SS+K, SS+J, SS+B |

The PLD ignores E0 for the table index, so the grey keys equal their keypad twins. PLD actions: Ctrl+Alt+`#71`
(Delete / KP.) pulls /RESET; F12 alone toggles turbo, and so does every typematic repeat of it.

**unreal-ng.**
- `core/src/emulator/ports/models/sprinter/sprinterinput.h:9-45, 80`: `SprinterInput` is the PS/2 sink. Set-2 bytes go
  to SIO A with real timing and FIFO overrun. The PLD's Ctrl / Alt / Shift flags, the Ctrl+Alt+Del reset and the F12
  turbo toggle are emulated (`:33-45`). But "Here the matrix comes from the host's ZX key events" (`:26-28`): the
  `KBD_INI2.MIF` table is **not** applied, and the FN line (F1-F10 = FN+digit) is not produced by PC keys.
- `core/src/emulator/ports/models/portdecoder_sprinter.cpp:155`: attached as the sink. Host route `Both`
  (`keyboard.cpp:121-129`).

## 7. PC keyboard styles

The unit u = 19.05 mm key pitch, keycap tops about 12.7 mm (0.5 in) square on 1u. ANSI 101/104 geometry is the
keyboard-layout-editor preset (ijprest/keyboard-layout-editor `layouts.json`, "ANSI 104", "ANSI 104 (big-ass enter)",
"ISO 105"). XT and AT are measured from the deskthority photos (±0.1u).

### 7.1 IBM PC/XT 83-key (Model F, 1981), the XT-firmware ATM 7.00 era

Photo `xt-83-ibm-model-f-top.jpg` (German variant; same geometry as the US one). Measured from the photo:

| Row | Keys left to right (width in u) |
|:--|:--|
| Function block (left, 2 columns x 5 rows, 0.5u gap to the main block) | F1 F2 / F3 F4 / F5 F6 / F7 F8 / F9 F10 (each 1u) |
| 1 | Esc 1, `1`..`=` 12 x 1, Backspace (←) ~1.75 · Num Lock 2, Scroll Lock 2 |
| 2 | Tab 1.5, Q..] 12 x 1, Enter (↵) 1u wide, 2 rows tall (rows 2-3) · KP7 KP8 KP9 KP- |
| 3 | Ctrl 1.75, A..' 11 x 1, ` 1, (Enter) · KP4 KP5 KP6, KP+ 1u wide, 2 rows tall (rows 3-4) |
| 4 | Left Shift 1.25 (US 2u: `\` sits right of `'` on US), `\` 1, Z../ 10 x 1, Right Shift 1.75, PrtSc/* 1 · KP1 KP2 KP3 |
| 5 | Alt 1.75, Space ~9.75, Caps Lock 2 · KP0/Ins 2, KP./Del 2 |

No separate cursor or navigation block: the keypad doubles as cursors (Num Lock). No lock LEDs. US 83-key specific
positions (backslash, backquote): UNVERIFIED against a US photo (the downloaded one is German).

Colours (photo): case pebble beige, very light warm grey. Alphanumeric caps a lighter off-white grey. Function,
modifier and keypad caps a darker warm grey. Legends black, small sans (Helvetica-like), top-left on the alphas,
centred on the modifiers, with symbols (⇧, ⇥, ←, ↵) rather than words for Shift, Tab and Backspace. Caps (deskthority
Model F): one profile for every row, the curved plate gives the sculpture; dye-sublimated PBT.

### 7.2 IBM PC/AT 84-key (Model F AT, 1984)

Photo `at-84-ibm-model-f-top.jpg` (US). Function block as XT (F1-F10, 2x5 on the left).

| Row | Keys (u) |
|:--|:--|
| 1 | `` ` `` 1, 1..= 12 x 1, `\` 1, Backspace (←) 1 · Esc, Num Lock, Scroll Lock, Sys Req (4 x 1) |
| 2 | Tab 1.5, Q..] 12 x 1, Enter: L-shaped ("big-ass"), 1.5u top, spans rows 2-3, bottom 2.25 · KP7 KP8 KP9, PrtSc/* |
| 3 | Ctrl 1.75, A..' 11 x 1, Enter (bottom part) · KP4 KP5 KP6, KP- |
| 4 | Left Shift 2.25, Z../ 10 x 1, Right Shift 2.75 · KP1 KP2 KP3, KP+ 2 rows tall (rows 4-5) |
| 5 | Alt 1.5, gap ~1, Space ~9 (from photo), gap ~1, Caps Lock 1.5 · KP0/Ins 2, KP./Del 1 |

Three lock LEDs (Caps, Num, Scroll) in a window at the top right. Colours as the XT: case pebble beige; alphas
off-white; F-keys, modifiers, Enter, Backspace, Esc, the lock keys and the keypad operators darker grey; keypad digits
light. Legends black.

### 7.3 Enhanced 101/102 key (IBM Model M 1985 and the 1990s clones), the AT-firmware ATM, Sprinter era

Geometry (KLE "ANSI 104" without the Windows keys; Model M has Ctrl 1.5, Alt 1.5, Space 7, with a 1u gap left and
right):

| Row | Keys (u) |
|:--|:--|
| F row | Esc 1, gap 1, F1-F4, gap 0.5, F5-F8, gap 0.5, F9-F12, gap 0.25, PrtSc ScrLk Pause; then a 0.5u vertical gap |
| 1 | `` ` `` 1..= 13 x 1, Backspace 2 · gap 0.25 · Ins Home PgUp · gap 0.25 · NumLk / * - |
| 2 | Tab 1.5, Q..] 12 x 1, `\` 1.5 · Del End PgDn · KP7 KP8 KP9, KP+ (2 tall) |
| 3 | Caps 1.75, A..' 11 x 1, Enter 2.25 · (3.5 gap) · KP4 KP5 KP6 |
| 4 | Shift 2.25, Z../ 10 x 1, Shift 2.75 · gap 1.25, Up, gap 1.25 · KP1 KP2 KP3, KP Enter (2 tall) |
| 5 | Ctrl 1.5, (1), Alt 1.5, Space 7, Alt 1.5, (1), Ctrl 1.5 · Left Down Right · KP0 2, KP. |

Variants:
- **104 (Windows, 1995+, typical PS/2 keyboard of the ZX-Evo / Phoenix era):** bottom row Ctrl, Win, Alt 1.25 each,
  Space 6.25, Alt, Win, Menu, Ctrl 1.25 each.
- **ISO 102/105:** Enter 1.5 top / 1.25 bottom over two rows, Left Shift 1.25 plus an extra key 1u (`\|`, PS/2 set-2
  `61`, `PcKey::IntlBackslash`).
- **"Big-ass Enter" ANSI** (KLE preset): `\` moves next to Backspace (1u each), Enter is L-shaped over rows 2-3. Many
  1990s Russian / Asian clone keyboards used it. Photo `russian-ps2-genius-cyrillic-legends.jpg` has this Enter with
  `\` next to Backspace.

Colours of the 1990s clones (photos `russian-ps2-genius-cyrillic-legends.jpg`, `genius-kb-06xe-beige.jpg`): case and
all keycaps one colour, off-white / light beige (budget membrane boards, no darker modifiers; the IBM-like two-tone
grey modifiers belong to Model M and its Cherry / BTC copies). Legends:
- Latin black, upper left;
- Cyrillic **red**, lower right on the letter keys (Й on Q … Э on ', Ё on `);
- the Russian shifted punctuation in red on the digit row: `"` (2), `№` (3), `;` (4), `:` (6), `?` (7); `.` `,` on the
  `/` key; `\ /` on `\`;
- three LEDs (Num, Caps, Scroll) at the top right.

Keycap profile: cylindrical, sculpted by row (OEM-like).

Russian ЙЦУКЕН legends (Windows layout, matches the photo): Q Й, W Ц, E У, R К, T Е, Y Н, U Г, I Ш, O Щ, P З, [ Х,
] Ъ, A Ф, S Ы, D В, F А, G П, H Р, J О, K Л, L Д, ; Ж, ' Э, Z Я, X Ч, C С, V М, B И, N Т, M Ь, , Б, . Ю, / `.`, ` Ё.

Typical brands in Russia in the 1990s / 2000s (BTC, Mitsumi, Chicony, Cherry, Genius/KYE, later Sven, Defender):
common knowledge, no source. UNVERIFIED. Red as the Cyrillic colour is in both photos, but blue or green also
existed. UNVERIFIED how common.

### 7.4 Soviet MS 7004 (for the MC04(XT) adapter on ATM)

Photo `ms7004-soviet-keyboard.jpg` (Wikimedia Commons). A DEC LK201 derivative: dark brown/black case, off-white
alphanumeric caps, grey function / edit caps. Legends black, Cyrillic upper and Latin lower on the same cap. Special
keys РУС/ЛАТ, ФКС, СУ, КМП, ТАБ; a function strip with labels (ПАУЗА, УСТ. РЕЖИМА …). A possible third, exotic ATM
picture. Its layout geometry is not researched here. UNVERIFIED.

## 8. Photos saved (`photos/pc/`)

| File | What | Source |
|:--|:--|:--|
| `zxevo-board-top.jpg` | ZX-Evolution board, top | http://nedopc.com/zxevo/zxevo_top_c.jpg |
| `zxevo-board-front-connectors.jpg` | ZX-Evo rear: PS/2 keyboard (purple) / mouse (green) stack | http://nedopc.com/zxevo/zxevo_front_c.jpg |
| `phoenix-rev01-board-ps2-sockets.jpg` | ZXM-Phoenix rev 01: two PS/2 sockets, MC2 ATmega8515 | http://micklab.ru/file/zxm_phoenix/img/zxm_phoenix_1.jpg |
| `phoenix-board-rev-photo5.jpg`, `phoenix-board-rev-photo12.jpg` | later Phoenix board revisions | micklab.ru `zxm_phoenix_5.jpg`, `_12.jpg` |
| `sprinter-sp2000-board-din-keyboard-socket.jpg` | Sp2000 board with the round DIN keyboard socket | local `sprinter/schematics/photos/board2000.jpg` |
| `xt-83-ibm-model-f-top.jpg` | IBM PC/XT Model F, German | https://deskthority.net/wiki/images/8/84/IBM_XT_top.JPG |
| `at-84-ibm-model-f-top.jpg` | IBM PC/AT Model F, US | https://deskthority.net/wiki/images/9/90/IBM_Model_F_AT.jpg |
| `russian-ps2-genius-cyrillic-legends.jpg` | Genius beige Russian keyboard, red Cyrillic, L-Enter | Commons `File:Genius russian keyboard.jpg` |
| `genius-kb-06xe-beige.jpg` | Genius KB-06XE Ukrainian/Russian, red Cyrillic | Commons `File:KYE Systems Genius KB-06XE-0534.jpg` |
| `ms7004-soviet-keyboard.jpg` | Soviet MS 7004 | Commons `File:Keyboard MS 7004 (14906723198).jpg` |

Not saved: a photo of a real ATM-Turbo 2+ with its keyboard (Commons `File:Computer_board_atmturbo2.jpg` exists, but
the download hit a Wikimedia rate limit), and an ES-1841 XT keyboard (Commons `File:Раскладка клавиатуры ЕС1841.jpg`,
same). Licences of the Commons files were not recorded (rate limit). Check them before any image goes into the repo.

## 9. Sources

Local:
- `zx-spectrum-data/atm-turbo/keyboard-controller/at-keyboard-v2.2/extracted/atm_xt.txt` (CP866):
  XT controller modes, mode-0 table, commands
- `…/at-keyboard-v4.1/extracted/atm_at41.asm` (`L_527` Spectrum table, `at2xt`, `L_582`, Ctrl+Alt+Del, Pause, RUS/LAT
  commands), `at41.txt`, `…/at-keyboard-v4.0/extracted/at40.txt`
- `zx-spectrum-data/atm-turbo/README.md`, `klug-bbs/extracted/ATMSRC/ATMSRC/READ.ME`
- `zx-spectrum-data/sprinter/bios/upstream/sprinter-computer-hard/ACEX/KBD.TDF`, `KBD_INI2.MIF`
- `zx-spectrum-data/sprinter/documentation/peters-plus/sp-guide.pdf`, `web-2003/arhitecture.htm`,
  `schematics/photos/board2000.jpg`
- repo: `core/src/emulator/io/keyboard/{atm2kbc.h,atm2kbc.cpp,pckey.h,pckey.cpp,keyboard.h,keyboard.cpp}`,
  `core/src/emulator/memory/atm/{evoavr.h,evoavr.cpp}`,
  `core/src/emulator/ports/models/{portdecoder_atm710.cpp,portdecoder_atm450.cpp,portdecoder_atm3.cpp,portdecoder_tsconf.cpp,portdecoder_sprinter.cpp}`,
  `core/src/emulator/ports/models/sprinter/sprinterinput.h`, `core/src/emulator/ports/portdecoder.cpp`,
  `core/src/emulator/config.h`, `docs/inprogress/2026-10-07-phoenix/design.md`

Web:
- ATM-turbo description (board table, ports, XT/AT controller, CP/M codes): http://atmturbo.nedopc.com/atmdscr.htm;
  firmware page http://atmturbo.nedopc.com/atmshem.htm
- ZX-Evolution: http://nedopc.com/zxevo/zxevo_eng.php, http://nedopc.com/zxevo/zxevo.php
- ZX-Evo AVR firmware: https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/kbmap.c, `zx.c`, `ps2.c`
- ZXM-Phoenix: http://micklab.ru/My%20Computer/ZXMPhoenix.htm (history, specs "Клавиатура – внешняя PS2
  (эмулируемая)"), https://pk8000.narod.ru/zx/phoenix.html with `zxm-phoenix_manual_by_mick.7z` (manual v03 §5) and
  `zxm-phoenix_techref_by_mick.7z` (`MC2PH_27.zip`: HEX + TXT); forum
  https://zx-pk.ru/threads/9310-kompyuter-quot-zxm-phoenix-quot-klon-rozhdennyj-na-forume.html (not read)
- deskthority: https://deskthority.net/wiki/IBM_Model_F (XT / AT notes, keycap profile, PBT), image files above
- keyboard-layout-editor presets: https://raw.githubusercontent.com/ijprest/keyboard-layout-editor/master/layouts.json
- Wikimedia Commons files named in section 8

## 10. UNVERIFIED (summary)

1. ATM-Turbo v4.50: which ZX keyboard / case was usual, and what the "extended 64-key matrix" looked like.
2. ATM-Turbo 2+ v7.10: the keyboard socket type (5-pin DIN assumed) and a photo of a real setup. Which keyboards
   people used (XT EC-1841-type, MS 7004 via adapter, AT clones) is inferred from the firmware set, not from photos.
3. ZXM-Phoenix: whether the board has a ZX matrix keyboard header. The ZXMC2 map beyond letters, digits, Space,
   Enter and the keypad digits (modifiers, symbols, F-keys, flag-bit meaning).
4. Sprinter: the DIN socket type is read from a photo, not from a document. Whether any BIOS / DSS rewrites the PLD
   keyboard RAM at boot (then the table in force differs from `KBD_INI2.MIF`).
5. XT 83-key widths (Backspace ~1.75, Space ~9.75) and AT Space ~9 are measured from photos (±0.1u). US XT backslash
   and backquote positions are not checked against a US photo.
6. Brands and the Cyrillic legend colour distribution of 1990s Russian keyboards. The red is confirmed in two photos
   only.
7. Licences of the downloaded Commons / deskthority / micklab / nedopc images.
