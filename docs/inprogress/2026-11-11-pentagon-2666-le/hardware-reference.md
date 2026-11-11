# Pentagon 2.666 LE: hardware reference (from the sources)

Everything below is read from the upstream sources, revision
[`e808402`](https://github.com/koe1234/pentagon_2_666_le/tree/e808402ff193491b94cfd3733f3e1f045c2aca6c) (2025-04-20):

| Short name | Upstream path | What it is |
|---|---|---|
| `PaE.vhd` | `LE/PAE_FPGA/PaE.vhd` | FPGA top: CPU, memory mapper, all ports, turbo, INT, sound mix, CMOS, SD |
| `zx_main.vhd` | `common/zx_main.vhd` | Video controller: raster counters, all video modes, INT strobe, AY clock enable |
| `betadisk.vhd` | `common/betadisk.vhd` | Hardware Beta Disk emulator: a WD1793 imitation over a RAM disk |
| `YM2149_linmix.vhd` | `common/YM2149/YM2149_linmix.vhd` | AY/YM core (MikeJ derivative), two instances (TurboSound) |
| `uart.vhd` | `common/uart.vhd` | 16550-like UART |
| `pollitra.hex`, `otmfnt.hex` | `common/` | Default palette (16 x 16 bit), text-mode font (2 KB) |
| `pae_mcu.c`, `ps2/ps2.c` | `LE/PAE_MCU/Firmwarez/` | LPC2368 firmware: FPGA configuration, ROM upload, PS/2, RTC, CMOS persistence |
| `rom/*.rom` | `LE/PAE_MCU/rom/` | ROM set: `basic48`, `basic128`, `trdos` (TR-DOS 6.11E), `gluk` (Gluk Reset Service), `fatall` (FATALL v0.25), `logo` (6912-byte screen) |
| TFM | `tfm_le.pdf` | User manual, document version 20.04.2025 (Russian), 33 pages |

`PaE.vhd:1031` means line 1031 of that file at this revision. Where the manual and the VHDL disagree, the VHDL is
what the shipped bitstream (`PaE.rbf`) does and what we emulate (owner decision 2026-11-11: everything as the
hardware, nothing improved); the disagreements are collected in §11.

Signal polarity: the VHDL `dos` signal is active low (`dos = '0'` means "DOS / shadow mode on"). This document says
"DOS on" / "DOS off" and never uses the raw level.

## 1. Clocks and frame

| Item | Value | Source |
|---|---|---|
| MCU quartz | 12 MHz (manual §6, ISP settings) | TFM |
| MCU core clock | 12 x 24 / 2 / 4 = 72 MHz (`PLLCFG = 23 \| 1 << 16`, `CCLKCFG = 3`) | `pae_mcu.c:972-987` |
| FPGA input clock | the MCU's PWM1.3 output: `CCLK / MR0` = 72 / 4 = **18 MHz** (`PWM1MR0 = 4`, `MR3 = 2`: 50 % duty); the FPGA PLL is constrained for the same 18 MHz (`inclk0_input_frequency => 55555` ps). Schematic (manual App. 2): net `FPGA_MAINCLK` runs from MCU pin 73 `P2[2]/PWM1[3]` to FPGA pin 23 (`clk`, `PaE.qsf:152`); the BOM lists no oscillator besides ZQ1 12 MHz and ZQ5 32768 Hz | `pae_mcu.c:103, 917-933`, `PLL1.VHD:139-150`, TFM |
| `mainclk` | 18 x 19 / 3 = **114 MHz** (`uart_clk_frequency = 114000000` agrees) | `PLL1.VHD`, `PaE.vhd:593` |
| VGA pixel clock | `mainclk / 4` = 28.5 MHz (`main_state(1:0)` 4-phase cycle) | `zx_main.vhd` |
| Raster | 896 x 640 VGA pixels; one ZX pixel = 2 x 2 VGA pixels, one ZX line = 2 VGA lines | `zx_main.vhd:71-72` |
| Frame rate | 114e6 / (896 x 640 x 4) = **49.70 Hz** | derived |
| CPU 3.5 MHz | `mainclk / 32` = 3.5625 MHz | `PaE.vhd:1579-1610` |
| CPU 14 MHz | `mainclk / 8` = 14.25 MHz | same |
| CPU 28 MHz | `mainclk / 4` = 28.5 MHz | same |
| Nominal names | "3.5 / 14 / 28 MHz" in the manual are names: the real clocks are 3.5625 / 14.25 / 28.5 MHz, exactly 1 : 4 : 8. 114 MHz is not a multiple of 3.5 MHz; a true 3.5 MHz would need `mainclk` = 112 MHz | derived |
| Frame in 3.5 MHz T-states | 896 x 640 x 4 / 32 = **71680** = 320 lines x 224 T: Pentagon timing | derived |
| Memory contention | none (SRAM is time-sliced with video in the 4-phase cycle) | `PaE.vhd:1069-1130` |

### 1.1 Turbo

`cpu_speed = (not IOcycle) & xx77.bit3 & EFF7.bit4` (`PaE.vhd:1579`):

| `xx77` bit 3 (turbo1) | `EFF7` bit 4 (turbo0) | CPU clock |
|---|---|---|
| 1 | x | 28 MHz |
| 0 | 0 | 14 MHz |
| 0 | 1 | 3.5 MHz |

- **Every I/O machine cycle runs at 3.5 MHz** regardless of the setting (`not IOcycle` = 0 forces the 3.5 MHz branch).
  At 14/28 MHz an `IN`/`OUT` therefore costs its I/O cycle at the slow clock.
- After reset `xx77 = 0xAB` (bit 3 = 1) and `EFF7 = 0`: **the machine starts at 28 MHz**; the Gluk ROM is expected to
  set the speed it wants. The manual's reset value `%xxxx1011` agrees.
- The Beta Disk emulator stops the CPU clock (`vg_tormoz`) while it moves a byte to or from the RAM disk
  (`PaE.vhd:1612-1630`).

### 1.2 Frame interrupt

| Item | Value | Source |
|---|---|---|
| Strobe position | VGA line 607 (= 640 - 1 - 32), pixel `int_delay + 1`, `int_delay = 704` after configuration | `zx_main.vhd:462-468`, `PaE.vhd:572` |
| Length | 32 CPU clock edges (`int_counter` 31 -> 0); at 14/28 MHz the pulse is 4/8 times shorter in real time | `PaE.vhd:1543-1565` |
| INT to first screen pixel | about 17968 T at 3.5 MHz (Pentagon 128: 17988 T); exact value to calibrate (§1.3) | derived |
| Flag | `FE` read bit 5 = 1 after each strobe; cleared by `OUT (FE), 0x20`; readable only when `EFF7.2 = 0` | `PaE.vhd:1460`, `1558-1561` |
| UART | `INT = vsync_int AND uart_int` (both active low): the UART can hold INT | `PaE.vhd:1566` |
| INT acknowledge data | `0xFF` (IM 2 vector byte) | `PaE.vhd:1493` |

The MCU can move the INT strobe (`int_delay`, commands 15/16) and the border delay (commands 13/14) over I2C
(`PaE.vhd:770-773`); the shipped firmware never sends them (`pae_mcu.c` sets its own `int_delay = 0` and does not
transmit it). Emulate the power-on values.

### 1.3 Raster geometry (VGA pixels, divide by 2 for ZX pixels)

| Region | Horizontal | Vertical |
|---|---|---|
| Total | 896 | 640 |
| Blanked | pixel >= 848 or <= 73 | line >= 608 or <= 16 |
| ZX / Pentagon 16c paper (256 x 192) | 192 .. 703 | 128 .. 511 |
| ATM 320 x 200 / 640 x 200 / text paper | 128 .. 767 | 128 .. 527 |
| Visible border around ZX paper | left 59, right 72 ZX pixels | top 55.5, bottom 48 ZX lines |

The ZX line is fetched into a line buffer while the previous VGA line pair is scanned (`multic_buf*`,
`border_buf*` double buffers in `zx_main.vhd`); `screen_delay` and `border_delay` (both 0) shift the read-out.
The exact INT-to-pixel offset therefore needs a calibration test (border-stripe and multicolor programs, see
[test-plan.md](test-plan.md#t-v-video)) rather than arithmetic.

## 2. Memory

2 MB SRAM = 128 pages of 16 KB, physical address `page[6:0] & A[13:0]` (`PaE.vhd:1053`).

### 2.1 Physical page map at power-on (MCU upload, `pae_mcu.c:381-393`)

| Page | Content | Write-protected (`romprotect`, `PaE.vhd:1066`) |
|---|---|---|
| `0x00..0x3F` | RAM, Pentagon-1024 range (reachable through `7FFD`) | no |
| `0x05` | boot logo (`logo.rom`, 6912 bytes) shown while the MCU uploads | no |
| `0x40..0x47` | RAM | no |
| `0x48` | Gluk Reset Service | yes |
| `0x49` | TR-DOS 6.11E | yes |
| `0x4A` | BASIC 128 | yes |
| `0x4B` | BASIC 48 | yes |
| `0x4C` | FATALL (SD / IDE file manager) | yes |
| `0x4D` | RAM | no |
| `0x4E..0x76` | RAM disk: TR-DOS drive D (Gluk) and the Beta Disk emulator's drive A; grows downwards from `0x76`; `firmware/test.trd` from the SD card is loaded here | no |
| `0x77..0x7F` | RAM | no |

The MCU loads each image from `firmware/<name>` on the SD card if present, else from its own flash.

### 2.2 Windows, two maps

Four 16 KB windows, two maps. The active map is **`7FFD` bit 4** (`atmwindow = 7FFD.4 & A15 & A14`,
`PaE.vhd:1033`). Each of the 8 window registers (`cpuW_M`, W = window, M = map) holds 10 bits:

| Register bits | Meaning |
|---|---|
| 9..8 | `x7F7` writes: data bits 7..6; `xFF7` writes: `11` |
| 7 | 1 = window follows `7FFD` (Pentagon mode), 0 = dispatcher page |
| 6 | 1 = RAM, 0 = ROM (dispatcher mode only) |
| 5..0 | inverted page bits |

Page selection (`PaE.vhd:1042-1052`):

| Register state | Window | Physical page |
|---|---|---|
| bit 7 = 0, bit 6 = 1 (RAM) | any | `NOT(bit8 & bits5..0)` (7 bits) |
| bit 7 = 0, bit 6 = 0 (ROM) | any | `0x48 + 2 x bit0 + bit1` (bits 0 and 1 swapped, not inverted) |
| bit 7 = 1 | 3 (`C000`) | `7FFD.5 << 5 | 7FFD.7 << 4 | 7FFD.6 << 3 | 7FFD.2..0` (`port7ffdadd` is never written: 64 pages) |
| bit 7 = 1 | 2 (`8000`) | 2 |
| bit 7 = 1 | 1 (`4000`) | 5 |
| bit 7 = 1, `EFF7.3 = 0` | 0 (`0000`) | `0x48 + 2 x DOSoff + 7FFD.4`: DOS on -> Gluk / TR-DOS, DOS off -> BASIC 128 / BASIC 48 |
| bit 7 = 1, `EFF7.3 = 1` | 0 | RAM page 0 |

Reset values (`PaE.vhd:1240-1247`): windows 1..3 of both maps = `0x3FF` (Pentagon mode), window 0 of both maps =
`0x3BF` (Pentagon mode, ROM). So after reset the machine is a plain Pentagon whose ROM 0 is Gluk.

Note: the "ROM" in window 0 is `fpga_rs_in`, the `7FFD.4` signal looped out of the FPGA (`fpga_rs_output`, pin 101)
and back in (pin 102) through the ZX-BUS, so an external card can override it. The emulator uses `7FFD.4`.

### 2.3 Dispatcher ports (DOS on only, full 16-bit decode)

| Port | Window | Written value `v` | Register result |
|---|---|---|---|
| `3FF7` / `7FF7` / `BFF7` / `FFF7` | 0 / 1 / 2 / 3 of the active map | bit 7 = mode, bit 6 = RAM/ROM, bits 5..0 inverted page | `11 & v` |
| `37F7` / `77F7` / `B7F7` / `F7F7` | 0 / 1 / 2 / 3 of the active map | inverted page, 8 bits (bit 7 ignored by the page formula) | `v7 v6 0 1 v5..v0`: RAM, dispatcher mode |

`xFF7` RAM pages reach 64 pages (low MB); `x7F7` reaches 128 pages (2 MB). The inactive map is never changed.

### 2.4 Readback ports `xxBE` (any mode, full 16-bit decode, `PaE.vhd:1516-1524`)

`00BE..03BE` return windows 0..3 of map 0, `04BE..07BE` windows 0..3 of map 1, value `NOT(register bits 7..0)`:

- after an `xFF7` write with RAM page `p`: returns `p` in bits 5..0, bits 7..6 = inverted mode and RAM/ROM bits;
- after an `x7F7` write: returns `10 & p[5..0]`; **page bit 6 is lost** (it lives in register bit 8).

### 2.5 Pentagon-1024 port `7FFD` (`PaE.vhd:1268-1277`)

Decode: `A15 = 0`, `A1 = 0` (and `A2 = 1`, see §4 "IDE gap"). Write only.

| `EFF7.2` | DOS | Effect of a write |
|---|---|---|
| 0 (1024 mode) | any | all 8 bits stored; no lock; bits 5, 7, 6 are page bits 5, 4, 3 |
| 1 (128 mode) | on | all 8 bits stored, lock ignored ("in DOS 1 MB anyway") |
| 1 (128 mode) | off | bits 5..0 stored only while bit 5 (lock) is 0; bits 7..6 keep their old value |

The page formula always includes bit 5, so in 128 mode a locked machine maps page `32 + 7FFD.2..0` at `C000`
(§11, D-3). Bit 3 selects the screen (page 5 / 7, and pages 1/5 or 3/7 in ATM modes) in **all** video modes.

## 3. DOS / shadow logic (`PaE.vhd:1207-1226`)

| Event | Effect |
|---|---|
| CPU reset | DOS on (`dos <= '0'`), `cpm <= 0`, `BF.0 <= 0` |
| M1 fetch in `3D00..3DFF` while `7FFD.4 = 1` | DOS on (checked on the address only, also when window 0 holds RAM or a dispatcher page) |
| M1 fetch at `4000..FFFF` | DOS := `dos_1` |
| End of every I/O cycle | `dos_1 := cpm AND NOT BF.0` (as a "DOS off" level) |

- `cpm` is `A9` of the last `xx77` write. With `cpm = 0` DOS never switches off: this is the ATM CP/M mode, and it
  is the state **after reset**, so Gluk must write `xx77` with `A9 = 1` before it runs code in RAM with DOS off.
- `BF.0 = 1` (port `xxBF`, any mode) keeps DOS on in RAM: the Evo BaseConf "shadow ports" switch. Unlike BaseConf
  it also keeps the DOS ROM mapping, because the same signal drives the ROM page (§2.2).

Ports gated by DOS: `xx77`, `xFF7`, `x7F7`, all Beta Disk ports, the `7FFD` lock bypass (§2.5), the SD port
layout (§5.2) and the palette (§6.2).

## 4. Port map

All internal write decodes require `A1 OR A2 = 1` (`ebl`, `PaE.vhd:1658`): ports with `A2 = A1 = 0` belong to the
external Nemo IDE (§5.3). "Full" = all 16 address bits compared.

### 4.1 Writes (`PaE.vhd:1228-1397`)

| Port | Decode | Condition | Function |
|---|---|---|---|
| `xxFE` | `A0 = 0` | not (`EFF7.2 = 1` and value = `0x20`) | bits 2..0 border GRB, bit 3 tape out, bit 4 beeper; `border3 := NOT A3` (bright border) |
| `xxFE` = `0x20` | `A0 = 0` | `EFF7.2 = 0` | also clears the INT flag (and sets the border, see §11) |
| `7FFD` | `A15 = 0`, `A1 = 0` | §2.5 | Pentagon-1024 paging |
| `EFF7` | `A15..13 = 111`, `A12 = 0`, `A5 = 1`, `A3 = 0`, `A0 = 1` | any | bit 0 Pentagon 16 colour, bit 2 128 mode (lock, no palette, no INT flag), bit 3 RAM at `0000`, bit 4 turbo0, bit 7 CMOS on |
| `DFF7` | full | `EFF7.7 = 1` | CMOS address |
| `BFF7` | full | `EFF7.7 = 1` | CMOS data (in DOS the same write also sets dispatcher window 2) |
| `xxFB` | `A7..0 = FB` | any | Covox (8-bit DAC, cleared on reset) |
| `xx57` | `A7..0 = 57` | DOS off; DOS on with `A15 = 0` | SD SPI data: starts an 8-bit transfer |
| `xx77` | `A7..0 = 77` | DOS off | SD config: bit 1 = CS (bit 0 power is not used by the FPGA) |
| `xx57` with `A15 = 1` | `A7..0 = 57` | DOS on | SD config |
| `xxFF` | `A7..0 = FF` | (DOS off or `pen2 = 0`) and `EFF7.2 = 0` | palette entry `border3 & FE[2:0]` := `A15..8 : D7..0` (§6.2) |
| `xx77` | `A7..0 = 77` | DOS on | ATM: bits 2..0 video mode, bit 3 turbo1; `cpm := A9`, `pen2 := A14` |
| `xxBF` | `A7..0 = BF` | any | bit 0: keep DOS on in RAM (§3) |
| `FFFD` | `A15..14 = 11`, `A1..0 = 01` | any | AY register select; `0xFF` selects chip 0, `0xFE` chip 1 (TurboSound) |
| `BFFD` | `A15..14 = 10`, `A1..0 = 01` | any | AY register write to the selected chip |
| `3FF7`...`FFF7`, `37F7`...`F7F7` | full | DOS on | dispatcher (§2.3) |
| `xx1F` / `3F` / `5F` / `7F` | `A7 = 0`, `A1..0 = 11`, register = `A6..5` | DOS on, BDI drive A | WD1793 command / track / sector / data |
| `xxFF` (system) | `A7 = 1`, `A1..0 = 11` | DOS on | Beta Disk system register (drive bits 1..0, reset bit 2, side bit 4) |
| `F8EF..FFEF` | `A15..11 = 11111`, `A7..0 = EF`, register = `A10..8` | any | UART 16550 registers |

Overlaps that follow from these decodes and that the emulator must reproduce:

- in DOS, `xx77` (ATM) is also a WD1793 data write (`A7..5 = 011`, `A1..0 = 11`);
- in DOS, every `xFF7` / `x7F7` dispatcher write and every palette write is also a Beta Disk system register write
  (`A7 = 1`, `A1..0 = 11`); the manual asks for bits 2 and 3 = 1 in palette writes for that reason;
- in DOS with `EFF7.7 = 1`, `BFF7` writes both the CMOS and dispatcher window 2.

### 4.2 Reads: priority mux (`PaE.vhd:1493-1541`, first match wins)

| # | Decode | Condition | Value |
|---|---|---|---|
| 1 | INT acknowledge | | `0xFF` |
| 2 | memory read | | SRAM |
| 3 | `A0 = 0` | any | `FE`: bits 4..0 keyboard (half-rows by `A15..8`), bit 5 = 1 if `EFF7.2 = 1` else INT flag, bit 6 tape in, bit 7 = 1 |
| 4 | `A15..13 = 111`, `A1..0 = 01` | | AY register of the selected chip |
| 5 | `BFF7` full | `EFF7.7 = 1` | CMOS cell `DFF7` (§5.4) |
| 6 | `00BE..07BE` full | | dispatcher readback (§2.4) |
| 7 | `A7..0 = 57` | any | SD SPI data (last received byte) |
| 8 | `A7..0 = 1F` | DOS off | Kempston joystick `000FUDLR` |
| 9 | `A7..5` = `000` / `001` / `010` / `011` | DOS on | WD1793 status / track / sector / data (no check of `A1..0` beyond `A0 = 1` from row 3) |
| 10 | `A7..5 = 111` | DOS on | Beta Disk system: bit 7 INTRQ, bit 6 DRQ, bits 5..0 = 1 |
| 11 | `FBDF` / `FFDF` / `FADF` full | any | Kempston mouse X / Y / buttons (bits 0 RMB, 1 LMB, 2 MMB, 7..4 wheel) |
| 12 | `F8EF..FFEF` | any | UART registers |
| 13 | anything else | | external data bus (Nemo IDE, ZX-BUS); `0xFF` with nothing attached |

The SD config port (`xx77` read, "card present / read only" in the manual) is **not** in the mux: it reads the
external bus. Gluk / FATALL do not need it (§11).

## 5. Peripherals

### 5.1 Sound (`PaE.vhd:1399-1432`, `YM2149_linmix.vhd`)

| Item | Value |
|---|---|
| Chips | 2 x YM2149 (TurboSound), selected by `FFFD` = `0xFF` (chip 0, reset default) / `0xFE` (chip 1) |
| AY clock | `CLK_ena = mainclk / 31` = 3.677 MHz, tone counter at `CLK_ena / 16`, square wave toggles: equivalent AY clock **1.8387 MHz** (Pentagon: 1.75 MHz) |
| Volume table | 32-step table, 6-bit output (`0x00..0x3F`), `YM2149_linmix.vhd:400-440` |
| Right channel | `A0 + B0 + A1 + B1 + covox + beeper` |
| Left channel | `B0 + C0 + B1 + C1 + covox + beeper` |
| Beeper / tape out | `+50` when one of `FE.4`, `FE.3` is set, `+100` when both |
| Sum width | 8 bits, **wraps** on overflow (the manual says so too) |
| Output | 8-bit time-multiplexed DAC (`snd`, strobes `str_l` / `str_r`) |

Channel A goes to the right and C to the left (a "CBA" layout as seen from the left speaker); this needs a check on
the schematic before we trust the `str_l` / `str_r` names (§11).

### 5.2 SD card (Z-Controller compatible, `PaE.vhd:972-1029`)

- Data port: a write sends the byte; a read returns the byte received in the previous transfer and starts a new
  transfer that sends `0xFF`. 8 bits are clocked at `mainclk / 8` (14 MHz), i.e. the transfer completes within one
  3.5 MHz T-state; no wait is modelled.
- Config port: bit 1 = CS. The card is always powered by the MCU.
- Ports by mode: DOS off: data `xx57`, config `xx77`; DOS on: data `xx57` with `A15 = 0`, config `xx57` with
  `A15 = 1` (because `xx77` belongs to ATM in DOS).
- The MCU shares the SD card for the boot upload (`spi_enable`), then hands it to the Z80.

### 5.3 Nemo IDE (external)

The IDE is not in the FPGA: `ebl` (`A2 = A1 = 0`, `PaE.vhd:1658`) and `IOR` / `IOW` / `RDH` / `WRH` (`A0` selects the
high-byte latch) go to board logic. Standard Nemo ports: `0x10` data low, `0x11` data high, `0x30..0xF0`
registers by `A7..5`, `0xC8` control. FATALL uses `LD C,0x10` / `0xC8` sequences (5 / 1 hits in `fatall.rom`).

**IDE gap**: in the read mux (§4.2 row 3) any port with `A0 = 0` returns the `FE` value, and the mux never selects
the external bus for it; as written, even-numbered IDE registers read the keyboard. This contradicts the manual
("IDE (Nemo)") and FATALL's IDE code. Decision: emulated as the VHDL does it, so IDE reads of even registers do not
work in the emulator either (§11 D-5).

### 5.4 CMOS and RTC (Gluk standard, `PaE.vhd:880-970`, `1497-1515`)

`EFF7.7 = 1` enables `DFF7` (address) and `BFF7` (data). 256-byte CMOS RAM in the FPGA, persisted by the MCU
(the manual documents 64 cells); RTC kept by the LPC2368.

| Cell | Read value |
|---|---|
| `00`, `02`, `04` | seconds, minutes, hours (BCD) from the MCU RTC, while no write to them is pending |
| `06`, `07`, `08`, `09` | day of week (1..7), day, month, year (BCD) |
| `0A` | `0x20` (constant) |
| `0B` | `0x02` (24 h, BCD) |
| `0C` | `0x00` |
| `0D` | `0x80` (valid RAM and time) |
| `11` (any address with low 6 bits = `0x11`) | `0xAA` |
| `31`, `32`, `33`, `34` | MCU ADC inputs `in_l[7:0]`, `in_l[9:8]`, `in_r[7:0]`, `in_r[9:8]` |
| others | CMOS RAM (`0E` memory mode, `12` = `'G'` enables the user message `13..2E` ended by `0xFF`) |

A write to `00/02/04/06/07/08/09` raises a flag; the MCU picks it up and sets its RTC.

### 5.5 Mouse and joystick

Kempston mouse `FADF` / `FBDF` / `FFDF` (any mode; PS/2 3 buttons and wheel counter in `FADF` bits 7..4). Without
a mouse the MCU sends `0xFF` for all three. Kempston joystick `xx1F` (DOS off), 5 bits from MCU GPIO; the firmware
comments name the bits `D0 left, D1 right, D2 up, D3 down` (`pae_mcu.c:1026-1047`), the opposite of the Kempston
standard (`D0 right, D1 left, D2 down, D3 up`): treat as a pin-naming slip, emulate the standard (§11).

### 5.6 Beta Disk emulator (`betadisk.vhd`)

A WD1793 imitation that works on the RAM disk instead of a floppy:

- Only drive A (`BDI system bits 1..0 = 00`); other drives get no command / data handling (status still reads).
- Image layout (`betadisk.vhd:520-531`): page = `0x76 - cylinder / 2`, offset =
  `(cylinder & 1) << 13 | (side) << 12 | (sector - 1) << 8 | byte`, side = `NOT system.bit4`. That is the TRD byte
  order (`(cylinder x 2 + side) x 4096`) stored downwards, 16 KB per page, 40 pages for 80 cylinders.
- Commands: restore, seek, step / step in / step out (with and without update), read address (track, side, sector
  register, length code `0xFF`, CRC `0xF0 0xE0`), read sector (single / multi), write sector (single / multi), read
  track and write track **as raw sectors 1..16** (no gaps, no IDs: TR-DOS `FORMAT` would write gap bytes into the
  sectors; Gluk formats the RAM disk itself, "Kills/RD Fido"), force interrupt.
- Status: never "not ready", "write protect", "record not found", "CRC error" or "lost data"; bit 1 = DRQ during
  transfers else the index pulse; bit 2 = track 0 for type I.
- Timing: about 128 `mainclk` (1.1 us) per byte, the CPU is stalled during each RAM access; restore and read
  address take `0x3D40000` `mainclk` = 0.56 s; index pulse 5 Hz, 4 ms.

### 5.7 UART

16550 subset at `F8EF..FFEF` (manual §2.12): 16-byte FIFOs, `baud = 115200 / divisor`, RTS / CTS, interrupts into
INT. The manual says "not supported by the current firmware" (no software uses it); the VHDL has it.

## 6. Video

### 6.1 Modes (`gfx_mode = xx77[2:0] & 00 & EFF7.0`, `PaE.vhd:1450`, `zx_main.vhd:250-410`)

| `xx77[2:0]` | `EFF7.0` | Mode | Pages (screen 0 / screen 1 by `7FFD.3`) | Layout |
|---|---|---|---|---|
| `011` | 0 | ZX 256 x 192 | 5 / 7 | standard pixels + attributes, FLASH from the frame counter (`flash_counter(4)`: 16 frames per phase) |
| `011` | 1 | Pentagon 16 colour 256 x 192 | 4+5 / 6+7 | 4 bytes per 8 pixels: page 4 `zx_offset`, page 5 `zx_offset`, page 4 `+0x2000`, page 5 `+0x2000`; byte = `I_r I_l G_r R_r B_r G_l R_l B_l` (manual §3.3) |
| `000` | 0 | ATM EGA 320 x 200, 16 colours | 1+5 / 3+7 | 8 x 1 blocks, `offset = X + 40Y` in page 1 / 5 / 1 `+0x2000` / 5 `+0x2000`, pixel bytes as Pentagon 16c |
| `010` | 0 | ATM hardware multicolor 640 x 200 | 1+5 / 3+7 | 16 x 1 blocks; pixels in page 5 (`offset`, `offset + 0x2000`), attributes in page 1; attribute `PI II PG PR PB IG IR IB` |
| `110` | 0 | ATM text 80 x 25 | 1+5 / 3+7 | chars page 5 `0x01C0 + 64Y + X` (left), `0x21C0 + 64Y + X` (right); attributes page 1 `0x21C0 + 64Y + X` (left), `0x01C1 + 64Y + X` (right); 8 x 8 font from `otmfnt.hex`, attribute as in 640 x 200 |
| other | | no case in `zx_main`: the colour register keeps its last value | | treat as "undefined", draw border colour (§11) |

ATM modes ignore `EFF7.0` only in the sense that `EFF7.0 = 1` with an ATM mode is "other". The ATM modes are
selected through `xx77`, i.e. only from DOS (Gluk, NedoOS drivers in shadow mode).

### 6.2 Palette (`PaE.vhd:1434-1446`, `zx_main.vhd:211-213`, `pollitra.hex`)

- 16 entries, applied in every mode (ZX mode included) and to the border.
- Write: `OUT (xxFF)` with the conditions of §4.1 stores `A15..8 : D7..0` into entry `border3 & FE[2:0]`, i.e. the
  entry is the **current border colour** (16 colours through `border3 = NOT A3` of the last `OUT (FE)`).
- Colour bits (each component inverted, 5-bit DAC whose LSB is always 0, so 4 effective bits, 4096 colours):

  | Output | Bit 4 | Bit 3 | Bit 2 | Bit 1 |
  |---|---|---|---|---|
  | R | `NOT D1` | `NOT D6` | `NOT A9` | `NOT A14` |
  | G | `NOT D4` | `NOT D7` | `NOT A12` | `NOT A15` |
  | B | `NOT D0` | `NOT D5` | `NOT A8` | `NOT A13` |

  `D` bits alone give the ATM2 2-bit-per-component palette; the `A` bits extend it to 4 bits (ATM3 / Evo style).
- Default contents (block RAM init from `pollitra.hex`, kept across CPU reset, restored only by FPGA
  reconfiguration). Entry `IGRB`, 16-bit word `A15..8 : D7..0`, components out of 31:

  | Entries | Words | Component on | Component off |
  |---|---|---|---|
  | 0..7 (`I = 0`) | `FFFF FEFE FDFD FCFC EFEF EEEE EDED ECEC` | 20 | 0 |
  | 8..15 (`I = 1`) | `FFFF DEDE BDBD 9C9C 6F6F 4E4E 2D2D 0C0C` | 30 | 0 |

  Entry 8 (bright black) is black.

### 6.3 Border

16 colours (`border3 & FE[2:0]`) through the palette. In the ZX and Pentagon 16c modes the border goes through a
per-line delay buffer (`border_buf*`, `border_delay`), in ATM modes it is taken directly from `FE`.

## 7. Reset and power-on

| State | CPU reset (F12, ZX-BUS RESET) | Power-on (adds) |
|---|---|---|
| `7FFD`, `EFF7`, `DFF7`, `BF.0`, `cpm` | 0 | |
| `xx77` | `0xAB`: Pentagon video, 28 MHz | |
| `pen2` | 1 | |
| Dispatcher windows | Pentagon mode (§2.2) | |
| DOS | on | |
| Covox, AY chip select | 0, chip 0 | |
| AY registers | reset | |
| Palette | kept | default from `pollitra.hex` |
| RAM, RAM disk, ROM pages | kept | MCU upload (§2.1), logo in page 5 |
| CMOS | kept | MCU restores 64 cells |

F12 on the PS/2 keyboard is the reset key (`ps2.c:54-55`); F11 (magic / NMI) is commented out.

## 8. Keyboard

PS/2 -> MCU -> 5 matrix bytes over I2C (`PaE.vhd:1170-1200`). The MCU maps cursor keys, Backspace, Del, Ins, Home,
End, PgUp / PgDn, Esc, Tab, punctuation to Caps / Symbol Shift combinations (`ps2.c:20-57`). The emulator uses its
own host keyboard mapping; the list matters only as a reference for the profile.

## 9. ZX-BUS

`fpga_io0..2` (external `IORQGE`), `fpga_rdrom_input` (external ROM), `fpga_wait_input`, `fpga_busrq_input`,
`fpga_nmi_input`, `fpga_int_input` (the internal INT goes out and comes back) are ZX-BUS lines. No card is part of
this configuration; the emulator models none of them.

## 10. What is not in this machine

No contended memory, no ULA snow, no floppy controller for real drives (only the RAM-disk Beta Disk), no TS-Conf
features (no `xxAF` registers, no DMA, no sprites), no BaseConf extended video modes, no `7FFD` readback, no
`EFF7` readback, no ATM3 `xx77` bits above 3.

## 11. Manual versus VHDL, and other doubts

| ID | Topic | Manual | VHDL | Decision (owner 2026-11-11: everything as the hardware, nothing improved) |
|---|---|---|---|---|
| D-1 | `7FFD` decode | `%1xxxxxxxxxxxxx0x` (A15 = 1) | `A15 = 0`, `A1 = 0` | VHDL; manual typo |
| D-2 | `OUT (FE), 0x20` | in extended mode clears the INT flag "without updating the other bits" | extended mode: clears the flag **and** writes border / beeper / tape; 128 mode: the write is ignored entirely | VHDL (Gluk / NedoOS run on it) |
| D-3 | 128 mode, locked `7FFD` | 48 KB lock | page bit 5 = lock bit, so `C000` maps page `32 + n` | VHDL; invisible to 48K software |
| D-4 | SD config read (card present, read only) | documented | not implemented: external bus | **decided**: `0xFF` (open bus) |
| D-5 | Nemo IDE reads | "IDE (Nemo)" works | even ports read `FE`; odd ports reach the bus only where no other row matches (`0x11` out of DOS; in DOS `0x11` reads WD status) | **decided**: copy the VHDL. Even IDE registers do not work and will not; the fix stays a commented-out recipe in the decoder (design §4.1), not enabled |
| D-6 | Stereo | not stated | right = A + B, left = B + C | **decided**: as the VHDL |
| D-7 | Kempston joystick bits | standard | the MCU sends its pins raw: `D0` = P2.7, `D1` = P2.6, `D2` = P2.5, `D3` = P2.4, `D4` = P0.11 (comments: left, right, up, down, fire) | **decided**: as the board; the direction of each pin from the schematic (Appendix 2), until then the firmware comments |
| D-8 | Undefined `gfx_mode` values | not stated | last colour held | **decided**: as the VHDL (the colour register keeps its last value) |
| D-9 | UART | "not supported by the current firmware" | implemented | as the VHDL, phase P4 |
| D-10 | CMOS size | 64 cells | 256-byte RAM, 64 persisted | as the VHDL: 256 cells, 64 persisted |
| D-11 | Sound overflow | "without overflow check" | 8-bit wrap | **decided**: wrap, as the board; a saturating mix stays a commented-out recipe (design §5.2), not enabled |
| D-12 | AY clock | not stated | 1.8387 MHz | as the VHDL |
