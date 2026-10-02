# FT812 behavior specification (VDAC2)

Design phase D-A of [vdac2-tdd.md](vdac2-tdd.md) §8: what the FT812 does, as seen from the
host bus and on the screen, written for an emulator. Every rule names its source. A rule
that the sources do not settle is marked **TO VERIFY** and collected in §12 with a way to
settle it. Terms are explained in the design's glossary.

Date: 2026-10-01. Chip: FT812 (24-bit RGB, resistive touch, not used on VDAC2). Facts
that differ for FT810/FT811/FT813 are noted where they matter.

## 0. Sources and markers

| Marker | Source |
|---|---|
| [PG §n] | FT81X Series Programmers Guide v1.2 (BRT_000031), section n |
| [DS §n] | FT81x datasheet (FT_001165), section n |
| [SDK file] | TS-Labs FT812 SDK, `tslabs/zx-evo` `pentevo/sdk/ft812sdk/`: library `lib/ft812/` (`ft812.h`, `ft812dl.c`, `ft812cp.c`, `ft812func.c`), test programs `testN/src/main.c`, viewer `ftview/src/main.c` |
| [dxt_conv] | TS-Labs image converter `pentevo/tools/dxt_conv/ft812_dxt_convert.py` and its README |
| [C file] | the VDAC2 card's CPLD, `pentevo/vdac/vdac2/cpld/` |
| [ESP32 file] | TS-Labs ESP32-S3 firmware, `pentevo/esp32/src/main/` (`ft8xx.h`, `ft8xx.cpp`) |
| [TSLib file] | TSLib FT812 macros as shipped with the VDAC2 games (`Docs/TSLib/Include/FT/`: `81x Const.inc`, `DL  Macro.inc`, `812 Macro.inc`, `812 Func.asm`) |
| [SW file] | VDAC2 game sources (R-Type, Zuma, HMM2), named by file |
| [DLL] | the FT81x ROM image inside `bt8xxemu.dll` (design §5.5) |

Where the PG and the DS disagree, both values are given and the item is TO VERIFY. Where
TS-Labs code disagrees with the PG, the code that runs on real cards decides: the VDAC2
games (TSLib) were tested on hardware, the SDK test programs were written by TS-Labs.

**TS-Labs code is not uniform, and one difference matters.** The SDK's C encoders
`ft_ScissorXY` / `ft_ScissorSize` [SDK `ft812dl.c`] and the ESP32's `ft_ScissorXY`
[ESP32 `ft8xx.cpp:529`] use the FT80x bit layout (9 / 10 bits). TSLib [TSLib `DL  Macro.inc:91-96`],
the ESP32's `ft_ScissorSize` and the PG use the FT81x layout (11 / 12 bits). The emulator
follows the PG. The SDK test programs do not use scissor, so nothing visible depends on
the old encoders.

## 1. Address space

The host sees a 22-bit address space [DS §5, PG Appendix A]:

| Range | Size | Name | Access |
|---|---|---|---|
| 0x000000-0x0FFFFF | 1 MB | `RAM_G` | read / write |
| 0x0C0000-0x0C0003 | 4 B | chip ID, inside `RAM_G` | after reset reads `08 12 01 00` for FT812 (byte at 0x0C0000 first); it is ordinary RAM, so the first write overwrites it [PG §2.2] |
| 0x1E0000-0x2FFFFB | 1152 KB | `ROM_FONT` | read only: the ROM image [DLL]; writes ignored |
| 0x2FFFFC-0x2FFFFF | 4 B | `ROM_FONTROOT` | reads 0x00201EE0 [PG §5.5.1] |
| 0x300000-0x301FFF | 8 KB | `RAM_DL` | read / write (the pending list, §6.7) |
| 0x302000-0x302FFF | 4 KB | `RAM_REG` | registers (§3) |
| 0x308000-0x308FFF | 4 KB | `RAM_CMD` | read / write, the coprocessor ring (§7.1) |
| 0x309000-0x309018 | | special registers | `REG_TRACKER`…`REG_TRACKER_4` (read 0 on VDAC2), `REG_MEDIAFIFO_READ` 0x309014, `REG_MEDIAFIFO_WRITE` 0x309018 [PG §3.5] |
| everything else | | reserved | "shall not be read or written" [DS §5]. Reads return 0 and writes are ignored (TO VERIFY) |

`RAM_ERR_REPORT` (0x309800) appears in TS-Labs headers [SDK `ft812.h`] but is a BT81x
feature; on FT812 it is reserved.

## 2. Host interface (SPI)

### 2.1 Transactions

- The chip is an SPI slave, **mode 0 only**, most significant bit first, single channel by
  default [DS §4.1.1]. A transaction starts when CS_N goes low and ends when it goes high
  [DS §4.1.2].
- The first byte's top two bits select the transaction [DS §4.1.3-4.1.5, §5]:

| Bits 7:6 of byte 0 | Transaction | Layout |
|---|---|---|
| `00` | memory read | `00 A[21:16]`, `A[15:8]`, `A[7:0]`, one dummy byte, then one data byte out per byte clocked |
| `10` | memory write | `10 A[21:16]`, `A[15:8]`, `A[7:0]`, then data bytes |
| `01` | host command | `01 C[5:0]`, parameter byte, `00` |
| `11` | undefined | ignored (TO VERIFY) |

- **Auto-increment.** Within one transaction the address increases by one per data byte,
  "as long as the memory address is continuous" [DS §4.1.2]. Two exceptions:
  - A write that **starts inside `RAM_CMD`** wraps from 0x308FFF back to 0x308000
    [PG §5.1.1].
  - A write to **`REG_CMDB_WRITE`** (0x302578) is a bulk FIFO write: the host sends up to
    `REG_CMDB_SPACE` bytes "in one write transfer with this register address"
    [PG §3.4, §5.1.2], so the address does not advance. Each complete 4-byte word is
    appended to the ring (§7.1). A partial word at CS_N high is TO VERIFY; the design
    drops it.
- **Read data timing.** MISO carries the byte of the current address while the host clocks
  the next byte; during the address and dummy bytes MISO is undefined, the emulator drives
  0xFF (TO VERIFY). On VDAC2 the Z-Controller returns the byte of the previous exchange,
  which gives the host idiom `OUT addr ×3, OUT dummy, IN (dummy), IN data…`
  [SDK `ft_rreg8`, comments "dummy (FT812)", "dummy (ZC)"].
- `REG_SPI_WIDTH` (0x302188) selects dual / quad mode and an extra dummy byte [PG §3.6].
  VDAC2 wires single-channel SPI only; writes are stored and the read path keeps one
  dummy byte. The extra-dummy bit (bit 2) is honored, since it is a host-side protocol
  change (TO VERIFY on the card; no VDAC2 software sets it).

### 2.2 Host commands

[DS §4.1.5 Table 4-5], codes as in [SDK `ft812.h`]:

| Byte 0 | Name | Effect in the emulator |
|---|---|---|
| 0x00 (`00 00 00`) | ACTIVE | leave STANDBY / SLEEP / PWRDOWN; clocks start. A memory read from address 0 also counts as ACTIVE ("read twice") [DS] |
| 0x41 | STANDBY | clock gated, PLL and oscillator on: time stops for the engines |
| 0x42 | SLEEP | clock, PLL and oscillator off: time stops; CLKSEL is accepted only here |
| 0x43 / 0x50 | PWRDOWN | core power off: all state except the host-command configuration is lost; SPI stays alive |
| 0x44 | CLKEXT | select the external clock (VDAC2: 8 MHz crystal). "No effect if external clock is already selected, otherwise a system reset will be generated" [DS] |
| 0x48 | CLKINT | select the internal oscillator (12 MHz, trimmable); same reset rule |
| 0x49 | PD_ROMS | power down ROM blocks per byte-1 bits; stored, no visible effect modeled (TO VERIFY) |
| 0x61 / 0x62 | CLKSEL | byte 1: `[5:0]` multiplier, `[7:6]` PLL range. 0 = default. Effective only in SLEEP [DS] |
| 0x68 | RST_PULSE | reset the core like power-on, keeping settings made by host commands [DS] |

- **Clock.** System clock = input clock × multiplier; with multiplier 0 the default is
  5 × input (60 MHz from 12 MHz) [DS §4.2.4]. The DS allows 2…5; VDAC2 software uses
  6…10 with the 8 MHz crystal (48…80 MHz) and always sets range bits `01`
  (`mul | 0x40`) [SDK `ft_init`, ESP32 `ft8xx.h` mode table]. The emulator accepts
  2…63 and computes `8 MHz × mul`; out-of-spec values are not rejected, because the card
  runs them.
