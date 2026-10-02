# VDAC2 for TS-Conf — technical design

Status: design, nothing implemented. Date: 2026-10-01.
Machine: `TSL` (ZX-Evolution with the TS-Conf firmware).
Sources are listed at the end (§13), by name and public URL. Markers used
throughout:

- **[V]** — the TS-Conf Verilog in `tslabs/zx-evo`;
- **[C]** — the CPLD on the VDAC2 card;
- **[PG]** — the FT81X Programmers Guide v1.2;
- **[DS]** — the FT81x datasheet;
- **[SW]** — the source code of VDAC2 games;
- **[SDK]** — the TS-Labs FT812 SDK, test programs and tools in `tslabs/zx-evo`
  (`pentevo/sdk/ft812sdk`, `pentevo/demos/examples`, `pentevo/tools/dxt_conv`);
- **[forum]** — the TS-Labs forum thread about the card (§13);
- **[PCB]** — the VDAC2 RevB schematic (`pentevo/vdac/vdac2/pcad`, P-CAD 2006 binary; part
  and net names read from the file).

## Glossary

| Term | Meaning |
|---|---|
| VDAC2 | A TS-Labs expansion card for the ZX-Evolution. It plugs into the IDE connector and carries a 24-bit video DAC (ADV7125) plus an FT812 graphics chip. |
| FT812 | A Bridgetek (formerly FTDI) "EVE2" graphics controller. The host writes a list of drawing commands; the chip draws the picture line by line while the monitor scans it, with no frame buffer. |
| Display list (DL) | The list of 32-bit drawing commands the chip executes for every frame. It lives in `RAM_DL` (8 KB, 2048 commands). |
| Coprocessor | A small processor inside the FT812. It reads higher-level commands from a 4 KB ring buffer (`RAM_CMD`) and turns them into display-list commands or memory operations (unpacking, copying, matrix math, text). |
| Swap | Display lists are double-buffered. The host builds the next list and then asks for a swap, which happens at the end of the frame being scanned. |
| `RAM_G` | The FT812's 1 MB of general memory: bitmaps, palettes, fonts. |
| msel | The VDAC2 "media select" line. `V_CONFIG[2]` in TS-Conf decides whether the monitor shows the Evo's own picture or the FT812's. |
| HLE | High-level emulation: reproduce what a command does, without emulating the chip's internal processor and its ROM code. |

## 1. What we are building and why

Run real VDAC2 software unmodified in unreal-ng. Today that means:

- three games, all with sources on GitHub: R-Type (arcade port), Zuma Deluxe and Heroes of
  Might and Magic II;
- the TS-Labs SDK test programs and demos;
- the `ftview` viewer plugin of Wild Commander (JPEG, PNG, AVI). To
the user, the result is that the VDAC2 card goes into the IDE slot instead of the Nemo IDE
controller. The IDE drives disappear, and the machine gains the FT812 video modes: 640×480,
800×600 and 1024×768 at 57 to 85 Hz, up to 16 M colors, with scaling and rotation in
hardware.

No existing emulator can be reused (§4). The FT812 model is written as a **separate
library** with no dependency on unreal-ng and vendored into the tree, as the user asked:
"если нет - придется сделать как отдельную библиотеку и потом ее завендорим".

## 2. Hardware facts (verified against the sources)

### 2.1 The card in the IDE slot

The `quartus_vdac2` firmware build turns the IDE connector into the VDAC2 bus.
`tune.v` defines `IDE_VDAC2` instead of `IDE_HDD`, so the Nemo IDE logic is not in that
firmware at all. The connector pins carry three things [V `top.v:454-508`]:

| Signal group | IDE pins | Direction | Purpose |
|---|---|---|---|
| Evo picture | `ide_d[15:0]` = PAL_SEL, B[4:0], G[4:3], G[1:0], R[4:0], G[2] | FPGA → card while msel = 0 | The Evo's own RGB 5:5:5. The card digitizes it, so the card **is** the machine's video output. |
| Syncs and clock | `ide_cs0_n` = HSYNC, `ide_cs1_n` = VSYNC, `ide_a[2]` = !fclk | FPGA → card | The Evo picture's timing (pixel clock = fclk) |
| msel | `ide_dir` and `ide_wr_n` = `vdac2_msel` | FPGA → card | 1 = show the FT812 picture; also turns `ide_d` into an input |
| SPI to the FT812 | `ide_a[0]` = SCK, `ide_a[1]` = MOSI, `ide_rd_n` = FT CS_n, `ide_rdy` = MISO | both | The FT812 control channel |
| FT812 INT_n | `ide_d[1]` (msel = 1 only) | card → FPGA | The card's CPLD drives `o_r[0]` with `ft_int_n` while msel = 1 [C `top.v:36`] |

**Easy to get wrong:** the FT812 picture does **not** come back into the
FPGA. The card's CPLD switches the monitor output as a whole: RGB, HSYNC, VSYNC **and the
pixel clock** come either from the Evo or from the FT812 [C `top.v:39-45`]. When msel
changes, the monitor sees a different video signal (another resolution and refresh rate)
and has to re-sync. The two pictures are never mixed.

**The card itself** [PCB, C `ide-video.qsf`] holds:

- the FT812 (VQFN-56) with an **8 MHz crystal** (HC-49SMD);
- the ADV7125 video DAC;
- an Altera EPM3128ATC100 CPLD (MAX3000A), purely combinational: the mux, the LUT and the
  INT routing, with no registers and no clock of its own;
- a 1117 LDO, the IDE40 connector, a DB15 VGA connector and a JTAG header.

The FT812 RGB bus is 24-bit (8 bits per channel straight to the DAC). The schematic has an
`AU_L` / `AUDIO` net on the FT812 audio pin, but the card has **no audio output**. In the
forum thread TS-Labs answered whether the sound would be routed: "Нет, потому что там не
звук, а пародия"; it would have needed "4 резика и 4 кондюка" of filter. Jumper J1 affects
colors (§2.7).

In the Evo path, the card's CPLD converts 5-bit channels to 8 bits with PAL_SEL
(= `vdac_mode`) [C `lut`]:

- PAL_SEL = 1: `in << 3`, so the maximum is 248, not 255.
- PAL_SEL = 0: the linear table 0, 10, 21 … 255 for levels 0..24.

**Exact by the hardware (D6).** VDAC2 builds take the Evo colors from this LUT, so
`CramToRgba` for `ts_vdac == 7` must return exactly these values:

- **Direct mode** (CRAM bit 15 = PAL_SEL = 1): `level << 3`, so 0, 8 … 248. Today we return
  `code × 255 / 31`, which makes 31 white (255).
- **Linear mode** (PAL_SEL = 0): the table `0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 117,
  127, 138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245, 255`; 25…31 → 255. That is
  `round(v × 255 / 24)`. Today we compute `v × 255 / 24` with truncation, which is one less
  at levels 11, 14, 17, 19, 20, 22 and 23.

The code carries the table with a comment that points to the CPLD source (`lut` in
`pentevo/vdac/vdac2/cpld/top.v`) and explains why VDAC2 differs from the other builds.
Whether the VDAC1 card has the same rounding is outside this design: a one-line note goes
to the TS-Conf TODO.

### 2.2 The control channel: the TS-Conf SPI controller

The FT812 is the second device on the same Z-Controller SPI bus as the SD card. The Z80
ports are the familiar ones [V `zports.v:284-311, 665-713`]:

| Port | Access | Bits |
|---|---|---|
| `#77` SDCFG | write | D0 MODE (SPI mode 0/3 only with `SPI_MODE_EN`; the VDAC2 build does not define it, so always mode 0) · **D1 = SD CS_n** (0 = SD selected) · **D2 = FT812 CS** (1 = selected, `ftcs_n = ~D2`) · D3 = SD2 CS · D4 = ESP32 CS · D7 = in the ESP32 build, together with D0 = 0, hands the bus to the ESP32 |
| `#77` | read | `0` (the ESP32 build returns `{espcs_int, 0000000}`) |
| `#57` SDDAT | write / read | One byte exchanged. A read returns the byte from the **previous** exchange and starts a new one with `#FF`, which is the same rule as for the SD card. |

The values software uses are `#07` (FT selected, SD deselected) and `#03` (both off)
[SW `_VDAC2_pervye_shagi.txt`]. That gives the read idiom: `OUT` address, `OUT` dummy byte,
`IN` dummy, `IN` data. It matches our `ZControllerSpi` exactly. The MISO line is switched to
the FT812 while its CS is active [V `top.v:1173-1178`].

