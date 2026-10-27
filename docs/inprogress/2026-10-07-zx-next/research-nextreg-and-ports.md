# Research: NextREG space and port map

**Date:** 2026-10-07 · part of [README.md](README.md)

Read in full: `nextreg.txt` and `ports.txt` of
[ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) (nextreg header: "core3.1.5 20200427"
and "Core 3.01.05"). Read in the parts named: MAME
[`specnext.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/next/specnext.cpp) (register
write/read cases, port-enable helpers, ROM list). **Not read:** the FPGA repository's own `nextreg.txt` of the
current core (3.02.04), see Q2 in [requirements.md](requirements.md). So section 3 lists where the sources are
known to stop agreeing, not what the missing registers do.

The implementation holds these as **one data table** (register number, name, reset values for soft and hard
reset, readable / writable, access handler) next to the port table, so a report can print them and a test can
walk them ([design-core.md](design-core.md) section 3).

## 1. NextREG list (core 3.1.5), grouped

R = readable, W = writable, "cfg" = only in configuration mode (NR `#03` bits 2:0 = 0).

| NR | Name | Notes |
|:--|:--|:--|
| `#00` | Machine ID | `#0A` Next; `#08` "emulators"; `#EA` ZX-DOS platform; `#FA` anti-brick |
| `#01`, `#0E` | Core version, sub-minor | |
| `#02` | Reset | W bit 1 hard, bit 0 soft, bit 7 holds the expansion bus / ESP reset; R tells which reset happened |
| `#03` | Machine type | W: bits 2:0 machine (cfg only), 6:4 display timing (bit 7 allows), bit 3 toggles the user timing lock; a write disables the bootrom in config mode |
| `#04` | Config mapping | cfg: 16K SRAM bank at `#0000` (first 1 MB) |
| `#05`, `#06`, `#08`, `#09`, `#0A` | Peripheral 1-5 | joystick modes (3 bits each), 50/60 Hz, scandoubler; hotkeys, DivMMC automap enable, multiface NMI, PS/2 mode, YM/AY; unlock `#7FFD`, contention off, ABC/ACB, internal speaker, DACs, `#FF` read, TurboSound, issue-2 keyboard; per-AY mono, sprite id lockstep, DivMMC MAPRAM reset, scanline weight; mouse buttons and DPI |
| `#07` | CPU speed | 0..3 = 3.5 / 7 / 14 / 28 MHz; R shows programmed and actual |
| `#10` | Core boot | W bit 7 starts core id 4:0 (cfg); R shows the DRIVE / M1 buttons |
| `#11` | Video timing | cfg: VGA 0-6, HDMI 7; sets the 28 MHz clock (28.0 / 28.571 / 29.464 / 30 / 31 / 32 / 33 / 27 MHz) |
| `#12`, `#13` | Layer 2 active / shadow bank | 16K bank number, reset 8 and 11 |
| `#14` | Global transparency | reset `#E3`, compared with the top 8 bits of the 9-bit colour |
| `#15` | Sprite and layers | lores enable, sprite priority, clipping in over-border mode, layer order 4:2, over-border, enable |
| `#16`, `#17`, `#71` | Layer 2 scroll X LSB, Y, X MSB | |
| `#18`-`#1B` | Clip windows: Layer 2, sprites, ULA / LoRes, tilemap | four writes X1 X2 Y1 Y2 each; reset 0,255,0,191 (tilemap 0,159,0,255); `#1C` resets the indices and reads them back |
| `#1E`, `#1F` | Active video line MSB / LSB | |
| `#22`, `#23` | Line interrupt control and value | bit 2 disables the ULA interrupt, bit 1 enables the line interrupt |
| `#26`, `#27` | ULA scroll X, Y | |
| `#28`-`#2B` | PS/2 keymap address and data | write to `#2B` auto-increments (the firmware loads `keymap.bin` here) |
| `#2C`-`#2E` | DAC B / A+D / C mirrors | reads give I2S samples from the Pi |
| `#2F`-`#31` | Tilemap X scroll MSB / LSB, Y | |
| `#32`, `#33` | LoRes scroll X, Y | |
| `#34`-`#39`, `#75`-`#79` | Sprite number and attributes 0-4 | `#75`-`#79` auto-increment the sprite number |
| `#40`-`#44` | Palette index, 8-bit value, ULANext format, control, 9-bit value | eight palettes, auto-increment, ULANext mask |
| `#4A`-`#4C` | Fallback colour, sprite transparency index, tilemap transparency index | |
| `#50`-`#57` | MMU slots 0-7 | reset 255, 255, 10, 11, 4, 5, 0, 1 |
| `#60`-`#64` | Copper data, address LSB, control, 16-bit data, vertical line offset | |
| `#68`-`#6C`, `#6E`, `#6F` | ULA control, display control 1, LoRes control, tilemap control and default attribute, tilemap and tile-definition base | |
| `#70` | Layer 2 control | resolution 5:4, palette offset 3:0 |
| `#7F` | User register 0 | reset `#FF` |
| `#80`-`#8A` | Expansion bus enable and control, internal / expansion port decode enables (`#82`-`#85`, `#86`-`#89`), IO propagate | bit 31 of the 32-bit enable word selects soft or hard reset for it |
| `#8C`, `#8E` | Alternate ROM, spectrum memory mapping | |
| `#90`-`#9B`, `#A0`, `#A2`, `#A8`, `#A9` | Pi GPIO, Pi peripheral enable, I2S control, ESP GPIO | stubs for us |
| `#B0`, `#B1` | Extended keys 0 and 1 | |
| `#FF` | reserved | |

## 2. Port map (ports.txt), grouped

| Port | Function |
|:--|:--|
| `#FE` (A0 = 0) | ULA: keyboard rows by high byte, EAR in bit 6; write border 2:0, MIC 3, EAR 4 |
| `#FF` | Timex video mode (hi-colour, hi-res, screen select, colour), floating bus when NR `#08` bit 2 is 0 |
| `#7FFD`, `#DFFD`, `#1FFD` | 128K and +3 paging; `#DFFD` bit 7 enables Pentagon-512 when Pentagon timing is on; `#DFFD` bits 3:0 extend the bank number to 7 bits |
| `#243B`, `#253B` | NextREG select and data |
| `#103B`, `#113B` | I2C SCL and SDA |
| `#123B` | Layer 2 control (map type 7:6, shadow 3, read map 2, display 1, write map 0; with bit 4 set: bank offset) |
| `#133B`, `#143B`, `#153B` | UART transmit / status, receive / prescaler, control |
| `#183B`-`#1B3B` | CTC channels 0-3 (jnext FPGA notes; not in `ports.txt`, listed in the FPGA repo analysis) |
| `#BF3B`, `#FF3B` | ULA+ register and data |
| `#303B` | Sprite slot select, status read (collision, overtime), `#57` attributes, `#5B` patterns |
| `#0B`, `#6B` | z80dma and zxnDMA; the port used selects the DMA mode |
| `#FFFD`, `#BFFD` | AY register (also chip select 7:0 pattern `#9C` base) and data |
| `#1F`, `#F1`, `#3F`, `#0F`, `#F3`, `#DF`, `#FB`, `#B3`, `#4F`, `#F9`, `#5F` | DAC A / B / C / D aliases (GS covox, Pentagon / ATM, SpecDrum, Soundrive 1 and 2, Covox, Profi Covox) |
| `#E7`, `#EB` | SPI chip select and data |
| `#E3` | DivMMC control |
| `#FBDF`, `#FFDF`, `#FADF` | Kempston mouse X, Y, wheel + buttons |
| `#1F`, `#37` | Kempston / MD joystick 1 and 2; `#37` write is the joystick I/O mode |
| Multiface | `#1F` / `#9F` (MF1), `#3F` / `#BF` (MF128), `#BF` / `#3F` (+3 type) |

Precedence notes from the file: `#DFFD` over the AY; `#F1` over the `XXFD` AY decode; `#F9` over `XXFD`.
`#1F` is shared by Kempston joystick 1, DAC A and Multiface 1, so the exact decode (A7, A6 and which
internal enable bits are on) is a design item in [design-core.md](design-core.md) section 4.

## 3. Beyond the 3.1.5 list: what MAME handles

MAME's `specnext.cpp` has write cases for NR `#0B` (joystick I/O mode: bit 7 enable, bits 5:4 mode, bit 0
parameter), `#0F`, `#20`, `#8F` (mapping mode: Pentagon 512 / 1024, Profi), `#B2`, `#B8`-`#BB` (DivMMC
entry-point enables, ROM-3-only conditions, timing), `#C0` (IM2 vector bits 7:5, stackless NMI bit 3,
pulse / IM2 mode), `#C2`, `#C3` (RETN address), `#C4`-`#C6`, `#CC`-`#CE` (interrupt enables for ULA, line, CTC,
UARTs, DMA), `#C8`-`#CA` (interrupt status), `#D8`-`#DA` (+3 FDC I/O traps: enable, written value, cause),
`#F0` (xdev command), `#F8`-`#FA` (XADC). jnext's feature list adds the Pentagon-1024 mode and
`NR 0x0B` joystick UART. What `#0F`, `#20`, `#B2`, `#F0` do is read from the code in phase N2, not guessed
here. Reads of unknown registers return 0 in MAME (and are logged).

## 4. Open points in the register list

| Point | State |
|:--|:--|
| `#7FFD` written in the +3 port range (`01XX XXXX XXXX XX01`) is "+3 only" | which machine types route it: see `ports.txt` row |
| The `#FF` port reads: Timex mode only when NR `#08` bit 2 is 1, otherwise the floating bus | stated in `nextreg.txt`; floating-bus pattern per machine type is a video-timing item |
| AY select pattern: `#FFFD` write with bits 7:5 = 100 and 4:2 = 111 chooses a chip (bit 6, 5 = left / right enable) | text in `ports.txt`; MAME tests `(data & 0x9c) == 0x9c` when TurboSound is on |
| MMU page numbers vs RAM size | Q4 |
| NR `#09` bit 4 sprite id lockstep | described in `nextreg.txt` |