- **Power-up state.** At power-on the chip is in SLEEP with the internal oscillator
  selected [DS §4.2.3]. Nothing answers until ACTIVE.
- **Boot handshake used by VDAC2 software** [SDK `ft_init`, TSLib `FT_BOOT_UP` /
  `Initialize`]: `PWRDOWN`, `ACTIVE`, `SLEEP`, `CLKEXT`, `CLKSEL(mul|0x40)`, `ACTIVE`,
  `RST_PULSE`; poll `REG_ID` until 0x7C; poll `REG_CPURESET` until 0. The time from
  ACTIVE to `REG_ID` = 0x7C is "up to 300 ms" of self-diagnosis [PG §2.3]; the emulator
  makes `REG_ID` readable after a fixed delay (TO VERIFY; 0 is acceptable for software
  that polls, which all of it does).

## 3. Registers

Offsets from `RAM_REG` = 0x302000. Reset values from [PG §3] unless noted; widths and
access from [DS Table 5-2]. All registers are 32-bit, little-endian, byte-addressable;
reserved bits read 0 [PG §3].

**How writes take effect** (TO VERIFY on the card for multi-byte registers): each byte
written updates the stored value at once; a register's side effect runs when a byte that
holds its function bits has been written. For 32-bit control registers whose side effect
depends on the whole value (`REG_CMD_WRITE`, `REG_MEDIAFIFO_WRITE`) the effect runs at the
end of the transaction (CS_N high) or when the register's last byte is written, whichever
comes first. Software writes these with one 16- or 32-bit transaction [SDK, TSLib].

### 3.1 Identification, time, clock

| Offset | Name | Reset | Behavior |
|---|---|---|---|
| 0x000 | `REG_ID` | 0x7C | read only |
| 0x004 | `REG_FRAMES` | 0 | read only; +1 per scanned frame (§5.3) |
| 0x008 | `REG_CLOCK` | 0 | read only; system clocks since reset, wraps at 2^32 |
| 0x00C | `REG_FREQUENCY` | 60 000 000 | read / write; the host's statement of the system clock, used by the coprocessor for millisecond delays (`CMD_INTERRUPT`, video timing). It does not change the clock. 28 bits [DS] vs 32 [PG] (TO VERIFY). `ftview` writes 64 000 000 before playing video [SDK `ftview/src/main.c` `show_avi`] |
| 0x010-0x01C | `REG_RENDERMODE`, `REG_SNAPY`, `REG_SNAPSHOT`, `REG_SNAPFORMAT` | 0, 0, -, 0x20 | single-line render mode for snapshots; stored, render mode 1 is not implemented (no VDAC2 software uses it) |
| 0x020 | `REG_CPURESET` | 0 [PG] / 2 [DS] (TO VERIFY) | bit 0 coprocessor, bit 1 touch, bit 2 audio: 1 = hold the engine in reset; reads back the reset state. SDK boot waits for 0 [SDK `ft_init`] |
| 0x024, 0x028 | `REG_TAP_CRC`, `REG_TAP_MASK` | -, 0xFFFFFFFF | CRC of the output frame computed at each swap. Implemented as CRC-32 of the visible frame (TO VERIFY the exact polynomial and coverage); no VDAC2 software reads it |

### 3.2 Video timing and output

| Offset | Name | Reset | Behavior |
|---|---|---|---|
| 0x02C | `REG_HCYCLE` | 548 | total pixel clocks per line (12 bits) |
| 0x030 | `REG_HOFFSET` | 43 | clocks before the first visible pixel |
| 0x034 | `REG_HSIZE` | 480 | visible pixels per line |
| 0x038 / 0x03C | `REG_HSYNC0` / `REG_HSYNC1` | 0 / 41 | HSYNC fall / rise, in pixel clocks from line start |
| 0x040 | `REG_VCYCLE` | 292 | total lines per frame |
| 0x044 | `REG_VOFFSET` | 12 | lines before the first visible line |
| 0x048 | `REG_VSIZE` | 272 | visible lines |
| 0x04C / 0x050 | `REG_VSYNC0` / `REG_VSYNC1` | 0 / 10 | VSYNC fall / rise, in lines |
| 0x054 | `REG_DLSWAP` | 0 | §6.7 |
| 0x058 | `REG_ROTATE` | 0 | screen orientation 0…7; changes the coordinate system of all drawing at once [PG §2.5.3]. No VDAC2 software sets it; implemented as specified (§6.3) |
| 0x05C | `REG_OUTBITS` | 0 on FT812 (8 bits per channel), 0x1B6 on FT810/811 [PG, DS] | bits per channel on the output; 0 = 8 |
| 0x060 | `REG_DITHER` | 1 | 2×2 dither when the output has fewer bits than the internal 8 [DS §4.4]. With `OUTBITS` = 0 (8 bits) it changes nothing, since "the graphics engine computes the colour values at an 8 bit precision" [DS §4.4] |
| 0x064 | `REG_SWIZZLE` | 0 | output pin arrangement [DS Table 4-12]: bit 0 reverses bit order within each channel; bits 3:1 select the channel order. Applied to the ARGB8888 output exactly per the table |
| 0x068 | `REG_CSPREAD` | 1 | 1: "R[7:0] changes a PCLK clock early and B[7:0] a PCLK clock later" [DS §4.4]. VDAC2 latches all 24 bits on one clock edge, so with 1 the screen shows each pixel's red from the next pixel and its blue from the previous one: a one-pixel color fringe. TS-Labs: 0 "is critical for correct colors display" [TSLib `812 Func.asm:33`]. The emulator produces the shifted picture (edges of a line: TO VERIFY) |
| 0x06C | `REG_PCLK_POL` | 0 | clock edge for the output; no picture effect on VDAC2 |
| 0x070 | `REG_PCLK` | 0 | pixel clock = system clock / `REG_PCLK`; 0 stops the output and the scan (§5.1) |

Timing semantics [DS Table 4-13]: `HOFFSET` = front porch + sync + back porch, `HSYNC0` =
front porch, `HSYNC1` = front porch + sync; the same for the vertical registers in lines.
TS-Labs programs the vertical values one line smaller (`VOFFSET = fp+sync+bp−1`,
`VSYNC0 = fp−1`, `VSYNC1 = fp+sync−1`) and the horizontal ones not
[SDK `ft_init`, TSLib `FT_ModeTab`]. That points to a line counter starting at a
different origin (TO VERIFY); it only shifts sync against the visible area and does not
change the frame period `HCYCLE × VCYCLE`.

### 3.3 Tags and audio

| Offset | Name | Reset | Behavior |
|---|---|---|---|
| 0x074 / 0x078 | `REG_TAG_X` / `REG_TAG_Y` | 0 | query point for `REG_TAG` |
| 0x07C | `REG_TAG` | 0 | tag buffer value at (`TAG_X`, `TAG_Y`) of the last drawn frame (§6.5) |
| 0x080 | `REG_VOL_PB` | 0xFF | stored |
| 0x084 | `REG_VOL_SOUND` | 0xFF | stored |
| 0x088 | `REG_SOUND` | 0 | stored; effect select |
| 0x08C | `REG_PLAY` | 0 | write starts the effect; reads 1 while it plays. Duration of each effect: TO VERIFY; the emulator ends a non-silent effect after a fixed time and raises `INT_SOUND` |
| 0x0B4 | `REG_PLAYBACK_START` | 0 | 20 bits; must be 8-byte aligned [PG §2.4.2] |
| 0x0B8 | `REG_PLAYBACK_LENGTH` | 0 | 20 bits |
| 0x0BC | `REG_PLAYBACK_READPTR` | 0 | read only; advances at `REG_PLAYBACK_FREQ` samples per second (1 byte per sample for linear / u-law, 2 samples per byte for ADPCM) |
| 0x0C0 | `REG_PLAYBACK_FREQ` | 8000 | Hz |
| 0x0C4 | `REG_PLAYBACK_FORMAT` | 0 | 0 linear, 1 u-law, 2 IMA ADPCM |
| 0x0C8 | `REG_PLAYBACK_LOOP` | 0 | loop when the end is reached |
| 0x0CC | `REG_PLAYBACK_PLAY` | 0 | any write starts playback; reads 1 while playing; at the end raises `INT_PLAYBACK` |

The card has no audio output (design §2.1): the audio engine runs its timing (read
pointer, flags, interrupts) and produces no samples for the host.

### 3.4 GPIO, backlight, interrupts, macros