TS-Conf DMA in RAM→SPI mode (`DMA_RAM_SPI`) streams up to 128 KB per call straight to the
FT812 [SW TSLib `812 Macro.inc:479-528`]. This is the main path for loading textures. The
games turn the Evo's own graphics off (`VID_NOGFX`) so the DMA gets more cycles per line
[SW `platform.asm:72`].

### 2.3 Video output switch and interrupts

- **Output switch.** `V_CONFIG[2]` (`#00AF` bit 2) is `vdac2_msel` [V `video_top.v:136`].
  Like the rest of `V_CONFIG`, it is latched per line, but the card switches the whole
  signal including the syncs. Real software sets it once, after the FT812 is initialized:
  `Video_Setting VID_FT812 | VID_NOGFX` [SW].
- **Interrupt.** With msel = 1, the TS-Conf **line** interrupt is triggered by the falling
  edge of FT812 INT_n instead of the start of a raster line:
  `int_start_lin(vdac2_msel ? int_start_ft : line_start_s)` [V `top.v:1092-1093`]. INT_n
  is synchronized to fclk in two stages [V `top.v:468-471`].
- **On the FT812 side,** INT_N is low while `REG_INT_FLAGS & REG_INT_MASK != 0` and
  `REG_INT_EN = 1`. Flags are cleared when the register is read. The bits are SWAP(0),
  TOUCH(1), TAG(2), SOUND(3), PLAYBACK(4), CMDEMPTY(5), CMDFLAG(6) and CONVCOMPLETE(7)
  [DS §4.1.6].
- **What the games do:** they write `REG_INT_MASK = INT_SWAP`, `REG_INT_EN = 1`, and
  synchronize frames by polling `REG_INT_FLAGS` for SWAP [SW R-Type
  `platform.asm:69-70`, `render.asm:86-92`]. They also poll `REG_FRAMES`.

### 2.4 Build identification

`STATUS[2:0]` (= `VDAC_VER`) is 7 in a VDAC2 build without the ESP32 and 6 in one with it
[V `zports.v:236-247`]. All three games check for **== 7** and refuse to start otherwise
[SW R-Type `platform.asm:47-51`, HMM2 `FT812_STUDY_NOTES.md`]. The current `tune.v` in the
repository (commit `4cd2b81`, 2026-09-23) has `ESP32_SPI` on, which gives 6. That is a
fresh VDAC3 build that today's games would not recognize. **We emulate 7.** Value 6 can be
a later option once an ESP32 model exists (out of scope).

We already have `[MISC] TS_VDAC2=1` → `config.ts_vdac = 7` (STATUS, BLT2 in DMA).
What is missing is the card itself.

### 2.5 FT812: what the emulator needs

| Item | Value | Source |
|---|---|---|
| Memory map | `RAM_G` 0x000000-0x0FFFFF (1 MB) · ROM fonts from 0x1E0000, font table pointer at `ROM_FONTROOT` 0x2FFFFC (contains 0x201EE0) · `RAM_DL` 0x300000 (8 KB) · `RAM_REG` 0x302000 (4 KB) · `RAM_CMD` 0x308000 (4 KB ring) | [PG Appendix A, §ROM fonts] |
| SPI protocol | Mode 0, single channel. **Write:** 3 address bytes with bits 23:22 = `10`, then data; the address auto-increments. **Read:** 3 address bytes with `00`, one dummy byte, then data. **Host command:** 3 bytes, the first one carries the command (`ACTIVE` = `00 00 00`, `SLEEP`, `CLKEXT`, `CLKSEL` + multiplier, `RST_PULSE`, `PWRDOWN`, …) | [PG §2], [DS §4.1] |
| Clock | `CLKEXT` (the card's 8 MHz crystal [PCB]), then `CLKSEL` with `mul \| 0x40`: system clock = **8 MHz × mul** (40…80 MHz, beyond the datasheet's 60 MHz). Pixel clock = system clock / `REG_PCLK` (`f_div` 1 or 2). | [SDK `ft812func.c` `ft_init`, `esp32/src/main/ft8xx.h`], [SW TSLib] |
| Timing | `REG_HCYCLE`/`HOFFSET`/`HSIZE`/`HSYNC0/1`, `REG_VCYCLE`/`VOFFSET`/`VSIZE`/`VSYNC0/1`, `REG_PCLK`, `REG_CSPREAD` (= 0, "critical for correct colors"), `REG_DITHER`, `REG_SWIZZLE`, `REG_OUTBITS` | [SW], [PG §3] |
| Mode table (TS-Labs) | 0: 640×480 57.25 Hz (48/24 MHz) · 1: 640×480 73.96 Hz (64/32) · 2: 640×480 76.34 Hz (64/32) · 3: 800×600 60.32 Hz (40/40) · 4: 800×600 60.32 Hz (80/40) · 5: 800×600 69.30 Hz (48) · 6: 800×600 84.68 Hz (56) · **7: 1024×768 59.08 Hz (64)** · 8: 1024×768 67.27 Hz (72) · 9: 1024×768 76.22 Hz (80) · 10: 640×1024 62.24 Hz (56) · 11/12: 1280×720 58.18 / 60.00 Hz (72) · **13: 800×600 and 14: 1024×768 at 48.7 Hz "for ZX-Evo sync"** (VCYCLE stretched to 748 / 938 lines) | [SDK `ft812func.c:11-27`, `ft8xx.h` table] |
| Modes in the games | R-Type 1024×768 at 59 Hz (HCYCLE 1344), HMM2 640×480 at 74 Hz, Zuma 1024×768, `ftview` mode 7 | [SW], [SDK] |
| Boot sequence | `PWRDOWN`, `ACTIVE`, `SLEEP`, `CLKEXT`, `CLKSEL(mul\|0x40)`, `ACTIVE`, `RST_PULSE` → poll `REG_ID` until 0x7C → poll `REG_CPURESET` until 0 → timing registers, `SWIZZLE`=0, `PCLK_POL`=0, `CSPREAD`=0, `HSIZE`/`VSIZE` → a 3-word DL (`CLEAR_COLOR_RGB`, `CLEAR`, `DISPLAY`) + `DLSWAP_FRAME` → `ADAPTIVE_FRAMERATE`=0, `GPIOX_DIR` bit 15 (DISP), `GPIOX` bits 15/12/9 (DISP high, drive strength, **INT_N push-pull**) → `REG_PCLK` → `INT_MASK`=SWAP, `INT_EN`=1 | [SDK `ft_init`] |
| Coprocessor handshake | Idle means `REG_CMDB_SPACE` == 0xFFC. **Fault** is detected as `REG_CMDB_SPACE & 3 != 0`. Recovery: `CPURESET`=1, `CMD_READ`=0, `CMD_WRITE`=0, `CPURESET`=0 | [SDK `ft_cp_wait`, `ft_load_cfifo`, `ft_cp_reset`] |
| Command FIFO | `REG_CMD_READ` / `REG_CMD_WRITE`, and the FT81x bulk write `REG_CMDB_WRITE` / `REG_CMDB_SPACE` (all three games use it) | [PG §5], [SW] |

### 2.6 Things that do not exist on the card

There is no touch panel, no NOR flash and no audio output (§2.1). The games use TurboSound
FM, General Sound and the AY, never the FT812. `ftview` still plays AVI with `OPT_SOUND`, so
the library models the audio engine's registers and timing (playback positions, the
`PLAYBACK` / `SOUND` interrupt flags) without producing samples for the host. That keeps
`PLAYVIDEO` with sound running at the right pace. Touch and flash registers read reset values
and do nothing.

### 2.7 Practical notes from users of the real card

Facts reported in the TS-Labs forum thread (§13), with the post they come from. They matter
for the emulator, because a picture that fails on the real card must fail here too, and the
same way. They matter for the docs, because users hit them.