| Offset | Name | Reset | Behavior |
|---|---|---|---|
| 0x090 / 0x094 | `REG_GPIO_DIR` / `REG_GPIO` | 0x80 / 0 | legacy view of GPIO; stored |
| 0x098 | `REG_GPIOX_DIR` | 0x8000 | bit 15 DISP direction, bits 3:0 GPIO3…0 |
| 0x09C | `REG_GPIOX` | 0x8000 [PG] / 0x0080 [DS] (TO VERIFY) | bit 15 DISP level, bit 12 drive strength, **bit 9 INT_N type: 0 open drain, 1 push-pull** (VDAC2 software sets 1 [SDK `ft_init`]) |
| 0x0A8 | `REG_INT_FLAGS` | 0 | §5.4; **cleared by read** |
| 0x0AC | `REG_INT_EN` | 0 | global INT_N enable |
| 0x0B0 | `REG_INT_MASK` | 0xFF | per-source enable |
| 0x0D0 / 0x0D4 | `REG_PWM_HZ` / `REG_PWM_DUTY` | 250 / 128 | backlight; stored, no effect on VDAC2 (no backlight) |
| 0x0D8 / 0x0DC | `REG_MACRO_0` / `REG_MACRO_1` | 0 | display list words executed by `MACRO` (§6.2) |

**DISP.** With DISP low (`REG_GPIOX` bit 15 = 0, or legacy `REG_GPIO` bit 7), a panel
would be off. On VDAC2 the picture goes from the FT812's RGB, HSYNC, VSYNC and PCLK pins
through the CPLD to the DAC; the CPLD has no DISP or DE input [C `top.v` ports]. So DISP
does not affect the picture, and the emulator only stores it.

### 3.5 Coprocessor registers

| Offset | Name | Reset | Behavior |
|---|---|---|---|
| 0x0F8 | `REG_CMD_READ` | 0 | coprocessor read pointer, 12 bits; 0xFFF after a fault (§7.4). The host writes it only during recovery [PG §3.4] |
| 0x0FC | `REG_CMD_WRITE` | 0 | host write pointer; must be a multiple of 4 [PG §3.4]; r/o in [DS Table 5-2], r/w in [PG] (software writes it during recovery [SDK `ft_cp_reset`]) |
| 0x100 | `REG_CMD_DL` | 0 | where the coprocessor writes the next display list word (offset in `RAM_DL`, 13 bits) |
| 0x574 | `REG_CMDB_SPACE` | 0xFFC | free bytes in the ring: `4092 − ((REG_CMD_WRITE − REG_CMD_READ) mod 4096)` [PG §5.1.1]. Idle = 0xFFC [SDK `ft_cp_wait`]. After a fault the formula with `READ` = 0xFFF yields a value with its low two bits set, which is how software detects the fault [SDK `ft_load_cfifo`: `if (sp & 3) ft_cp_reset()`] |
| 0x578 | `REG_CMDB_WRITE` | - | write only, §2.1 |
| 0x57C | `REG_ADAPTIVE_FRAMERATE` | 1 (TO VERIFY) | VDAC2 software writes 0 [SDK `ft_init`]. Meaning on FT812: TO VERIFY; stored |
| 0x564 | `REG_DATESTAMP` | ROM build stamp (TO VERIFY: read the value from the [DLL] ROM image) | read only |

Touch registers (0x104-0x190) read their reset values; touch is not wired on VDAC2.
`REG_TRIM`, `REG_BIST_EN`, `REG_ANA_COMP` are stored and have no effect.

## 4. Time base

- **System clock** f_sys = input × multiplier (§2.2): 8 MHz × 6…10 on VDAC2.
  `REG_CLOCK` counts it. The owner advances the chip in system clocks.
- **Pixel clock** f_pix = f_sys / `REG_PCLK`; 0 = no scan.
- **Line period** = `HCYCLE` pixel clocks = `HCYCLE × REG_PCLK` system clocks.
- **Frame period** = `HCYCLE × VCYCLE` pixel clocks. The TS-Labs mode table gives the
  expected rates [ESP32 `ft8xx.h`]: mode 7 (1024×768, f_sys 64 MHz, `PCLK` 1, 1344 × 806)
  = 59.081 Hz; mode 1 (640×480, 64 MHz, `PCLK` 2, 832 × 520) = 73.964 Hz; mode 14
  (64 MHz, `HCYCLE` 1344, `VSIZE` 938, `VCYCLE` 976) = 48.790 Hz; the SDK names it
  1024×768 but programs 938 visible lines. Unit tests reproduce all 15 rows.
- STANDBY, SLEEP, PWRDOWN and `REG_PCLK` = 0 stop the scan; `REG_FRAMES` and `REG_CLOCK`
  stop with the system clock (STANDBY / SLEEP / PWRDOWN) (TO VERIFY whether `REG_CLOCK`
  runs while `REG_PCLK` = 0: it should, the system clock runs).

## 5. Scan-out, swap and interrupts

### 5.1 Scan

- The scan position (pixel clock in line, line in frame) advances with f_pix. The
  visible window is pixel `HOFFSET … HOFFSET+HSIZE−1` of lines
  `VOFFSET … VOFFSET+VSIZE−1` (per §3.2; origin details TO VERIFY).
- **The picture is built line by line, without a frame buffer** [PG §2.5.7, DS §4.3.1]:
  for each visible line the graphics engine executes the active display list from its
  start and fills a line buffer, which is then shifted out. A write to `RAM_G` or to a
  bitmap's data therefore shows on the lines drawn after it, in the same frame. VDAC2
  software depends on this: writing `RAM_G` under the list being shown visibly corrupts
  sprites on the card [SW R-Type `CLAUDE.md`].
- **Look-ahead.** The line buffer is filled during the previous line's scan-out
  (double line buffer). How early a line samples memory (one line, or a fixed number of
  clocks before its first pixel) is TO VERIFY; the emulator draws line N during line
  N − 1, starting at its pixel clock 0.

### 5.2 Line budget

[PG §2.5.7], [SW R-Type `ft812_line_cost.py`, `CLAUDE.md`]:

- The engine fetches **one display list command per system clock**; a list may have at
  most 2048 commands (a coprocessor attempt to write more is a fault, §7.4). Commands
  inside a subroutine count each time it is called.
- Filling costs per pixel, per primitive span on the line:

| Filter | Formats | Pixels per clock |
|---|---|---|
| NEAREST | TEXT8X8, TEXTVGA, PALETTED4444, PALETTED565 | 8 |
| NEAREST | all other formats | 16 |
| BILINEAR | TEXT8X8, TEXTVGA, PALETTED4444, PALETTED565 | 2 |
| BILINEAR | all other formats | 4 |

  The PG text says "¼ pixels per clock" for bilinear but its table says 4; the table is
  used (TO VERIFY). Points, lines, rectangles and edge strips: cost per pixel TO VERIFY
  (treated as 16 per clock).
- **Budget.** The time available for one line "depends on REG_PCLK and REG_HCYCLE but is
  never less than 2048 internal clock cycles" [PG §2.5.7]. With VDAC2's `PCLK` = 1 the
  line lasts `HCYCLE` system clocks (1344 in mode 7), and R-Type finds the usable ceiling
  "around 1300" there, with its worst line at 1247 and no broken lines on the card
  [SW R-Type `CLAUDE.md`]. The budget the emulator uses is `HCYCLE × REG_PCLK` system
  clocks, with the 2048 floor applied only when that product is at least 2048 (TO VERIFY
  both the overhead below `HCYCLE` and the floor).
- **Overflow.** A line whose cost exceeds the budget comes out broken on the card. What
  exactly appears (the rest of the line from the previous frame, black, a repeated line)
  is TO VERIFY. Until then the emulator draws the line completely, counts the overflow,
  and reports it (design §5.6). The per-line cost is always available to the debugger.

### 5.3 Frame counter and swap

- `REG_FRAMES` increments once per frame; the point in the frame where it increments is
  TO VERIFY (the design uses the end of the last visible line).
- `REG_DLSWAP` [PG §3.1 Register Definition 18]:
  - 2 (`DLSWAP_FRAME`): the pending list in `RAM_DL` becomes active after the current
    frame has been scanned out; the register then reads 0 and `INT_SWAP` is raised.
  - 1 (`DLSWAP_LINE`): the swap happens after the current line; tearing is possible.
  - 0 and 3 must not be written; writing them leaves a pending swap as it is (TO VERIFY).
  - While it reads non-zero, the pending list must not be written.
- After `RST_PULSE` / power-on the very first `DLSWAP` "swaps the display list
  immediately" while no picture is being scanned (`REG_PCLK` = 0) [PG §2.3 step 5]. The
  emulator applies a swap at once whenever the scan is stopped.

### 5.4 Interrupts

[DS §4.1.6], codes [SDK `ft812.h`]:

| Bit | Flag | Raised when |
|---|---|---|
| 0 | `INT_SWAP` | a display list swap took place |
| 1 | `INT_TOUCH` | never on VDAC2 |
| 2 | `INT_TAG` | `REG_TOUCH_TAG` changed: never on VDAC2 |
| 3 | `INT_SOUND` | a sound effect ended |
| 4 | `INT_PLAYBACK` | audio playback ended |
| 5 | `INT_CMDEMPTY` | the coprocessor finished the last command in the ring, and on a fault [PG §5.4, §5.6] |
| 6 | `INT_CMDFLAG` | `CMD_INTERRUPT` fired |
| 7 | `INT_CONVCOMPLETE` | touch conversion finished: never on VDAC2 |

- `REG_INT_FLAGS` collects events regardless of the mask; **a read returns the flags and
  clears them** [PG Register Definition 89, DS §4.1.6].
- **INT_N** is low while `REG_INT_EN` = 1 and `REG_INT_FLAGS & REG_INT_MASK` ≠ 0
  [DS §4.1.6]. With `REG_INT_EN` = 0 the pin floats (pulled high: inactive).
- On VDAC2 the TS-Conf line interrupt triggers on INT_N's falling edge while msel = 1
  (design §2.3); a new edge needs the flags to be cleared by a read first.
- VDAC2 software: `INT_MASK` = `INT_SWAP`, `INT_EN` = 1, and frames synchronized by polling
  `REG_INT_FLAGS` for SWAP [SDK `ft_init`, `ft_wait_swap`; SW R-Type `render.asm:86-92`].

## 6. Display list

### 6.1 Encoding

All commands are 32-bit. Bits 31:30 = `01` is `VERTEX2F`, `10` is `VERTEX2II`; otherwise
bits 31:24 are the opcode [PG §4]. Reserved bits must be zero; with non-zero reserved
bits the behavior is TO VERIFY (the emulator ignores them).

| Opcode | Command | Fields (bit ranges) | Initial value / notes |
|---|---|---|---|
| 0x00 | `DISPLAY` | - | end of list: nothing after it is executed |
| 0x01 | `BITMAP_SOURCE` | addr 21:0 | address in `RAM_G`, aligned to the pixel size; per handle |
| 0x02 | `CLEAR_COLOR_RGB` | red 23:16, green 15:8, blue 7:0 | 0,0,0. The PG's field figure says "Red Blue Green"; the bit order red-green-blue is confirmed by [SDK `ft_ClearColorRGB`] and [TSLib] |
| 0x03 | `TAG` | s 7:0 | 255 |
| 0x04 | `COLOR_RGB` | red 23:16, green 15:8, blue 7:0 | 255,255,255 (same note as 0x02) |
| 0x05 | `BITMAP_HANDLE` | handle 4:0 | 0 |
| 0x06 | `CELL` | cell 6:0 | 0 |
| 0x07 | `BITMAP_LAYOUT` | format 23:19, linestride 18:9, height 8:0 | per handle |
| 0x08 | `BITMAP_SIZE` | filter 20, wrapx 19, wrapy 18, width 17:9, height 8:0 | per handle |
| 0x09 | `ALPHA_FUNC` | func 10:8, ref 7:0 | ALWAYS, 0 |
| 0x0A | `STENCIL_FUNC` | func 19:16, ref 15:8, mask 7:0 | ALWAYS, 0, 255 |
| 0x0B | `BLEND_FUNC` | src 5:3, dst 2:0 | SRC_ALPHA, ONE_MINUS_SRC_ALPHA |
| 0x0C | `STENCIL_OP` | sfail 5:3, spass 2:0 | KEEP, KEEP |
| 0x0D | `POINT_SIZE` | size 12:0 (1/16 px, radius) | 16 |
| 0x0E | `LINE_WIDTH` | width 11:0 (1/16 px, half width) | 16 |
| 0x0F | `CLEAR_COLOR_A` | alpha 7:0 | 0 |
| 0x10 | `COLOR_A` | alpha 7:0 | 255 ([PG §4.26]; the context table in [PG §4.1] says 0, the command section and every program assume 255) |
| 0x11 | `CLEAR_STENCIL` | s 7:0 | 0 |
| 0x12 | `CLEAR_TAG` | t 7:0 | 0 |
| 0x13 | `STENCIL_MASK` | mask 7:0 | 255 |
| 0x14 | `TAG_MASK` | mask 0 | 1 |
| 0x15 | `BITMAP_TRANSFORM_A` | a 16:0, signed 8.8 | 256 |
| 0x16 | `BITMAP_TRANSFORM_B` | b 16:0, signed 8.8 | 0 |
| 0x17 | `BITMAP_TRANSFORM_C` | c 23:0, signed 15.8 | 0 |
| 0x18 | `BITMAP_TRANSFORM_D` | d 16:0, signed 8.8 | 0 |
| 0x19 | `BITMAP_TRANSFORM_E` | e 16:0, signed 8.8 | 256 |
| 0x1A | `BITMAP_TRANSFORM_F` | f 23:0, signed 15.8 | 0 |
| 0x1B | `SCISSOR_XY` | x 21:11, y 10:0 | 0, 0 |
| 0x1C | `SCISSOR_SIZE` | width 23:12, height 11:0 | 2048, 2048 [PG §4.40] (the context table says `HSIZE`, 2048: TO VERIFY); 0 = nothing drawn |
| 0x1D | `CALL` | dest 15:0 | command index (word, not byte) in `RAM_DL` [SW R-Type `ft812_line_cost.py`: `index = word & 0xFFFF`], [SDK `ft_Call`: `dest & 2047`]; 4-level return stack |
| 0x1E | `JUMP` | dest 15:0 | command index, as `CALL` |
| 0x1F | `BEGIN` | prim 3:0 | primitive, §6.4 |
| 0x20 | `COLOR_MASK` | r 3, g 2, b 1, a 0 | all 1 |
| 0x21 | `END` | - | ends the primitive; optional [PG §4.30] |
| 0x22 | `SAVE_CONTEXT` | - | push; a fifth push drops the oldest |
| 0x23 | `RESTORE_CONTEXT` | - | pop; an extra pop loads the defaults |
| 0x24 | `RETURN` | - | return from `CALL` |
| 0x25 | `MACRO` | m 0 | execute the word in `REG_MACRO_0` / `REG_MACRO_1` |
| 0x26 | `CLEAR` | c 2, s 1, t 0 | clear color / stencil / tag buffers |
| 0x27 | `VERTEX_FORMAT` | frac 2:0 | 4 (1/16 px) |
| 0x28 | `BITMAP_LAYOUT_H` | linestride 3:2, height 1:0 | high bits of the last `BITMAP_LAYOUT`; per handle |
| 0x29 | `BITMAP_SIZE_H` | width 3:2, height 1:0 | high bits of the last `BITMAP_SIZE`; per handle; initial 0 |
| 0x2A | `PALETTE_SOURCE` | addr 21:0 | `RAM_G` (0); 2-byte aligned for PALETTED4444/565 |
| 0x2B | `VERTEX_TRANSLATE_X` | x 16:0, signed, 1/16 px | 0 |
| 0x2C | `VERTEX_TRANSLATE_Y` | y 16:0, signed, 1/16 px | 0 |
| 0x2D | `NOP` | - | |
| `01` | `VERTEX2F` | x 29:15, y 14:0, signed, units 1/2^frac px | |
| `10` | `VERTEX2II` | x 29:21, y 20:12 (0…511 px), handle 11:7, cell 6:0 | handle and cell only matter for `BITMAPS` |

Opcodes not in this table (0x2E…0xFF except the vertex forms): TO VERIFY; the emulator
treats them as `NOP` and counts them.

### 6.2 Execution