| Topic | What is known | Post |
|---|---|---|
| Image size limit | "JPG - кол-во пикселей (Х*У) не должно превышать 524288, размеры не более 1024х768; PNG - кол-во пикселей (Х*У) не должно превышать 483328, размеры не более 1024х768" (creator, 10.12.2023). The answer from TS-Labs the same day: the pictures are shown "из внутренней памяти, где лежат в формате битмапа". | [p31899](https://forum.tslabs.info/viewtopic.php?p=31899#p31899), [p31900](https://forum.tslabs.info/viewtopic.php?p=31900#p31900) |
| Why those numbers | Our arithmetic, not from the thread: the decoded picture is a 16-bit bitmap in the 1 MB `RAM_G`. 524 288 × 2 bytes = 1 MB exactly, so a JPEG may fill all of `RAM_G`. 483 328 × 2 = 944 KB, which leaves 80 KB for the PNG decoder's working memory. The library enforces the limit as the available `RAM_G` at the output address, not as hard-coded numbers. | — |
| JPEG kinds | "требуются во1х Baseline JPEG (это которые грузятся сразу, а не с увеличением разрешения), во2х формата RGB (хоть в даташите про это и не сказано) либо ч/б" (TS-Labs, 30.04.2020). Progressive JPEG and CMYK JPEG do not load; the CMYK case was a user file that would not show. | [p30961](https://forum.tslabs.info/viewtopic.php?p=30961#p30961), [p30959](https://forum.tslabs.info/viewtopic.php?p=30959#p30959) |
| Jumper J1 | "Оказывается у меня всё это время vdac2 показывал в розово-фиолетовом... Поставил перемычку, стало в сером как первоначально" (Frago, 01.05.2021). Without J1 the colors are wrong. What J1 connects is not stated; it is a board option, not something software sees, so the emulator always behaves as with J1 fitted. | [p31579](https://forum.tslabs.info/viewtopic.php?p=31579#p31579) |
| Audio | No audio output on the card (§2.1). | forum, page 2 |

**For the user documentation** (the recipe in §6.5 and the `ftview` notes):

- which images `ftview` can show: baseline JPEG (RGB or grayscale) up to 524 288 pixels;
  PNG up to 483 328 pixels, not interlaced; both up to 1024×768;
- that progressive or CMYK JPEG files and larger images fail on the real card as well.

## 3. What real software uses (sets the scope)

These are the identifiers that occur in the three games' Z80 sources (counted with grep,
[SW]). The TS-Labs programs add the items in the second table [SDK].

| Group | Used | Not used |
|---|---|---|
| Graphics primitives | `BITMAPS`, `RECTS`, `POINTS`, `LINES`, `LINE_STRIP` (one each in HMM2) | `EDGE_STRIP_*`, `LINE_STRIP` in bulk |
| Bitmap formats | **PALETTED4444** (the bulk), ARGB4, RGB565, ARGB1555, L1, L4, L8 | ASTC (BT81x only) |
| Bitmap state | `BITMAP_SOURCE/LAYOUT(+_H)/SIZE(+_H)/HANDLE`, `BITMAP_TRANSFORM_A..F` (scaling, rotation), `PALETTE_SOURCE`, `CELL`, `NEAREST`/`BILINEAR`, `BORDER`/`REPEAT` | — |
| Drawing state | `VERTEX2F`, `VERTEX2II`, `VERTEX_FORMAT`, `VERTEX_TRANSLATE_X/Y`, `COLOR_RGB`, `COLOR_A`, `BLEND_FUNC`, `SCISSOR_XY/SIZE`, `CLEAR`, `CLEAR_COLOR_RGB/A`, `COLOR_MASK`, `POINT_SIZE`, `LINE_WIDTH` | stencil, `TAG`, `ALPHA_FUNC` (not found) |
| DL control flow | `CALL`/`RETURN`, `JUMP`, `MACRO`, `NOP`, `DISPLAY` | — |
| Coprocessor | `CMD_DLSTART`, `CMD_SWAP`, **`CMD_INFLATE`**, **`CMD_MEMCPY`**, `CMD_MEMSET`, `CMD_MEMZERO`, `CMD_MEMWRITE`, `CMD_APPEND`, matrices (`CMD_LOADIDENTITY`, `CMD_SCALE`, `CMD_ROTATE`, `CMD_TRANSLATE`, `CMD_SETMATRIX`), `CMD_TEXT` / `CMD_NUMBER` (once each, debug output) | widgets (`BUTTON`, `GAUGE`, `SLIDER`…), `SNAPSHOT`, `SKETCH`, touch calibration |
| Registers | `DLSWAP`, `FRAMES`, `CMD_*`, `CMDB_*`, `INT_FLAGS/MASK/EN`, `CPURESET`, `ID`, timing, `GPIOX(_DIR)`, `PCLK_POL`, `CSPREAD`, `SWIZZLE`, `OUTBITS`, `DITHER`, `VOL_*` (zeroed) | touch, audio, `MACRO_0/1` (to check) |

| Program | What it adds |
|---|---|
| `test1`-`test3` | `POINTS` with `POINT_SIZE` (antialiased), `LINES` with `LINE_WIDTH`, `COLOR_A`, `BLEND_FUNC`, `COLOR_MASK`, `CLEAR_COLOR_A` |
| `test4`, `test6`, `ftview` DXP, `dxt_conv` | **"DXT" images built from FT812 blending**. A full-size L1/L2/L4 mask writes destination alpha; two RGB565 layers at a quarter of the size, scaled ×4 with `BITMAP_TRANSFORM_A/E` = 64, are mixed through it with `DST_ALPHA` / `ONE_MINUS_DST_ALPHA` and `COLOR_MASK(1,1,1,0)`. Destination alpha, the blend factors and the scaling must be exact, or these pictures come out wrong. |
| `test5` | ZX screens shown through `PALETTE_SOURCE` + `VERTEX_TRANSLATE` |
| `test9` | `CMD_ROMFONT`, `CMD_SETFONT2`, `CMD_SETBASE`, `CMD_GRADIENT`, `CMD_TEXT`, `CMD_NUMBER`: **ROM fonts matter** |
| `ftview` (Wild Commander) | **`CMD_LOADIMAGE` with JPEG and PNG** (FT81x decodes both [PG §5.19]; PNG types 0 → L8, 3 → PALETTED565 …), `CMD_SETBITMAP`, `SAVE/RESTORE_CONTEXT`, **`CMD_MEDIAFIFO` + `CMD_PLAYVIDEO`** (AVI M-JPEG, `OPT_FULLSCREEN \| OPT_SOUND \| OPT_MEDIAFIFO \| OPT_NOTEAR`, `REG_MEDIAFIFO_READ/WRITE`), `REG_FREQUENCY` write; a raw DL file (`.dls`) written straight to `RAM_DL` |
| `demos/examples` `ft_pong`, `tunnel` | asm demos on the same macros |

So the core is a bitmap compositor with a matrix, exact blending, scissor and a short list
of coprocessor commands. On top of that, the Wild Commander viewer needs image decoding and
video playback, and `test9` needs ROM fonts. Widgets, `SNAPSHOT` and `SKETCH` wait until
software appears that uses them.

## 4. Existing emulators: the decision

| Candidate | What it is | Usable? |
|---|---|---|
| Bridgetek **BT8XX Emulator** (`bt8xxemu.dll`, [EVE_Emulator](https://github.com/Bridgetek/EVE_Emulator)) | A behavioral FT80x/FT81x/BT81x/BT820 emulator. **Windows binaries only, no source.** Per AN_281 it runs the **real coprocessor ROM** (the `CoprocessorRomFilePath` parameter) and does not model the INT pin, power commands, coprocessor reset or `CMD_SNAPSHOT`. Version 5.1.26 (2026-09-25) states "hardware-matching" rendering of lines, rectangles and points. | Not as a component: closed, Windows only, multithreaded, not deterministic. **The best available pixel reference** for differential tests on Windows (§9.4). |
| TS-Labs Unreal ([zx-evo-unreal](https://github.com/tslabs/zx-evo-unreal) `Unreal/ft812.cpp`; the same code lives in `tslabs/zx-evo` `pentevo/unreal`) | The only working VDAC2 emulation, as a wrapper around `bt8xxemu.dll`. It shows the picture **in a separate window**, has no msel switching, and fakes the INT: it puts a swap flag into the read of `REG_INT_FLAGS` (0x3020A8) and derives the line INT from it. | As a reference for the SPI and CS integration points [`zc.cpp:35-100`, `tsconf.cpp:950-966`]. |
| ZEsarUX, MAME | TS-Conf exists in ZEsarUX; no FT8xx device in either | No |
| Kaetemi `gdemu` | Gameduino 1 (J1 coprocessor), a different chip | No |
| `gd2-lib`, RudolphRiedel `FT800-FT813`, Bridgetek `EveApps` | Host-side libraries (the code that runs on the MCU) | As a reference for legal host traffic and constants |
| TS-Labs FT812 SDK + ESP32 firmware (`tslabs/zx-evo` `pentevo/sdk/ft812sdk`, `pentevo/esp32`) | The authoritative host side for VDAC2: init, modes, coprocessor handshake, DMA upload, test programs. The ESP32-S3 firmware ("VDAC3") can take over the FT812 SPI bus and drives it itself (console, test DLs). | **Primary reference** for host behavior and the test corpus (§9) |

Searching GitHub repositories for "ft800 emulator", "ft81x emulator" and "eve emulator", and
GitHub code for the emulator's API and class names (`BT8XXEMU_run`,
`FT8XXEMU_EmulatorParameters`, `ft800emu`), finds only HAL glue that calls the closed DLL
(EveApps, Newhaven Display samples). The local mirrors of the TS-Labs repositories, Unreal
forks, Xpeccy, ZXMAK2, ZX-M8XXX, NedoOS and MAME contain no FT8xx emulation either (checked
2026-10-01). **Decision: write our own FT812 library**, using
HLE for the coprocessor (§5.4).

## 5. The `eve-emu` library (working name)

### 5.1 Principles

1. **Independent.** No includes from unreal-ng; C++20 plus the standard library.
   zlib-compatible inflate goes through an interface; the vendored copy uses our
   `core/src/3rdparty/miniz`. It is a separate static library in its own repository (`eve-emu`, user
   decision 2026-10-01; in unreal-ng the submodule `lib/eve-emu`) with a public C API, standardized decoder interfaces and a build option per
   decoder (built-in or supplied by the host). Scope today is FT812 only; the rest of the
   EVE family may follow. Details: [eve-emu-architecture.md](eve-emu-architecture.md).
2. **Deterministic, single-threaded, driven from outside.** The library has no threads and
   no timers of its own. The owner reports elapsed time in FT812 system clock ticks;
   everything inside happens in those ticks (`REG_FRAMES`, swaps, INT). This is the basis of
   TTD.
3. **The picture follows the beam, as on the chip.** The FT812 has no frame buffer: it draws
   each line from the live display list and the live `RAM_G` just before scanning it out.
   Software depends on that: a write into `RAM_G` under the list being shown visibly breaks
   sprites on the real card [SW R-Type `CLAUDE.md`, "большие спрайты глючат"]. So lines are
   drawn in step with FT812 time: before any write that changes what is drawn, the library
   first draws every line the beam has passed (catch-up, like the TS-Conf renderer). While
   the monitor does not show the FT812 (msel = 0), lines are not drawn, but all timing runs.
4. **State is split into control state and memory.**
   - The **control state** is small and trivially copyable: SPI front-end phase, registers
     that are not memory, coprocessor position and matrix, scan position, INT level. It is
     saved whole at every TTD checkpoint.
   - The **memory** is `RAM_G` (1 MB), `RAM_DL` (8 KB, active and pending), `RAM_REG`
     (4 KB) and `RAM_CMD` (4 KB). It is exposed as solid regions aligned to 4 KB pages, each
     with a dirty bitmap. All writes go through one internal function (host SPI, coprocessor
     and DMA streams alike), which marks the page dirty.
   - The owner captures only dirty pages. In unreal-ng that is the TTD v2 memory region
     mechanism (PLAN #40 V1, `TTDRegionDesc`, not built yet), so **VDAC2's TTD support depends
     on V1**. Storing 1 MB at every frame checkpoint is not an option.
   - Snapshots write the memory whole.
   - An operation in flight (inflate, image, video frame, long memory command) is not part
     of the stable state; a record of it lets a restore restart it exactly
     ([eve-emu-architecture.md §7](eve-emu-architecture.md)).
5. **Portable with zero warnings** on MSVC, MinGW, clang and gcc: no OS calls, no SIMD in v1
   (the hot loops are tagged `SIMD-CANDIDATE`).
6. **Covers the FT810…FT813 family** with `FT812` as the default. The differences between
   them (touch type, RGB bus width) only matter for registers we do not emulate.

### 5.2 Public API (sketch)

```cpp
namespace eve
{
    enum class Chip : uint8_t { FT810, FT811, FT812, FT813 };

    struct Config
    {
        Chip chip = Chip::FT812;
        uint32_t externalClockHz = 8'000'000;  // VDAC2: 8 MHz crystal on CLKEXT (§12, D7)
        IInflate* inflate = nullptr;           // CMD_INFLATE backend (miniz in unreal-ng)
        std::span<const uint8_t> romImage;     // optional ROM image 0x1E0000..0x2FFFFF (§5.5)
    };

    class Ft81x
    {
    public:
        explicit Ft81x(const Config& config);

        // --- Host bus (SPI, one byte at a time) ---------------------------
        void Select(bool selected);            // CS_n edge; deselect ends a transaction
        uint8_t Exchange(uint8_t mosi);        // full duplex, returns MISO

        // --- Time ----------------------------------------------------------
        void Advance(uint64_t systemTicks);    // runs the scan timing: REG_FRAMES, swaps, INT
        uint64_t TicksToNextEvent() const;     // next swap / frame boundary / INT change
        uint32_t SystemClockHz() const;        // externalClockHz x CLKSEL multiplier (0 = sleeping)

        // --- Outputs -------------------------------------------------------
        bool IntAsserted() const;              // the INT_N pin level (true = low)
        VideoTiming Timing() const;            // H/V sizes, cycles, pixel clock, frame period
        void SetOutput(uint32_t* framebuffer, uint32_t stride, bool drawing);
                                               // where scanned-out lines go (ARGB8888);
                                               // drawing = false: timing only (msel = 0)
        uint64_t CompletedFrames() const;      // VSYNC count; a new value = a whole frame is out
        LineCost LastLineCost(uint32_t line) const; // rasterizer cost of a line (§5.6)

        // --- State ---------------------------------------------------------
        void Reset();                          // power-on (also the RST_PULSE host command)
        size_t ControlStateSize() const;       // the small part, saved at every checkpoint
        void SaveControlState(uint8_t* out) const;
        bool LoadControlState(const uint8_t* in, size_t size);
        std::span<const MemoryRegion> Regions() const; // RAM_G, RAM_DL (x2), RAM_REG, RAM_CMD:
                                               // base, size, 4 KB dirty bitmap
        void ClearDirty();

        // --- Debugging (read-only, does not change state) -----------------------
        uint8_t Peek(uint32_t address) const;  // 22-bit FT812 address space
        DisplayListView DisplayList(bool active) const;
        CoprocessorView Coprocessor() const;
        bool ProbePixel(uint32_t x, uint32_t y, PixelSource& out) const; // which DL command / bitmap
    };
}
```

`ProbePixel` is the same "where does this pixel come from" query as the TS-Conf video mapper
(PLAN #42). It returns the DL command index, the bitmap handle, the source address in
`RAM_G` and the palette entry.

### 5.3 Inside: the host interface and memory

- A small SPI front-end state machine: address phase (3 bytes) → write stream, or dummy
  byte + read stream, or host command (3 bytes). A deselect cuts any phase short.
- Address decoding by region. Writes to `RAM_REG` run side effects (`REG_DLSWAP`,
  `REG_CMD_WRITE`, `REG_CMDB_WRITE`, `REG_CPURESET`, timing registers, `REG_PCLK`). Reads of
  `REG_INT_FLAGS` clear the flags. `REG_CMDB_SPACE` reads back the free space in the ring.
- Reads from the ROM region return the font image if one is loaded (§5.5), zeros otherwise.
  `ROM_FONTROOT` (0x2FFFFC) always returns 0x201EE0.
- The real chip ignores the address phase of a command sent while it sleeps (before
  `ACTIVE`). The library has to honor `ACTIVE` / `SLEEP` / `PWRDOWN` / `CLKEXT` /
  `CLKSEL` / `RST_PULSE`, because the games' boot sequence waits for `REG_ID` = 0x7C after
  them.

### 5.4 Inside: the coprocessor (HLE)

The real coprocessor is a small CPU that runs code from the FT812's internal ROM. Running
that ROM would require a ROM dump and an emulator of an undocumented processor. Instead, the
library reproduces each command's **effect** from the descriptions in [PG §5]:

- **Handshake (from the sources).** Every program studied paces itself only by
  `REG_CMD_READ` / `REG_CMDB_SPACE` (idle = 0xFFC), `INT_SWAP` and `REG_FRAMES`: the TS-Labs
  SDK (`ft_cp_wait`, `ft_load_cfifo*`), TSLib (`FT.Coprocessor.WaitFlush`, `Write32` with
  space checks) and the three games. None of them waits a fixed time for the coprocessor.
- **Duration (on the chip).** The coprocessor takes time per command, and that time decides
  how long the Z80 waits and so how fast a game runs. Running commands instantly would make
  the emulated machine faster than the real one, so commands advance `REG_CMD_READ` over
  FT812 time by a cost model: a per-command base cost plus a per-byte or per-pixel cost for
  `INFLATE`, `MEMCPY`, `MEMSET`, `LOADIMAGE` and the DL-producing commands.
- **The numbers are not in [PG] or [DS].** Each cost constant is marked **TO VERIFY** in the
  code and in §12.1, with how to measure it. `REG_CLOCK` counts FT812 system clocks and
  can be read over SPI before and after a command; Zuma's `ft812_profiler_guide.md`
  already uses it this way. Until measured, the constants are estimates and say so.
- **v1 commands:** `DLSTART`, `SWAP`, `INFLATE`, `MEMCPY`, `MEMSET`, `MEMZERO`, `MEMWRITE`,
  `MEMCRC`, `APPEND`, `LOADIDENTITY`, `SETMATRIX`, `SCALE`, `ROTATE`, `TRANSLATE`,
  `GETMATRIX`, `GETPTR`, `INTERRUPT`, `COLDSTART`, `SETFONT`/`SETFONT2`, `SETBITMAP`,
  `TEXT`, `NUMBER`. Any display-list word in the ring is copied into `RAM_DL` at
  `REG_CMD_DL`.
- **Fixed-point math.** Matrix math must give the same coefficients as the real chip
  (16.16; rotation uses a sine table). Where [PG] does not say how rounding works, the
  values are checked against the BT8XX emulator (§9.4).
- **Unknown or unsupported command:** the real chip's fault behavior. On FT81x the fault is
  `REG_CMD_READ = 0xFFF`, so `REG_CMDB_SPACE` reads with its low bits set. The TS-Labs SDK
  tests `& 3` and recovers with the `CPURESET` sequence (§2.5); the games do the same through
  `FT_CMD_RESET`. Each one is logged once.
- **Image and video (phase L3b):** `ROMFONT`, `SETBASE`, `GRADIENT`, `LOADIMAGE` (JPEG
  baseline and PNG without Adam-7 [PG §5.19]; the output format follows the PNG color
  type / JPEG to RGB565 or L8), `MEDIAFIFO` + `REG_MEDIAFIFO_READ/WRITE`, `PLAYVIDEO`
  (AVI M-JPEG; audio as in §2.6). Decoders go through interfaces like inflate (`lodepng` is
  already in `core/src/3rdparty`; JPEG needs a small baseline decoder). Pixel-exact JPEG
  output against the real chip is not expected: its IDCT is not documented. The limits
  users observe on the real card (§2.7) are reproduced: the library rejects the same inputs
  the chip rejects.
- **Later (no software uses them):** the widgets, `SNAPSHOT`, `SKETCH`, touch calibration.

### 5.5 ROM fonts

`CMD_TEXT` and `CMD_NUMBER` with fonts 16-34 draw glyphs from ROM. The games use them only
for debug lines, but the TS-Labs `test9` uses `CMD_ROMFONT`, so a picture comparison needs
the real glyphs. Options, in order of preference:

**Source: the ROM image inside the Bridgetek emulator DLL.** Both `bt8xxemu.dll` builds,
Bridgetek's own (EVE_Emulator 5.1.26) and the one TS-Labs ships in `zx-evo-unreal/Unreal/cfg`,
carry the FT81x ROM uncompressed. Verified 2026-10-01:

- The font table sits where `ROM_FONTROOT` points (0x201EE0): 19 metric blocks of 148
  bytes for fonts 16…34. Fonts 16…25 are L1 (8×8 up to 30×38); fonts 26…34 are L4,
  antialiased (14×16 up to 78×108, font 34 at 0x1E1B5C).
- Glyphs drawn from it are correct ('A' in fonts 16 and 34).
- The 1 MB at 0x200000…0x2FFFFF is byte-identical in the two DLL versions.

A small tool (`tools/`, Python) extracts and validates the image: it finds the metric
table, checks all 19 blocks and draws a test string. The image stays a local file outside
git. The library takes it as an optional input
(`Config::romImage`) and serves it to SPI reads of the ROM area. Without the image, ROM
reads return zeros and `CMD_TEXT` with ROM fonts draws nothing; the TS-Conf log says so
once.

### 5.6 Inside: drawing a display list

- **Model.** A scanline renderer like the real chip's. For each line, run the DL from the
  start with the graphics state (the state stack is 4 deep in `SAVE/RESTORE_CONTEXT`) and
  draw into a line buffer in 8-bit RGBA. Blending follows `BLEND_FUNC`; `COLOR_MASK` and
  `SCISSOR` apply. Coordinates use `VERTEX_FORMAT` (1/16 px by default) and
  `VERTEX_TRANSLATE`.
- **Bitmaps.** The formats in v1 are those in §3: PALETTED4444/565/8 (FT81x keeps the
  palette in `RAM_G` at `PALETTE_SOURCE`), ARGB1555/ARGB4/ARGB2, RGB565/RGB332, L1/L2/L4/L8,
  TEXT8X8, TEXTVGA and BARGRAPH. Each pixel of a primitive maps back through the matrix
  `BITMAP_TRANSFORM_A..F`, then the image is filtered (`NEAREST`/`BILINEAR`) and wrapped
  (`BORDER`/`REPEAT`). `CELL` offsets by `LAYOUT_H × stride × height`.
- **The line cost, as on the chip.** The rasterizer walks the whole display list again for
  every line: a command costs one FT812 clock on every line, and a subroutine called twice
  costs twice. Filling costs per pixel: PALETTED4444 with NEAREST draws 8 pixels per clock,
  BILINEAR 2 [SW R-Type `ft812_line_cost.py`, `CLAUDE.md`]. Over budget, lines break on the
  real card; R-Type keeps its worst line under about 1300 clocks at HCYCLE 1344 and reports
  no broken lines on the board since then.
  - The library computes this cost for every line and exposes it (`LastLineCost`, the
    automation surfaces in §6.5). For developers of VDAC2 software this is the most useful
    readout there is.
  - The cost of each format and filter, the exact budget, and **what a broken line looks
    like** on the chip are **TO VERIFY** (§12.1). The overflow rendering is implemented once
    that is known; until then an overflow is counted and reported, never hidden.
- **Pitfalls verified on the chip, which the emulator must reproduce** [SW R-Type `CLAUDE.md`]:
  - `VERTEX2F` coordinates are 15 bits;
  - vertical bitmap scale is coefficient E, not D;
  - `BITMAP_TRANSFORM_B/C/D/F` persist between frames, and `CMD_TEXT` changes them;
  - the built-in font is a bitmap and is drawn with the identity matrix;
  - `BITMAP_SIZE` per handle persists between frames.

  R-Type also notes that bt8xxemu culled a whole bitmap with a negative X. Whether the chip
  does the same is **TO VERIFY**; until then the PG rule (clipping, not culling) applies.
- **Exact blending is a requirement, not a nicety.** The TS-Labs "DXT" images (§3) work
  only if destination alpha, `COLOR_MASK`, `DST_ALPHA` / `ONE_MINUS_DST_ALPHA` and the ×4
  scaling match the chip. The TS-Labs converter's preview (`dxt_conv`) blends with
  `(c0 × (255 − a) + c1 × a + 127) / 255` and L2 levels 0/85/170/255. That gives a
  second, independent check besides the BT8XX emulator.
- **Primitives.** `POINTS` (antialiased circles), `LINES`/`LINE_STRIP` (with `LINE_WIDTH`),
  `RECTS` (rounded corners at width > 1). Edge strips and stencil come in the same pass if
  they are cheap; they are required in v2.
- **Fast path.** Most frames in the games are `BITMAPS` with an identity matrix, NEAREST
  filtering and an opaque or simply blended palette. That case is a separate loop without
  per-pixel matrix math. A/B benchmarks measure it; the naive version comes first
 .
- **Display limits.** On the real chip, a line whose drawing does not fit in the time of one
  line comes out broken. We do not reproduce that in v1, so the emulator never shows
  artifacts the real chip would have. It goes into the docs as a known difference, and a
  per-line command budget can be added later.
- **Output.** The visible region is `HSIZE × VSIZE`, without blanking. VDAC2 software
  leaves the output at 8 bits per channel (`OUTBITS` = 0 on FT812), and the ADV7125 takes
  8 bits, so the output is ARGB8888. `SWIZZLE` is applied as documented. For `DITHER`,
  `CSPREAD` and `PCLK_POL` see §12, D5.

### 5.7 Inside: time and frames

- Frame period = `HCYCLE × VCYCLE / pixel clock`; pixel clock = system clock / `REG_PCLK`
  (`REG_PCLK = 0` stops scanning). `Advance` moves the scan position. Each wrap past the
  last line increments `REG_FRAMES`, applies a pending `DLSWAP_FRAME` (copying the new list
  in and clearing `REG_DLSWAP`), sets the SWAP flag and recomputes INT_N.
- `DLSWAP_LINE` is applied at the end of the current line.
- The chip draws a line into a line buffer during the previous line's scan-out. How far
  ahead it draws (one line, or a fixed number of clocks) decides exactly which writes a
  line still sees. It is **TO VERIFY**; the design draws line N during line N − 1.
- `TicksToNextEvent` lets the owner schedule the next edge exactly, without stepping the
  FT812 every Z80 instruction.

## 6. Integration into unreal-ng

### 6.1 The card in the IDE slot

- In the machine configuration, the IDE connector of the `TSL` machine holds one of two
  things: **Nemo IDE** (default) or **VDAC2**. This works like the existing choice between
  the 5BIT / VDAC builds and becomes a single `TS_VDAC2=1` key, already present in
  `config.cpp:744-761`. Choosing VDAC2:
  - removes the Nemo IDE: the config forces `config.ide_scheme = IDE_NONE` for this build
    (`config.cpp:396`), so `TryIdePortIn` / `TryIdePortOut` find no IDE, exactly as with
    `[HDD] Scheme` absent. No new check on the port path; reads fall through as in a firmware
    without `IDE_HDD`;
  - greys out the IDE slots (`ide0.master` / `ide0.slave`) in the media manager, and they
    refuse insert requests from all surfaces with a clear reason;
  - adds the FT812 device and its picture.
- The choice is fixed when the machine is created (as on real hardware, where the card is
  swapped with the power off and the FPGA reflashed). Changing it means recreating the
  machine; there is no hot plug.

### 6.2 The SPI bus: from one device to a CS hub

`ZControllerSpi` today has one device and one CS line (`csN` from D1). It becomes a hub:

- **Devices:** CS0 = SD card (D1, active low), CS1 = FT812 (D2, active high), CS2 = SD2
  (D3), CS3 = ESP32 (D4). In VDAC2 builds only CS0 and CS1 have devices.
- **Rule:** `Exchange` goes to the selected device. With no device selected, the line
  returns `#FF`. If several are selected at once, MISO is the AND of their outputs: the line
  is pulled up, and any device driving 0 wins. In the RTL the MISO mux prefers the FT812
  [V `top.v:1175`], so we follow it: the FT812 overrides.
- **State:** `State` grows to `{csMask, rxLatch}`. The TTD blob `EvoSdCard` keeps reading
  the old layout (versioned).
- **Cost:** in builds without VDAC2 the hub sees exactly one device, and the fast path stays
  as it is now (the same single `if`).
- The DMA in `RAM_SPI` / `SPI_RAM` modes already goes through `_zc`, so it reaches the
  FT812 automatically.

### 6.3 The VDAC2 card device

`Vdac2Card` (`core/src/emulator/platforms/tsconf/vdac2card.{h,cpp}`):

- owns the `eve::Ft81x`, implements `SpiDevice` for CS1, and passes in `IInflate`
  (miniz) and the ROM fonts, if a file is configured;
- **time:** it converts Z80 tacts to FT812 system ticks (Z80 tact = 1/3.5 MHz × the turbo
  factor, so it goes through the master clock). It calls `Advance` lazily: on every access
  over SPI, at the TS-Conf frame end, and at the point `TicksToNextEvent` reports when the
  INT is in use;
- **INT:** a falling edge of INT_N while msel = 1 becomes `int_start_ft` in
  `TsConfInterrupts`. Today that controller latches the line event at raster tact
  `224 n − 1` on each of the 320 lines (`tsconfinterrupts.h`). With msel = 1 that source is
  replaced by the FT812 edge, at the Z80 tact the card computes from `TicksToNextEvent`
  (§2.3). With msel = 0 the INT is not
  seen by the Evo at all: the CPLD does not route it [C];
- **state:** a new TTD blob `Vdac2` (the next free id; 25 at the time of writing,
  `ttdserializable.h`). It holds the library's control state and the card's own fields
  (time remainder, last INT level), a few dozen bytes. The library's memory regions
  register as TTD v2 device regions (PLAN #40 V1), so only dirty 4 KB pages go into the
  page store. Until V1 exists, VDAC2 machines cannot be recorded; the card says so instead
  of recording something it cannot restore.

### 6.4 Video output

- **Rule from the hardware:** the monitor shows the FT812 picture if msel = 1 at the end of
  the frame's visible area. A frame that is split by msel does not exist on real hardware:
  the monitor would lose sync. Software switches msel once, so this rule is enough. It is
  checked against the actual per-line `vConfig` from `TsConfEngine`, which is already
  latched.
- **Two clocks, as on the hardware.** The Z80, the TS-Conf video, sound and the emulator's
  frame pacing keep running on TS-Conf time (48.83 Hz with Pentagon timing), because the
  machine does not slow down or speed up when the card shows the FT812. The FT812 scans
  out on its own clock (§5.7), and with msel = 1 the monitor shows exactly that signal.
- **What reaches the screen.** Every FT812 frame is drawn line by line in FT812 time
  (§5.1, principle 3). At each FT812 VSYNC the finished frame is latched into the
  presentation queue (`Screen::LatchFramebuffer`), so the queue receives frames at the
  FT812 rate (59.08 Hz in mode 7, 48.7 Hz in modes 13/14), not at the TS-Conf rate. The host
  window shows the newest latched frame at each host refresh, the same way it shows any
  machine frame.
- **What follows from that.** Video recording with msel = 1 records the FT812 frames at the
  FT812 rate. TTD and the frame cache step by machine frames as before; the picture shown at
  a TTD position is the last FT812 frame completed by then.
- **No sampling or mixing invented.** A machine frame does not pick an FT812 frame. The A/V
  delay applies in time (microseconds), not in a count of machine frames.
- **Size.** `ScreenTSConf` gets a second framebuffer for the FT812 at `HSIZE × VSIZE` (up to
  2048×2048; real modes are up to 1024×768). Switching msel or the FT812 mode changes the
  `FramebufferDescriptor` and posts `NC_VIDEO_MODE_CHANGED`. The guest-programmed AlCo,
  Profi and ATM modes already use that path (`screen.cpp:627`), and the Qt main window
  re-attaches on it (`mainwindow.cpp:1789`). Recording, screenshots, `capture_media` and the
  video wall have not been checked yet; that is part of I2.
- **Cost.** With msel = 1 every FT812 line is drawn: about 46 M pixels per second in
  mode 7, which the fast path (§5.6) has to carry. With msel = 0 only the timing runs,
  which costs a few comparisons per SPI access and per FT812 frame. `REG_PCLK = 0` or
  `PWRDOWN` gives black.
- **Debug view:** an optional "show the FT812 regardless of msel" for development (a
  separate preview in the debugger, like TS-Labs' window). It is not a hardware mode.

### 6.5 Automation surfaces

All of these get docs, an OpenAPI entry and a recipe.

| Surface | Addition |
|---|---|
| WebAPI | `GET /api/v1/emulator/{id}/state/vdac2`: card present, msel, FT812 mode (H/V sizes, frequency), `REG_ID`, `REG_FRAMES`, INT flags/mask/en, coprocessor pointers, fault. `GET …/state/vdac2/displaylist?active=1` (decoded DL). `GET …/vdac2/memory?address=&length=` (`RAM_G` / `RAM_DL` / `RAM_REG` / `RAM_CMD` read-only). `GET …/vdac2/frame` (PNG of the FT812 picture regardless of msel). `GET …/video/pixel` with layer `ft812` (from `ProbePixel`). |
| CLI | `state vdac2`, `vdac2 dl [active\|pending]`, `vdac2 mem <addr> <len>`, `vdac2 frame <file>` |
| MCP | `inspect_state` aspect `vdac2`; `capture_media` with the source `vdac2` |
| Lua / Python | `vdac2_state()`, `vdac2_display_list()`, `vdac2_read(addr, len)`, `vdac2_frame()` |
| Qt | later: an FT812 debugger dock (DL with highlighting, `RAM_G` as bitmaps by handle, registers). Not in v1, following the TS-Conf decision ("debug UI - потом"). The API above is complete enough that the dock is integration only. |
| Recipe | `.recipe/machines/tsconf-vdac2.md`: how to enable the card, put a game on the SD card, start it, take a picture; which images `ftview` accepts (§2.7) |

## 7. Isolation and performance

- Shared files (`zcontrollerspi`, `screen`, automation) must not mention VDAC2 or the
  FT812, except through an interface (the SPI device, a second framebuffer). The isolation
  test (no `platforms/tsconf/` and no `MM_TSL` in shared files) stays green.
- With VDAC2 off: no new instructions on the per-instruction or per-port paths. With VDAC2
  on: SPI cost per byte = one call to the state machine.
- New benchmarks in `core/benchmarks/`:
  - `BM_Ft81xRender_Bitmaps1024` (a typical R-Type frame from a capture);
  - `BM_Ft81xRender_Transform` (scaling / rotation);
  - `BM_Ft81xInflate`;
  - `BM_TsConfFrame_Vdac2` (the whole machine with VDAC2 against without it).
- Target: the whole frame with an FT812 picture at 1024×768 costs no more than 2x a plain
  TS-Conf frame.

## 8. Plan of work

**No code before the designs.** This document fixes the hardware facts, the scope and the
architecture. The detailed designs below come first, and each is reviewed before the code
it describes is written.

| Phase | Content | Done when |
|---|---|---|
| **D-A** | **FT812 behavior specification** ([ft812-behavior-spec.md](ft812-behavior-spec.md), draft 2026-10-01), one section per area: SPI front end and host commands; registers (reset values, side effects, read behavior); display list commands (state, primitives, bitmap formats, matrix, blending, scissor, CALL/JUMP/MACRO); coprocessor commands with their RAM_CMD and RAM_DL effects; timing (scan, swap, INT); the line cost. Each rule carries its source ([PG] page, [SDK] or [SW] file) or a TO VERIFY mark | reviewed |
| **D-B** | **Library architecture** ([eve-emu-architecture.md](eve-emu-architecture.md), draft 2026-10-01): modules, data layout of control state and memory regions, the catch-up drawing pipeline, the cost model, decoder interfaces, the public API in full | reviewed |
| **D-C** | **Integration design** ([vdac2-integration-design.md](vdac2-integration-design.md), draft 2026-10-01): CS hub and TTD layout versioning of `EvoSdCard`; `Vdac2Card` and its clock conversion; the INT path in `TsConfInterrupts`; the second framebuffer and FT812-rate presentation in `Screen`; the exact Evo LUT (D6); the IDE-off config; TTD v2 regions (dependency on PLAN #40 V1); automation surfaces | reviewed |
| **D-D** | **Test corpus and oracles** ([vdac2-test-corpus.md](vdac2-test-corpus.md), draft 2026-10-01): TS-Labs SDK programs, demos, games, `ftview` files; golden-image procedure from the BT8XX emulator; the ROM extraction tool; what each test proves | reviewed |
| L0-L3b | Library, in the order of D-B (host side and timing, renderer, coprocessor, images / video / ROM fonts) | per D-B and D-D |
| I1-I4 | Integration, in the order of D-C (bus and card, video and INT, TTD and automation, speed) | per D-C and D-D |
| later | Widgets, `SNAPSHOT`, `SKETCH`, stencil / edge strips | when software that needs them appears |

The library is `eve-emu`, a separate static library (D-B).

## 9. Testing

1. **Library, unit level** (in the library repository, mirrored in the vendored copy):
   - SPI framing (address, dummy byte, cut-off transactions);
   - auto-increment across region boundaries;
   - `REG_INT_FLAGS` cleared on read;
   - INT_N with mask / en;
   - frame period from the mode registers, for each of the 15 TS-Labs modes, against the
     frequencies in the SDK's table (§2.5);
   - the SDK boot sequence (`ft_init`) and the fault and recovery sequence, as a byte script;
   - swap at the frame / line end.
2. **Golden images.** Small display lists, one feature each (a format, a transform, a
   blend mode, a primitive). Reference PNGs come from the BT8XX emulator on Windows, and a
   few from the real chip if the user can capture them. Comparison is exact for NEAREST
   and within a tolerance for BILINEAR / antialiasing (bounds fixed in the test).
3. **Traffic replay.** The SPI stream of a game (captured by a hook in our emulator, or
   generated from `gd2-lib` sequences) is replayed into the library. Then `RAM_G`, `RAM_DL`
   and the picture are compared against reference dumps.
4. **Differential testing against `bt8xxemu.dll`** (Windows, an optional CI job): the same
   SPI stream into both, compared after each swap. INT is excluded (BT8XX does not model it),
   and so are known HLE differences (a list kept in the tests).
5. **Whole machine** (`core/tests/emulator/machines/tsconf/`):
   - the TS-Labs SDK test programs (`test1`-`test6`, `test9` `.spg`, public in
     `tslabs/zx-evo`), plus `ft_pong` and `tunnel`: small, deterministic, one feature each;
     the first targets;
   - the games from `testdata/machines/tsconf/vdac2/`, local and untracked like Wild
     Commander, so those tests skip without them;
   - `ftview` through Wild Commander with JPEG / PNG / DXP / AVI files.
   - Boot to the title, then a screenshot fixture.
   - `STATUS & 7 == 7`.
   - The Nemo IDE does not respond.
   - The line INT comes from the FT812 when msel = 1.
6. **Isolation and TTD:** the isolation test, a record / replay round trip, and old TTD
   files with `EvoSdCard` still load.

## 10. Risks

| Risk | What we do |
|---|---|
| Coprocessor HLE gives slightly different coefficients or pixels | differential tests against BT8XX; known differences are listed in the tests and the docs |
| Drawing 1024×768 at 59 Hz in software is expensive | lines are drawn only while msel = 1; fast path for plain bitmaps; benchmarks before optimizing |
| Software depends on the timing of the coprocessor or SPI | instant execution first; a cost model as an option if a game breaks |
| Changing the frame size breaks recording / the video wall / screenshots | check every consumer in I2, as with Profi / ATM |
| Mid-frame `RAM_G` changes (the real chip draws line by line from live memory) | drawn in step with the beam by design (§5.1); the exact look-ahead of the line buffer is TO VERIFY |
| TTD for VDAC2 depends on TTD v2 memory regions (PLAN #40 V1) | V1 goes first; until then VDAC2 machines refuse to record instead of recording incompletely |

## 11. Assumptions corrected during the design

Points that an earlier reading got wrong and this design corrects:

- the video path (the card is the output, not a source into the FPGA);
- the size of `RAM_G` (1 MB, not 256 KB);
- the library: a separate static library in its own repository, not a folder in
  `core/src/emulator/io/`;
- the scope, which is set by what the three games actually use, not by the full command set;
- `VDAC_VER` (6 against 7);
- the clock (an external 8 MHz × MUL, up to 80 MHz);
- image and video decoding (`ftview` uses them, and FT81x decodes PNG as well as JPEG);
- the state: control state plus memory regions with dirty pages (TTD v2 regions), not one
  blob;
- the picture: drawn in step with the FT812 beam and presented at the FT812's own frame
  rate, not once per machine frame.

## 12. Decisions

All decisions follow the hardware; where the sources do not settle a detail, it is marked
TO VERIFY (§12.1) in this document and, later, at the exact place in the code.

| # | Topic | Decision | From |
|---|---|---|---|
| D1 | ROM fonts | The FT81x ROM image extracted from `bt8xxemu.dll` (Bridgetek's or the one TS-Labs ships); verified glyphs; kept outside git; loaded as an optional file (§5.5) | DLL analysis 2026-10-01 |
| D3 | Coprocessor execution time | Takes FT812 time by a cost model, as on the chip; the constants are TO VERIFY (§5.4) | handshake in [SDK], [SW] |
| D4 | Frame rate on screen | FT812 frames drawn with the beam and latched at the FT812's own VSYNC; the machine keeps TS-Conf time (§6.4) | the card switches the whole video signal [C] |
| D5 | `DITHER`, `OUTBITS`, `CSPREAD`, `SWIZZLE`, `PCLK_POL` | Software leaves the output at 8 bits; dithering then changes nothing ("the graphics engine computes the colour values at an 8 bit precision" [DS §4.4]). `SWIZZLE` is applied per [DS Table 4-12]. `CSPREAD` = 1 moves R one pixel clock early and B one late [DS §4.4]; VDAC2 latches all 24 bits on one edge, so the picture gets a one-pixel R / B fringe, which the emulator reproduces (TS-Labs: 0 is "critical for correct colors"). Details: [spec §3.2](ft812-behavior-spec.md) | [SDK `ft_init`], TSLib, [DS §4.4] |
| D6 | Evo palette through the card | Exactly the card's LUT: direct `level << 3`, linear `round(v × 255 / 24)` (§2.1) | [C `lut`] |
| D7 | Card clock | 8 MHz crystal | [PCB], [SDK] |
| D8 | VDAC3 / ESP32 (`VDAC_VER` = 6, CS3; the ESP32 firmware can drive the FT812 itself) | Out of scope; the CS hub leaves room for it | [V], [SDK] |
| D9 | FT812 audio | Not routed on the card; registers and timing only | [forum] |

### 12.1 To verify

The complete list for the chip is the table in
[ft812-behavior-spec.md §12](ft812-behavior-spec.md) (V1…V18). The items below are the
ones that touch the design decisions.

Each item names how it can be settled. Items that need the real card are collected for
whoever has one; items that the BT8XX emulator can answer are checked there first.

| Item | Where it matters | How to settle |
|---|---|---|
| Coprocessor cost per command and per byte / pixel (`INFLATE`, `MEMCPY`, `MEMSET`, `LOADIMAGE`, DL commands) | game speed (§5.4) | `REG_CLOCK` before / after each command on the card (Zuma's profiler guide shows the method) |
| Line cost per format and filter; exact per-line budget; what a broken line looks like | §5.6 | BT8XX emulator with `DynamicDegrade` on (it models degradation); the card for the look |
| Line buffer look-ahead (how far ahead of the scan a line is drawn) | which `RAM_G` writes a line sees (§5.7) | a test program that writes `RAM_G` at known beam positions, on the card |
| Negative X: clipped or the whole bitmap culled | §5.6 | the card (R-Type saw culling in bt8xxemu) |
| Pixel rules: point / line antialiasing, bilinear filter, blend rounding, matrix rounding in `CMD_ROTATE` / `CMD_SCALE` | golden images | BT8XX emulator 5.1.26 ("hardware-matching" primitives); the `dxt_conv` formula for blending |
| JPEG IDCT output | `ftview` | BT8XX emulator (exactness against the chip not expected) |
| `CSPREAD` = 1 at the first and last pixel of a line | D5 | the card |
| `VDAC_VER` of the released VDAC2 firmware (6 with `ESP32_SPI` or 7) | §2.4 | `STATUS` read on a card with the current firmware |

## 13. Sources

**Hardware (TS-Labs)**
- TS-Conf firmware, VDAC2 build: https://github.com/tslabs/zx-evo/tree/master/pentevo/fpga/current/quartus_vdac2 (`tune.v`)
- RTL: https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/top.v ,
  https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zports.v ,
  https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/video/video_top.v
- CPLD and PCB of the card: https://github.com/tslabs/zx-evo/tree/master/pentevo/vdac/vdac2
- FT812 SDK, test programs and the `ftview` viewer: https://github.com/tslabs/zx-evo/tree/master/pentevo/sdk/ft812sdk
- Asm demos (`ft_pong`, `tunnel`, `includes/ft81x.asm`): https://github.com/tslabs/zx-evo/tree/master/pentevo/demos/examples
- DXT converter: https://github.com/tslabs/zx-evo/tree/master/pentevo/tools/dxt_conv
- ESP32-S3 firmware (FT8xx master, mode table with frequencies): https://github.com/tslabs/zx-evo/tree/master/pentevo/esp32
- TS-Conf documentation: https://github.com/tslabs/zx-evo/tree/master/pentevo/docs/TSconf
- Chip documents in the TS-Labs repository: https://github.com/tslabs/zx-evo-docs
- Forum thread "IDE Video DAC2 на FT812" (13 pages; audio, J1, image limits, software): https://forum.tslabs.info/viewtopic.php?f=40&t=651
- Video "VDAC2 - видеокарта для ZX Evolution": https://www.youtube.com/watch?v=OjSbMH9WKf4 ,
  https://rutube.ru/video/9e1d5210d92516fb09cb355ff219caf3/

**FT812 (Bridgetek)**
- FT81X Series Programmers Guide v1.2: https://brtchip.com/wp-content/uploads/Support/Documentation/Programming_Guides/ICs/EVE/FT81X_Series_Programmer_Guide.pdf
- FT81x datasheet: https://www.glyn.de/Daten/Datenblaetter/FTDI/FT81x/Datasheets/DS_FT81x.pdf ;
  FT812/813: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/7130/1613_762.pdf
- AN_281 FT8xx Emulator Library User Guide: https://brtchip.com/wp-content/uploads/Support/Documentation/Programming_Guides/ICs/EVE/AN_281_FT800_Emulator_Library_User_Guide.pdf
- AN_390 FT80x → FT81x Migration Guide: https://brtchip.com/wp-content/uploads/Support/Documentation/Application_Notes/ICs/EVE/AN_390-FT80x-To-FT81x-Migration-Guide.pdf
- BRT_AN_033 BT81X Programming Guide (comes with TSLib; for reference): https://brtchip.com/wp-content/uploads/2026/02/BRT_AN_033_BT81X-Series-Programming-Guide.pdf

**Emulators and libraries**
- TS-Labs Unreal (VDAC2 through `bt8xxemu.dll`): https://github.com/tslabs/zx-evo-unreal (`Unreal/ft812.cpp`, `Unreal/zc.cpp`, `Unreal/tsconf.cpp`)
- Bridgetek EVE Emulator (binaries + headers, release notes 5.1.26): https://github.com/Bridgetek/EVE_Emulator
- Gameduino 2/3 host library: https://github.com/jamesbowman/gd2-lib
- Gameduino 1 emulator (another chip): https://github.com/kaetemi/gdemu
- RudolphRiedel EVE libraries: https://github.com/RudolphRiedel/FT800-FT813 , https://github.com/RudolphRiedel/EmbeddedVideoEngine
- Bridgetek EveApps: https://github.com/Bridgetek/EveApps
- Checked, no FT8xx: ZEsarUX https://github.com/chernandezba/zesarux , MAME https://github.com/mamedev/mame

**VDAC2 software (sources, MIT)**
- R-Type VDAC2: https://github.com/andrewinsidelazarev/R-Type-Arcade-VDAC2-FT812 (release v1.01: https://github.com/andrewinsidelazarev/R-Type-Arcade-VDAC2-FT812/releases/tag/v1.01)
- Zuma Deluxe VDAC2: https://github.com/andrewinsidelazarev/Zuna-Deluxe-VDAC2-FT812 (release v1.1: https://github.com/andrewinsidelazarev/Zuna-Deluxe-VDAC2-FT812/releases/tag/v1.1)
- Heroes of Might and Magic II for VDAC2: https://github.com/andrewinsidelazarev/Heroes-of-Might-and-Magic-II-for-VDAC2
- In the game repositories: TSLib with the FT812 macros (`Docs/TSLib/Include/FT/`), the "VDAC2 #2 - Первые шаги" article (`Docs/_VDAC2_pervye_shagi.txt`), the TS-Conf + VDAC2 textbook (`Docs/uchebnik_tsconf_vdac2.md`)

**Community**
- zx-pk.ru, "Разработка игры-космосима под ZX Evo Tsconf + VDAC2": https://zx-pk.ru/threads/32689-razrabotka-igry-kosmosima-pod-zx-evo-tsconf-vdac2.html
- speccy.pl, Rev.CV (VDAC2 on the board's VGA connector; blocks automated fetches): https://www.speccy.pl/forum/index.php?topic=7464.0