- The list runs from command 0 to `DISPLAY`, for every visible line (§5.1). `JUMP`
  transfers to a command index; `CALL` pushes the return index (4 levels, "any
  additional CALL/RETURN done will lead to unexpected behavior" [PG §4.19]: the emulator
  stops the line's execution and counts the event). `MACRO` executes one word from the
  macro register as if it were in the list.
- Running off the end of `RAM_DL` without `DISPLAY` (command 2047 reached) ends the list
  (TO VERIFY).
- **Persistent state.** The graphics context (§6.3) is reset to its initial values at the
  start of every line's execution (TO VERIFY: the PG implies it; every program starts its
  list with `CLEAR` and sets what it uses). The **per-handle bitmap parameters**
  (`BITMAP_SOURCE`, `LAYOUT`, `LAYOUT_H`, `SIZE`, `SIZE_H`) are not part of the context:
  they persist across lines, frames and lists until changed [PG §4.1]. VDAC2 software
  depends on that: "BITMAP_SIZE per handle persists between frames" [SW R-Type
  `ft812_line_cost.py`]. Their power-on values: handles 16…31 point at the ROM fonts
  16…31 (§7.6); handles 0…15 are zero (TO VERIFY).
- R-Type also reports that "B/C/D/F persist between frames and CMD_TEXT changes them"
  [SW R-Type `CLAUDE.md`]: the transform coefficients are context, so they persist from
  where a frame's list leaves them only if the context is not reset per line. Together
  with the previous point this is TO VERIFY as one question: **is the graphics context
  reset per line, per frame, or never?** The R-Type observation is evidence for "not per
  frame". The emulator must settle it before L2.

### 6.3 Graphics context

Members and initial values [PG §4.1 Table 5], corrected per §6.1: alpha func + ref;
stencil func + ref + mask; blend src + dst; cell; color A; color RGB; line width; point
size; scissor XY + size; current bitmap handle; transform A…F; clear stencil; clear tag;
stencil mask; stencil op; tag; tag mask; clear color A; clear color RGB; palette source;
vertex format; vertex translate X / Y (TO VERIFY that translate and vertex format are
saved by `SAVE_CONTEXT`: the PG lists vertex format in the table and calls translate
"part of the graphics context").

- `SAVE_CONTEXT` / `RESTORE_CONTEXT` use a 4-level stack [PG §4.39]. Coprocessor widgets
  use one level, leaving 3 for the application [PG §5.9].
- `REG_ROTATE` ≠ 0 transforms vertex coordinates before rasterization as described in
  [PG §2.5.3]; the exact transform for each of the 8 values is TO VERIFY (no VDAC2
  software uses it).

### 6.4 Coordinates and primitives

- Coordinates range from −16384 to 16383 pixels; the visible area is 0…2047 [PG §2.5.2].
  Internally positions are in 1/16 pixel [DS §4.3.1].
- `VERTEX2F`: X, Y are 15-bit signed in units of 1/2^frac pixel; with frac = 4 the range is
  −1024…1023 px, with frac = 3 it is −2048…2047 (R-Type uses 3 to reach 1024×768)
  [PG §4.49, SW R-Type `CLAUDE.md` "VERTEX2F даёт координате 15 бит"].
- `VERTEX2II`: unsigned 0…511 px. `VERTEX_TRANSLATE_X/Y` (1/16 px) is added to **both**
  vertex forms; VDAC2 software places sprites beyond x = 511 that way
  [SW R-Type `ft812_line_cost.py`, the `VERTEX2II` branch: translate applied].
- `BEGIN` primitives [PG §4.5]:

| Value | Primitive | Vertices | Geometry |
|---|---|---|---|
| 1 | `BITMAPS` | 1 per bitmap | top-left corner; size from the handle's `BITMAP_SIZE` |
| 2 | `POINTS` | 1 per point | antialiased disc, radius `POINT_SIZE` (1/16 px) |
| 3 | `LINES` | 2 per line | antialiased line, half width `LINE_WIDTH` |
| 4 | `LINE_STRIP` | connected | as `LINES`, each vertex joins the previous |
| 5…8 | `EDGE_STRIP_R/L/A/B` | connected | fills from the strip to the right / left / above / below screen edge |
| 9 | `RECTS` | 2 per rectangle | top-left and bottom-right corners; `LINE_WIDTH` rounds the corners and grows the rectangle by that amount on each side [PG §2.5.4] |

- Antialiasing applies to all primitives except bitmaps [DS §4.3.1]. The exact coverage
  computation for points, lines, rectangles and edges is TO VERIFY (reference: the BT8XX
  emulator, whose 5.1.26 release states hardware-matching lines, rectangles and points).
- Pixel centers, the inclusive / exclusive rule at primitive edges and the rounding of
  1/16 positions to pixels: TO VERIFY with the same reference.

### 6.5 Bitmaps

**Formats** [PG §4.7 Table 7], values as in [SDK `ft812.h`]:

| Value | Format | Bits | Decoding (byte order as in [PG] Figures 6-9) |
|---|---|---|---|
| 0 | ARGB1555 | 16 | A bit 15, R 14:10, G 9:5, B 4:0 |
| 1 | L1 | 1 | pixel 0 in bit 7 |
| 2 | L4 | 4 | pixel 0 in bits 7:4 |
| 3 | L8 | 8 | |
| 4 | RGB332 | 8 | R 7:5, G 4:2, B 1:0 |
| 5 | ARGB2 | 8 | A 7:6, R 5:4, G 3:2, B 1:0 |
| 6 | ARGB4 | 16 | A 15:12, R 11:8, G 7:4, B 3:0 |
| 7 | RGB565 | 16 | R 15:11, G 10:5, B 4:0 |
| 9 | TEXT8X8 | 8 | each byte is a character code drawn from the 8×8 ROM font (handles 16/17 data) |
| 10 | TEXTVGA | 16 | character + attribute cells, 8×16 ROM font (handles 18/19 data); attribute semantics TO VERIFY |
| 11 | BARGRAPH | 8 | opaque if the byte at x is less than y; up to 256×256 |
| 14 | PALETTED565 | 8 | index into a 16-bit RGB565 palette at `PALETTE_SOURCE` |
| 15 | PALETTED4444 | 8 | index into a 16-bit ARGB4 palette at `PALETTE_SOURCE` |
| 16 | PALETTED8 | 8 | index into a 32-bit palette; one channel per pass, chosen by `PALETTE_SOURCE` offset (+0 B, +1 G, +2 R, +3 A) and `COLOR_MASK` [PG §4.7 note] |
| 17 | L2 | 2 | pixel 0 in bits 7:6 |

- **Expansion to 8 bits per channel** (L1/L2/L4, 5- and 6-bit channels, 2-, 3- and 4-bit
  channels): TO VERIFY; the design uses bit replication (L4 x → x × 17, 5-bit x →
  (x << 3) | (x >> 2), and so on). The `dxt_conv` preview treats L2 levels as 0, 85,
  170, 255 [dxt_conv], which matches replication.
- **Luminance formats** (L1/L2/L4/L8) produce alpha = the value and color = `COLOR_RGB`
  (TO VERIFY that RGB is white × color, alpha is the luminance).
- **Color modulation**: every bitmap pixel is multiplied by `COLOR_RGB` and `COLOR_A`
  (TO VERIFY the rounding, `(a × b + 127) / 255` or `(a × (b + 1)) >> 8`).
- **Layout and size.** Stride = `linestride` (12 bits with `_H`), height 11 bits
  [PG §4.7-4.8]. `BITMAP_SIZE` width / height: 1…2047 with `_H`; **0 means 2048**
  when the high bits are also 0 [PG §4.9]. With wrap = REPEAT the memory width / height
  must be powers of two, otherwise "undefined" [PG §4.9] (the emulator wraps modulo the
  given size).
- **Sampling.** For each covered screen pixel (x, y), relative to the vertex, the
  sample position is `x' = A·x + B·y + C`, `y' = D·x + E·y + F` [PG §2.5.5] with A, B, D, E
  in 8.8 and C, F in 15.8. Whether x, y are pixel centers (x + 0.5) and how x', y' are
  truncated for NEAREST: TO VERIFY. Vertical scale is coefficient E
  [SW R-Type `CLAUDE.md`].
- **Wrap.** BORDER: samples outside the bitmap are transparent; REPEAT: modulo
  [PG §4.9].
- **Cells.** `CELL` / `VERTEX2II` cell n offsets the source by
  `n × linestride × height` (TO VERIFY for TEXT8X8 and fonts).
- Handles 16…31 hold ROM fonts by default [PG §5.5.2]; handle 15 is the coprocessor's
  scratch handle unless moved by `CMD_SETSCRATCH` [PG §4.6].

### 6.6 Per-pixel pipeline

For each pixel a primitive covers, in this order (OpenGL-like, [PG §4.4, §4.18, §4.42]):

1. **Scissor**: outside `SCISSOR_XY` / `SCISSOR_SIZE` → nothing.
2. **Source color**: primitive color (`COLOR_RGB`, `COLOR_A` × coverage) or the
   modulated bitmap sample.
3. **Alpha test** `ALPHA_FUNC(func, ref)` on the source alpha: fail → nothing.
4. **Stencil test** `STENCIL_FUNC(func, ref, mask)` against the stencil buffer;
   `STENCIL_OP(sfail, spass)` updates it, masked by `STENCIL_MASK`. Ops: ZERO 0, KEEP 1,
   REPLACE 2, INCR 3, DECR 4, INVERT 5 [PG §4.44]; TS-Labs also names 6 / 7 as
   INCR_WRAP / DECR_WRAP, "undocumented???" [SDK `ft812.h`] (TO VERIFY).
5. **Blend**: `result = source × src_factor + destination × dst_factor` per channel,
   factors ZERO, ONE, SRC_ALPHA, DST_ALPHA, ONE_MINUS_SRC_ALPHA, ONE_MINUS_DST_ALPHA
   [PG §4.18]. Results saturate at 255. Rounding: TO VERIFY; the TS-Labs DXT preview uses
   `(c0 × (255 − a) + c1 × a + 127) / 255` [dxt_conv].
6. **Color mask** `COLOR_MASK(r, g, b, a)` selects which channels are written; the
   destination alpha channel exists and is written like the others (the TS-Labs "DXT"
   pictures write destination alpha first, then blend with `DST_ALPHA`
   [SDK `test6/src/main.c`, `ftview/src/main.c` `show_dxp`]).
7. **Tag**: with `TAG_MASK` = 1 the tag buffer takes `TAG`.

`CLEAR(c, s, t)` writes the clear values into the selected buffers inside the scissor
rectangle; alpha test, blend and stencil do not apply [PG §4.21]. At the start of each
line the buffers hold: color = 0 with alpha 0, stencil = 0, tag = 0 (TO VERIFY;
programs always `CLEAR` first).

### 6.7 The two display lists

- `RAM_DL` as the host and the coprocessor see it is the **pending** list. The graphics
  engine executes the **active** list. A swap copies pending → active (or exchanges
  them; the observable rule is that after a swap the pending list keeps its contents,
  TO VERIFY).
- Writing `RAM_DL` while `REG_DLSWAP` ≠ 0 is not allowed [PG Register Definition 18]; the
  emulator lets the write happen and the swap takes whatever the pending list holds at
  that moment.

### 6.8 Output stage

The finished line passes through: `REG_ROTATE` (geometry, §6.3), `REG_OUTBITS` +
`REG_DITHER` (no effect at 8 bits, §3.2), `REG_SWIZZLE` (pin order), `REG_CSPREAD`
(one-pixel R / B shift on VDAC2, §3.2). The visible region `HSIZE × VSIZE` is the output
frame.

## 7. Coprocessor

### 7.1 The ring

[PG §5.1]:

- `RAM_CMD` is a 4 KB circular buffer. The host writes words at `REG_CMD_WRITE` and then
  advances it; the coprocessor executes up to `REG_CMD_WRITE` and advances
  `REG_CMD_READ`. Equal pointers = empty and idle. Pointers are multiples of 4.
- Free space = `4092 − ((WRITE − READ) mod 4096)` (never 4096, so a full ring never looks
  empty).
- `REG_CMDB_WRITE` appends words at `REG_CMD_WRITE` and advances it by itself
  [PG §5.1.2]. VDAC2 software uses this path almost exclusively [SDK `ft_ccmd_write`,
  `ft_load_cfifo*`; TSLib `FT.Coprocessor.Write32`].
- A command whose trailing data is not a multiple of 4 bytes is padded; the coprocessor
  skips to the next 4-byte boundary [PG §5.1.1].
- **Display list words in the ring** (anything that is not `0xFFFFFFxx`) are copied to
  `RAM_DL` at `REG_CMD_DL`, which then advances by 4 [PG §5.3]. Writing past command
  2047 is a fault [PG §5.6].
- **Output parameters** (`CMD_MEMCRC` result, `CMD_REGREAD` result, `CMD_GETPTR`,
  `CMD_GETPROPS`, `CMD_GETMATRIX`) are written back into the ring at the parameter's own
  position; the host reads them from `RAM_CMD` after the command completes
  [PG §5.16, §5.24, §5.47].

### 7.2 Timing

- Commands take time. **`REG_CMD_READ` advances as the coprocessor consumes ring words,
  including the data words of a command still in progress**: commands whose data is
  longer than the ring (`CMD_INFLATE`, `CMD_LOADIMAGE`, `CMD_MEMWRITE`, `CMD_PLAYVIDEO`
  without the media FIFO) could not work otherwise, and the TS-Labs SDK feeds one
  command's data in pieces of at most `REG_CMDB_SPACE`, waiting for space between them
  [SDK `ft_load_cfifo`, `ft_load_cfifo_dma`]. The coprocessor never runs ahead of
  `REG_CMD_WRITE`; a command whose data has not arrived yet waits for it. Exactly when the
  command's own words (code and parameters) are released, at the start or at completion,
  and in what steps the data words are released, is TO VERIFY (V19); the design releases
  words as they are consumed.
- **Waiting commands** [PG §5.11]: `CMD_DLSTART` "waits until the current display list is
  scanned out" (until a pending swap has completed) and then sets `REG_CMD_DL` to 0.
  `CMD_SWAP` writes `REG_DLSWAP` = 2 without waiting [PG §5.12].
- **Durations** are TO VERIFY for every command (design §5.4 cost model, §12 below):
  per-command base cost and per-byte / per-pixel costs of `INFLATE`, `MEMCPY`, `MEMSET`,
  `MEMZERO`, `MEMCRC`, `APPEND`, `LOADIMAGE`, and of DL-producing commands (per word
  written). Measured with `REG_CLOCK` read before and after a command; Zuma's profiler
  guide uses this method [SW Zuma `Docs/ft812_profiler_guide.md`].
- `INT_CMDEMPTY` is raised when the coprocessor completes the last command in the ring
  [PG §5.4].

### 7.3 Coprocessor state

[PG §5.7 Table 12]: background color 0x002040, foreground color 0x003870, gradient color
0xFFFFFF; the current matrix (identity); the scratch handle (15); the number base (10);
the media FIFO (address 0, length 0); spinner / trackers / interrupt timer off; and the
font table: handles 0…31 → font metric pointers (16…31 → ROM fonts 16…31 by default,
§7.6). The state is reset by coprocessor reset (`REG_CPURESET` bit 0) and by
`CMD_COLDSTART`, and is not touched by `CMD_DLSTART` / `CMD_SWAP` [PG §5.7].

### 7.4 Faults and recovery

- A fault occurs on an invalid inflate stream, an invalid image, a display list longer
  than 2048 commands [PG §5.6], and (design rule) on a command code the emulator does not
  implement. On a fault the coprocessor sets `REG_CMD_READ` = 0xFFF, raises
  `INT_CMDEMPTY` and stops accepting commands.
- Recovery [PG §5.6, SDK `ft_cp_reset`]: `REG_CPURESET` = 1, `REG_CMD_READ` = 0,
  `REG_CMD_WRITE` = 0, `REG_CPURESET` = 0. While bit 0 of `REG_CPURESET` is 1 the
  coprocessor does nothing; on release it starts at `READ` with its state reset (§7.3).
- `REG_CMD_DL` after recovery: TO VERIFY (the design leaves it unchanged).

### 7.5 Command reference

Codes and layouts [PG §5.11-5.67], codes cross-checked with [SDK `ft812.h`]. "VDAC2 use"
names the software that issues the command (design §3). Words are little-endian 32-bit;
16-bit fields pack two per word in the listed order.

**Display list control**

| Code | Command | Layout | Effect |
|---|---|---|---|
| FF00 | `CMD_DLSTART` | - | wait for a pending swap to complete, then `REG_CMD_DL` = 0 |
| FF01 | `CMD_SWAP` | - | `REG_DLSWAP` = 2 |
| FF32 | `CMD_COLDSTART` | - | coprocessor state to defaults (§7.3) |
| FF1E | `CMD_APPEND` | ptr, num | copy `num` bytes (multiple of 4) from `RAM_G` at ptr to `RAM_DL` at `REG_CMD_DL`; `REG_CMD_DL` += num |

**Memory**

| Code | Command | Layout | Effect |
|---|---|---|---|
| FF1A | `CMD_MEMWRITE` | ptr, num, bytes… | write the following bytes to memory at ptr (any address, including registers) |
| FF1B | `CMD_MEMSET` | ptr, value, num | fill `num` bytes with the low byte of value |
| FF1C | `CMD_MEMZERO` | ptr, num | fill with 0 |
| FF1D | `CMD_MEMCPY` | dst, src, num | copy (overlap behavior TO VERIFY; the design copies forward) |
| FF18 | `CMD_MEMCRC` | ptr, num, result | result ← CRC-32 of the block (the zlib / IEEE polynomial: TO VERIFY the exact variant) |
| FF19 | `CMD_REGREAD` | ptr, result | result ← 32-bit value at ptr |
| FF22 | `CMD_INFLATE` | ptr, deflate data… | decompress a zlib stream (with the 2-byte header, TO VERIFY raw deflate) to ptr; an invalid stream faults |
| FF23 | `CMD_GETPTR` | result | result ← first address after the last `CMD_INFLATE` output |
| FF39 | `CMD_MEDIAFIFO` | ptr, size | define the media FIFO in `RAM_G`; sets `REG_MEDIAFIFO_READ` = `WRITE` = ptr (the PG example prints both as the start address, TO VERIFY) |

VDAC2 use: `MEMWRITE`, `MEMSET`, `MEMZERO`, `MEMCPY`, `INFLATE`, `APPEND` (games); `MEDIAFIFO` (`ftview`).

**Images and video**

| Code | Command | Layout | Effect |
|---|---|---|---|
| FF24 | `CMD_LOADIMAGE` | ptr, options, data… | decode baseline JFIF JPEG or PNG (bit depth 8, not interlaced) to ptr [PG §5.19]. PNG gray → L8, truecolor → RGB565, indexed → PALETTED565 (PALETTED4444 if it has transparency; palette written, `PALETTE_SOURCE` emitted), truecolor + alpha → ARGB4; gray + alpha unsupported. JPEG → RGB565, or L8 with `OPT_MONO` (1). Unless `OPT_NODL` (2), emits `BITMAP_SOURCE` / `LAYOUT` / `SIZE` for the current handle; `OPT_FULLSCREEN` (8) scales to the screen; `OPT_MEDIAFIFO` (16) reads data from the media FIFO. Limits seen on the card: JPEG ≤ 524 288 pixels, PNG ≤ 483 328 pixels, both ≤ 1024×768; progressive and CMYK JPEG fail [forum, design §2.7]. Output exceeding `RAM_G` is "unpredictable" [PG]: the emulator faults |
| FF25 | `CMD_GETPROPS` | ptr, width, height | ← address, width and height of the last `CMD_LOADIMAGE` |
| FF3A | `CMD_PLAYVIDEO` | options, data… | play an M-JPEG AVI from the ring or (`OPT_MEDIAFIFO`) the media FIFO; `OPT_FULLSCREEN`, `OPT_NOTEAR` (4) sync updates to blanking, `OPT_SOUND` (32) decodes the audio track (IMA ADPCM, 8-bit PCM, u-law; 16-bit PCM by dropping the low byte) [PG §5.21]. Frame pacing from the AVI header and `REG_FREQUENCY` (TO VERIFY). On VDAC2 the audio has no output (§3.3) |
| FF40 | `CMD_VIDEOSTART` | - | read the AVI header from the media FIFO |
| FF41 | `CMD_VIDEOFRAME` | dst, ptr | decode the next frame to dst; write 1 to ptr if more frames follow, 0 at the last |

VDAC2 use: `LOADIMAGE`, `MEDIAFIFO`, `PLAYVIDEO` (`ftview`: `show_jpg_png`, `show_avi`).

**Matrix** (the coprocessor's current matrix; 16.16 fixed point)

| Code | Command | Layout | Effect |
|---|---|---|---|
| FF26 | `CMD_LOADIDENTITY` | - | current matrix = identity |
| FF28 | `CMD_SCALE` | sx, sy (16.16) | current = current × scale |
| FF29 | `CMD_ROTATE` | a (1/65536 turn, clockwise) | current = current × rotation |
| FF27 | `CMD_TRANSLATE` | tx, ty (16.16) | current = current × translation |
| FF2A | `CMD_SETMATRIX` | - | emit `BITMAP_TRANSFORM_A…F` from the current matrix (A, B, D, E to 8.8, C, F to 15.8) |
| FF33 | `CMD_GETMATRIX` | a…f | ← the current matrix |

The multiplication order (whether the new transform is applied on the left or the right),
the sine / cosine table of `CMD_ROTATE` and the rounding of each step and of the 16.16 →
8.8 conversion: TO VERIFY. The reference is the BT8XX emulator, which runs the real
coprocessor ROM (design §4), so its output is the chip's output. VDAC2 use: Zuma
(`SCALE`, `ROTATE`, `TRANSLATE`, `SETMATRIX`, `LOADIDENTITY`).

**Text and fonts**

| Code | Command | Layout | Effect |
|---|---|---|---|
| FF0C | `CMD_TEXT` | x, y (int16), font, options (uint16), string, NUL, pad | draw the string with the font's bitmaps: options `OPT_CENTERX` 512, `OPT_CENTERY` 1024, `OPT_CENTER` 1536, `OPT_RIGHTX` 2048 |
| FF2E | `CMD_NUMBER` | x, y, font, options, n (int32) | draw n in the current base; options as `CMD_TEXT` plus `OPT_SIGNED` 256 and a 1…9 zero-padding width in the low bits |
| FF38 | `CMD_SETBASE` | b (2…36) | number base |
| FF2B | `CMD_SETFONT` | font, ptr | font handle → metrics block at ptr (bitmap parameters set separately) |
| FF3B | `CMD_SETFONT2` | font, ptr, firstchar | same, and emits the bitmap parameters; characters start at firstchar |
| FF3F | `CMD_ROMFONT` | font, romslot (16…34) | handle → ROM font, emits its bitmap parameters |
| FF3C | `CMD_SETSCRATCH` | handle | scratch handle for widgets |
| FF43 | `CMD_SETBITMAP` | addr, fmt, width, height (16-bit each), pad | emit `BITMAP_SOURCE` / `LAYOUT` (+`_H`) / `SIZE` (+`_H`) for the current handle, with NEAREST / BORDER / BORDER [PG §5.65] |

- **The exact display list that `CMD_TEXT` and `CMD_NUMBER` emit** (which commands,
  whether they save / restore context, set `BITMAP_HANDLE`, `CELL`, the transform) is not
  in the PG. R-Type observes that `CMD_TEXT` changes transform coefficients B/C/D/F and
  draws the font with the identity matrix [SW R-Type `CLAUDE.md`]. The emitted words are
  TO VERIFY against the BT8XX emulator (real coprocessor ROM).
- Font metrics block [PG §5.5.1, DS Table 4-7]: 128 width bytes, then format, stride,
  width, height, data pointer (32-bit each), 148 bytes. ROM fonts 16…34 are in the
  [DLL] image at `ROM_FONTROOT` (§1).
- ROM fonts 16, 18, 20…34 cover ASCII 0x20…0x7E indexed by code; 17 and 19 cover
  0x80…0xFF indexed by `code − 0x80` [PG §5.5.2]. Fonts 16…19 are fixed 8 pixels wide.
- VDAC2 use: `TEXT`, `NUMBER` (games' debug labels, R-Type's speed label, `test9`),
  `SETBASE`, `SETFONT2`, `ROMFONT` (`test9`), `SETBITMAP` (`ftview`).

**Drawing helpers and widgets**

| Code | Command | VDAC2 use | Notes |
|---|---|---|---|
| FF0B | `CMD_GRADIENT` x0, y0, rgb0, x1, y1, rgb1 | `test9` | linear interpolation `R0 + t × (R1 − R0)` per channel between two points, to be used with scissor [PG §5.34]; emitted words TO VERIFY (it uses the scratch handle) |
| FF09 / FF0A / FF34 | `CMD_BGCOLOR` / `CMD_FGCOLOR` / `CMD_GRADCOLOR` | none | set coprocessor colors |
| FF0D, FF14, FF13, FF0E, FF0F, FF11, FF10, FF2D, FF12 | `BUTTON`, `CLOCK`, `GAUGE`, `KEYS`, `PROGRESS`, `SCROLLBAR`, `SLIDER`, `DIAL`, `TOGGLE` | none | widgets: later (design §8); until then they fault and are logged |
| FF02 | `CMD_INTERRUPT` ms | none seen | raise `INT_CMDFLAG` after ms milliseconds (by `REG_FREQUENCY`), immediately for 0 |
| FF31 | `CMD_LOGO` | none | later |
| FF16, FF2F, FF30, FF17 | `SPINNER`, `SCREENSAVER`, `SKETCH`, `STOP` | none | later |
| FF1F, FF37 | `SNAPSHOT`, `SNAPSHOT2` | none | later |
| FF15, FF20, FF2C, FF36 | `CALIBRATE`, `TOUCH_TRANSFORM`, `TRACK`, `SETROTATE` | none | touch-related: `SETROTATE` sets `REG_ROTATE` and the touch matrix; the rest complete without effect |

Codes in TS-Labs headers that the FT812 PG does not document: `CMD_CRC` FF03,
`HAMMERAUX` FF04, `MARCH` FF05, `IDCT_DELETED` FF06, `EXECUTE` FF07, `GETPOINT` FF08,
`CSKETCH` FF35 (deprecated), `INT_RAMSHARED` FF3D, `INT_SWLOADIMAGE` FF3E, `SYNC` FF42
(BT81x), and the `FLASH*` commands FF44…FF4E (BT81x) [SDK `ft812.h`]. Their behavior on
FT812 is TO VERIFY; the emulator faults on them.

### 7.6 Fonts at reset

Handles 16…31 are bound to ROM fonts 16…31: the coprocessor's font table points at the ROM
metric blocks, and the graphics engine's per-handle bitmap parameters for 16…31 describe
the ROM bitmaps (source, layout, size) [PG §5.5.2]. Whether the graphics engine's
parameters for 16…31 are set at reset, or only when the coprocessor draws text, is
TO VERIFY; the design sets them at reset (programs draw `VERTEX2II(x, y, 31, 'A')`
without any setup [PG §2.5.1]).

## 8. Media FIFO

[PG §3.5, §5.20]: a FIFO in `RAM_G` from `ptr` to `ptr + size`. The host writes data and
advances `REG_MEDIAFIFO_WRITE`; the coprocessor consumes it and advances
`REG_MEDIAFIFO_READ`; both wrap at the end. `ftview` fills a 512 KB FIFO at 0x80000 in
4 KB chunks and polls `REG_MEDIAFIFO_READ` [SDK `ftview/src/main.c` `show_avi`]. Empty =
`READ == WRITE`; the coprocessor waits for data (TO VERIFY how a full FIFO is
distinguished; `ftview` always leaves a chunk free).

## 9. What VDAC2 software expects (checklist)

Each item must work exactly; together they are the acceptance list for L0-L3b.

1. The SDK boot sequence (§2.2) leaves `REG_ID` = 0x7C and `REG_CPURESET` = 0.
2. The mode registers give the TS-Labs frame rates (§4).
3. The first `DLSWAP_FRAME` after boot takes effect and raises `INT_SWAP`.
4. `REG_CMDB_SPACE` = 0xFFC when idle; `& 3` ≠ 0 on fault; recovery works (§7.4).
5. `REG_CMDB_WRITE` bulk writes up to `REG_CMDB_SPACE` bytes, also through TS-Conf DMA
   (`DMA_RAM_SPI`, 512-byte blocks, CS held for the whole transfer [SDK
   `ft_load_cfifo_dma`]).
6. `REG_INT_FLAGS` read-to-clear with `INT_SWAP` once per swap.
7. `REG_FRAMES` counts at the FT812 rate.
8. Bitmaps in PALETTED4444, ARGB4, RGB565, ARGB1555, L1, L2, L4, L8 with NEAREST,
   identity and scaled matrices, `VERTEX_TRANSLATE`, scissor, `COLOR_A`, blend modes
   including `DST_ALPHA` and `COLOR_MASK` (the "DXT" pictures).
9. `CALL` / `RETURN` / `JUMP` / `MACRO`; per-handle bitmap parameters persisting across
   frames.
10. `INFLATE`, `MEMCPY`, `MEMSET`, `MEMZERO`, `MEMWRITE`, `APPEND`; matrix commands;
    `TEXT` / `NUMBER` with ROM fonts; `LOADIMAGE`; `MEDIAFIFO` + `PLAYVIDEO`.
11. Line-by-line drawing from live memory (§5.1).

## 10. Not modeled on VDAC2

Touch (registers read reset values, no touch interrupts), backlight PWM, DISP, GPIO pins,
NOR flash and every BT81x feature, audio samples (timing only), quad / dual SPI electrical
behavior, `REG_RENDERMODE` 1.

## 11. Corrections to the design document

- Dithering is settled: no effect at 8 output bits (§3.2).
- `REG_CSPREAD` = 1 has a defined effect on VDAC2 (§3.2), so it is emulated rather than
  left open.
- The `SCISSOR_*` encoding difference inside TS-Labs code (§0).

The design's §12 and §12.1 are updated with these.

## 12. To verify

Grouped by what can settle them. "BT8XX" = the Bridgetek emulator (Windows), which runs
the real coprocessor ROM; "card" = a real VDAC2.

| # | Item | § | How |
|---|---|---|---|
| V1 | Graphics context reset point: per line, per frame or never (the R-Type observation says not per frame) | 6.2 | BT8XX: a list that sets `COLOR_RGB` after the first vertex and draws a second vertex on the next line; card |
| V2 | Pixel rules: centers, edge inclusion, 1/16 rounding, antialiased coverage of points / lines / rectangles / edge strips | 6.4 | BT8XX golden images (hardware-matching primitives since 5.1.26) |
| V3 | Bitmap sampling: pixel center offset, NEAREST truncation, BILINEAR weights; channel expansion; luminance and color modulation rounding | 6.5 | BT8XX golden images |
| V4 | Blend and stencil arithmetic rounding; stencil ops 6 / 7 | 6.6 | BT8XX; the `dxt_conv` formula as a second check |
| V5 | `CMD_TEXT` / `NUMBER` / `GRADIENT` / `LOADIMAGE` / `SETFONT2` / `ROMFONT` / `SETBITMAP` emitted display list words; matrix command order and rounding | 7.5 | BT8XX (real ROM): capture `RAM_DL` after each command |
| V6 | Coprocessor durations per command and per byte / pixel | 7.2 | card: `REG_CLOCK` before / after |
| V7 | Line budget: overhead below `HCYCLE × PCLK`, the 2048 floor; cost of non-bitmap primitives; what an overflowing line looks like | 5.2 | BT8XX with `DynamicDegrade` for the budget; card for the look |
| V8 | Line look-ahead: which `RAM_G` writes a line still sees | 5.1 | card: a program writing `RAM_G` at known beam positions |
| V9 | Frame origin: where in the frame `REG_FRAMES` increments and the swap happens; the vertical "−1" convention | 3.2, 5.3 | BT8XX register reads around the swap; card with a scope on VSYNC |
| V10 | Reset values where PG and DS disagree: `REG_CPURESET` (0 / 2), `REG_GPIOX` (0x8000 / 0x0080), `REG_FREQUENCY` width (28 / 32); `REG_ADAPTIVE_FRAMERATE` reset and meaning; `REG_DATESTAMP` value | 3 | BT8XX register dump after reset; the [DLL] ROM image |
| V11 | Multi-byte register write semantics; partial `REG_CMDB_WRITE` words; reserved-area reads; MISO during address bytes | 2.1, 3 | BT8XX via `BT8XXEMU_transfer`; card |
| V12 | Time from ACTIVE to `REG_ID` = 0x7C | 2.2 | card |
| V13 | `REG_CSPREAD` = 1 at line edges; `REG_ROTATE` transforms | 3.2, 6.3 | card (CSPREAD), BT8XX (ROTATE) |
| V14 | Swap: copy or exchange of the two lists; writes of 0 / 3 to `REG_DLSWAP` | 5.3, 6.7 | BT8XX |
| V15 | Undocumented opcodes and command codes (§6.1, §7.5); running off the end of `RAM_DL` | 6.1, 7.5 | BT8XX |
| V16 | `CMD_INFLATE` zlib header vs raw deflate; `CMD_MEMCRC` polynomial; `CMD_MEDIAFIFO` initial pointers; `CMD_MEMCPY` overlap | 7.5 | BT8XX |
| V17 | Per-handle bitmap parameters at reset for handles 0…15 and 16…31 | 6.2, 7.6 | BT8XX: draw `VERTEX2II(…, 31, 'A')` right after reset |
| V18 | `CMD_PLAYVIDEO` frame pacing and `OPT_NOTEAR` timing | 7.5 | BT8XX / card with `ftview` |
| V19 | Granularity of `REG_CMD_READ` inside a command: when the command's words are released, in what steps data words are | 7.2 | BT8XX: read `REG_CMD_READ` while feeding a long `CMD_INFLATE` slowly |

The design's §12.1 references this table.
