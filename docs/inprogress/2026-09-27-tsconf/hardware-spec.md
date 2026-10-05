# TS-Conf Hardware Specification (implementation view)

**Revision:** v2 — full re-verification 2026-09-27 against the Verilog, the
TSconf.xls register workbook, the ancestor Unreal, MAME, Xpeccy and the ROM
images. v1 contained ~40 factual errors (ROM group order, sprite count, DMA
block width, vdos INT loss, 720×576, SPI status, Beta ports, …); §13 lists
every correction so reviewers of v1 can see what moved.

This is the **behavioral contract** for the `TSCONF` machine: what the emulator
must do, with evidence per rule. How it is built into unreal-ng is
[technical-design.md](technical-design.md) Part III.

## 0. Sources, precedence, glossary

| Tag | Source (local clone under `emulators/github/`, upstream in [references.md](references.md)) | Role |
|:--|:--|:--|
| **[V]** | `zx-evo-tsconf/pentevo/fpga/current/` — TS-Conf Verilog (HEAD 4cd2b81) | **authoritative** for behavior |
| **XLS** | `zx-evo-docs/TSconf/TSconf.xls` (read with `xlrd`; sheets `#nnAF`, `MemConfig`, `FMaps`, `VDOS`) | register tables; wins over `tsconf_en.md` (which is incomplete: SPI/sound/RTC/VDOS sections are empty) |
| **[U]** | `zx-evo-unreal/Unreal/` — TS-Labs Unreal fork (files are **cp1251**: use `LC_ALL=C grep -a`) | porting blueprint; divergences from [V] listed in §12 |
| **[M]** | `mame/src/mame/sinclair/evo/` | cross-check (several known bugs, §12) |
| **[X]** | `xpeccy-plus/src/libxpeccy/` | cross-check, SD/vFAT oracle |

Precedence: **[V] code > XLS > [V] comments/texts > [U] > [M] > [X]**. Where a
Verilog *comment* contradicts the Verilog *code* (clock switch "at RFSH",
`zint.v` vdos comment, `arbiter.v` "32 of 3.5MHz"), the code wins.

Terms used below:

| Term | Meaning |
|:--|:--|
| **tact / T** | one CPU clock at 3.5 MHz = 2 dots = 8 fclk |
| **dot** | one 7 MHz pixel clock; a raster line is 448 dots |
| **fclk** | the FPGA's 28 MHz master clock |
| **page** | 16 KB of RAM (256 pages = 4 MB) or ROM (32 pages = 512 KB); physical address = `page << 14 \| offset` |
| **window** | one of the four 16 KB CPU address ranges W0-W3 |
| **mapped mode** | window 0 page chosen by the ROM-set formula (§2.2) instead of `PAGE0` directly |
| **DOS** | the TR-DOS ROM is active (entered by the `#3Dxx` fetch trap) |
| **vdos** | "virtual DOS": a Beta-disk port access on a drive marked virtual swaps RAM page 0xFF into window 0, so Z80 code preloaded there emulates the drive |
| **TSU** | tile/sprite unit: 2 tile layers + 3 sprite layers composited over the graphics |
| **CRAM / SFILE** | on-chip palette RAM / sprite descriptor RAM (both 256 × 16 bit) |
| **line-latched** | a register write that takes effect at the start of the next raster line |

### 0.1 Firmware builds

The tree ships **three** compiled builds, each with its own `top.rbf`; the
difference is visible to software through STATUS `VDAC_VER` (§3.3):

| Build dir | Defines | VDAC_VER | Notes |
|:--|:--|:--|:--|
| `quartus` | `IDE_HDD`, `KEMPSTON_8BIT` | 0 | standard ZX-Evo; **Nemo IDE built**, **no XTR_FEAT** |
| `quartus_vdac` | `IDE_VDAC`, `XTR_FEAT`, `KEMPSTON_8BIT` | 3 | external 5-bit VDAC |
| `quartus_vdac2` | `IDE_VDAC2`, `ESP32_SPI`, `XTR_FEAT`, `KEMPSTON_8BIT` | 6 (7 without ESP32) | VDAC2 + FT812 + ESP32 |

Never built in any of them: `COPPER`, `FDR`, `PENT_312`, `AUTO_INT`,
`ENABLE_60HZ`, `SD_CARD2`, `FREE_IORQ`, `DISABLE_TSU`, `SPI_MODE_EN`.
`XTR_FEAT` gates: DMA BLT2, `V_CONFIG` bit 3 `GFXOVR`, `T_CONFIG` bit 0 (360-wide
TS window).

**Emulator decision (D1):** implement the **superset** — `GFXOVR` and the
360-wide TS window always present (as the ancestor does) — and derive
`VDAC_VER`, the palette DAC curve and BLT2 from the ini key `[MISC] TS_VDAC`
(`NONE` → 0, PWM curve, no BLT2 - the default and the shipped setting since
2026-09-30, the standard build with the IDE board; `3BIT` / `4BIT` / `5BIT` →
1 / 2 / 3; `TS_VDAC2=1` → 7; the VDAC values have BLT2 and the §4.3 VDAC
curves). A video DAC sits on the IDE connector; the emulator leaves the IDE to
`[HDD] Scheme`. FT812 / ESP32 are out of scope; `V_CONFIG` bit 2 is stored
and ignored.

## 1. Machine overview

| Property | Value | Evidence |
|:--|:--|:--|
| CPU | Z80, 3.5 / 7 / 14 MHz (`SYS_CONFIG[1:0]`; value 3 = 14 MHz) | [V] `zclock.v:50-52,107` |
| RAM | 4096 KB = 256 pages, 22-bit physical space | [V] `zmem.v` |
| ROM | 512 KB = 32 pages (5 ROM page pins, `Page0[7:5]` ignored for ROM) | [V] `top.v:34-38`, `zmem.v:298` |
| CRAM | 256 × 16 bit (512 B), RGB555 + bit 15 | [V] `video_out.v:48-51,165-166` |
| SFILE | 256 × 16 bit (512 B) = **85 sprite descriptors** of 3 words | [V] `video_ts.v:264-268,384-385` |
| Raster | 448 dots × 320 lines, 7 MHz dot clock | [V] `video_sync.v:84,94` |
| Frame | 224 T × 320 lines = **71680 T ⇒ 48.828 Hz**, 20480 µs | derived |
| Visible | at most 360×288 dots (geometry 3); TXT hires doubles horizontally → 720×288 | [V] `video_mode.v:136-137,154-201` |
| INT | 4 sources, fixed vectors: frame 0xFF, line 0xFD, DMA 0xFB, wait-port 0xF9 | [V] `zint.v:57-61,79-86` |

**No separate VRAM**: CPU, VDU, TSU and DMA all address the same 4 MB DRAM.

## 2. Memory

### 2.1 CPU windows

| Window | Range | Page register | Readable? |
|:--|:--|:--|:--|
| W0 | 0000-3FFF | `PAGE0` (0x10) + `MEM_CONFIG` policy | no (reads 0xFF) |
| W1 | 4000-7FFF | `PAGE1` (0x11) | no |
| W2 | 8000-BFFF | `PAGE2` (0x12) | **yes** |
| W3 | C000-FFFF | `PAGE3` (0x13) | **yes** |

[V] `zports.v:428-429`; XLS marks Page0/1 W, Page2/3 RW.

### 2.2 Window 0 policy (`MEM_CONFIG`, reg 0x21)

| Bit | Name | Meaning |
|:--|:--|:--|
| 0 | `ROM128` | copy of 7FFD bit 4: **0 = BASIC-128, 1 = BASIC-48** |
| 1 | `W0_WE` | window 0 writable |
| 2 | `!W0_MAP` | **0 = mapped mode**, 1 = normal mode (page = `PAGE0` directly) |
| 3 | `W0_RAM` | window 0 shows RAM instead of ROM |
| 7-6 | `LCK128` | 7FFD decode mode (§2.3) |

Reset `MEM_CONFIG = 0x04` (normal mode, ROM, `PAGE0 = 0` → ROM page 0 = TS-BIOS).

Window 0 page ([V] `zmem.v:73,81`):

```
page = vdos     ? 0xFF (RAM, writable regardless of W0_WE)
     : !W0_MAP  ? {PAGE0[7:2], ~DOS, ROM128}      // mapped mode
     :             PAGE0                           // normal mode
source = W0_RAM || vdos ? RAM : ROM (ROM uses page[4:0])
```

The mapped formula applies to **RAM too** (a 64 KB ROM set can live in RAM).
Mapped-mode group layout — **all sources agree**, also verified in
`data/rom/zxevo.rom`:

| `{~DOS, ROM128}` | +offset | Content | zxevo.rom evidence |
|:--|:--|:--|:--|
| DOS=1, ROM128=0 | +0 | Service / TS-BIOS | page 0: "TS-BIOS Setup Utility" @0x0B05 |
| DOS=1, ROM128=1 | +1 | TR-DOS | page 1: "TR-DOS Ver 5.04T" @0x4363 |
| DOS=0, ROM128=0 | +2 | BASIC-128 editor | page 2: "1986 Sinclair Research" @0x8563 |
| DOS=0, ROM128=1 | +3 | BASIC-48 | page 3: "1982 Sinclair Research" @0xD53B |

(XLS `MemConfig`; [M] `tsconf_m.cpp:57`; [U] `config.cpp:924-929`; [X]
`tslab.c:33,38`; unreal-ng `rom.cpp:237-243` already uses 0/1/2/3 =
sys/dos/128/48.) The same 512 KB `zxevo.rom` also carries BaseConf's set in
pages 28-31 — one image serves both TSCONF (group 0) and ATM3 (last group).
The 64 KB `ts-bios*.rom` files contain group 0 only.

**DOS switching** ([V] `zmem.v:80,87-88`):

- `dos_on` = M1 fetch in window 0 at `#3D00-#3DFF` **and** `ROM128=1` **and**
  mapped mode (`MEM_CONFIG[2:0]` = `x01` pattern: bit 2 = 0, bit 0 = 1).
  At reset (`0x04`, normal mode) the trap is **off** until the BIOS enables mapping.
- `dos_off` = M1 fetch at ≥ `#4000`, suppressed while `vdos`.
- Clearing `ROM128` while DOS is active selects the Service page (+0).
- `dos_on` and `vdos` exit each insert a 4-fclk stall ([V] `zclock.v:75-86`; timing phase only).

### 2.3 7FFD compatibility and `LCK128`

Decode: **A15 = 0 and A[7:0] = 0xFD** (`port & 0x80FF == 0x00FD`), ignored while
`lock48` is set ([V] `zports.v:622-630`). A 7FFD write:

1. sets `MEM_CONFIG[0] = D4` (ROM128);
2. sets `V_PAGE = D3 ? 7 : 5` **immediately** (live register and shadow);
3. sets `PAGE3` per `LCK128` ([V] `zports.v:579-583`; [U] `io.cpp:725-758` agrees):

| `LCK128` | Mode | `PAGE3` |
|:--|:--|:--|
| 00 | 512K | `{000, D7, D6, D2, D1, D0}` |
| 01 | 128K | `{00000, D2, D1, D0}` |
| 10 | auto | 128K rule if the latched opcode bit says so, else 512K (below) |
| 11 | 1024K | `{00, D5, D7, D6, D2, D1, D0}` |

4. latches **`lock48 = D5`** unless in 1024K mode; while set, every later 7FFD
   write (incl. its ROM128/V_PAGE effects) is ignored until reset. `#xxAF`
   writes still work. A lock taken before switching to 1024K stays in force.

**Auto mode**: at *every* M1 the decoder latches `!(D7 ^ D6)` of the opcode; a
following 7FFD write uses the 128K rule when the latch is 1 — i.e. `out (n),a`
(0xD3) → 128K; `out (c),r` (ED 01rrr001) and `outi/otir/outd/otdr`
(ED 101xx011) → 512K ([V] `zports.v:555-556`; `texts/lock128.txt`; XLS's
"`!a[13]`" wording is stale).

### 2.4 FM window (`FMAPS`, reg 0x15)

Bit 4 = `MEN` (enable), bits 3-0 = `A[15:12]` of the window, so the window is
4 KB aligned ([V] `zmaps.v:55-81`):

| `A[11:8]` | Target |
|:--|:--|
| `000x` (0x000-0x1FF) | CRAM, entry `A[8:1]` |
| `001x` (0x200-0x3FF) | SFILE, entry `A[8:1]` |
| `0100` (0x400-0x4FF) | TS registers: write to `+0x400+n` ≡ `OUT (n<<8)\|0xAF` |
| 0x500-0xFFF | no FM effect (0x600+ copper only in COPPER builds, not built) |

- **Write-only**: reads always return underlying memory.
- **Writes also go through to the underlying RAM/ROM window** as a normal
  write (`ramreq` is not gated by the FM hit; [M] `tsconf_m.cpp:444` agrees).
- CRAM/SFILE pairing: an even-address byte is stashed; the odd-address write
  commits `{D, stash}` to entry `A[8:1]` (effective immediately, §4.3).
- Reset clears only `MEN` (the address nibble keeps its value).

### 2.5 CPU cache (`CACHE_CONFIG`, reg 0x2B)

A 512 B direct-mapped cache of **256 one-word entries** indexed by `A[8:1]`,
each with its own 13-bit tag `{page[7:0], A[13:9]}` + valid bit ([V] `zmem.v:210-292`).

- **Fill**: every CPU DRAM read fills its entry, whatever `CACHE_CONFIG` says.
- **Hit**: used (no DRAM request, data from cache) when `CACHE_CONFIG[window]`
  is set, **at every CPU speed**; only the timing benefit is 14 MHz-specific.
- **Invalidate**: a CPU RAM write that hits the entry. ROM is never cached.
  **DMA and video writes do not invalidate** → stale reads after DMA are
  hardware-correct (software invalidates by writing 512 bytes, `tsconf_en.md:247-256`).
- Any write to `SYS_CONFIG` copies its bit 2 into all four `CACHE_CONFIG` bits.
- 14 MHz miss penalty ([V] `zmem.v:141-207`): M1 +3..+6 fclk, data read
  **+4..+7** (c3..c0), write 0 while the arbiter grants the next cycle. The
  comment table at `zmem.v:153-172` says read +2..+5; the RTL releases a data
  read at c2 of the cycle after the grant and an M1 at c1 (`zmem.v:204`), so a
  read waits one fclk longer than an M1 (corrected 2026-09-30). When video
  holds the next DRAM cycle (`cpu_next = 0`) a read waits for the grant and a
  write or any non-read cycle freezes the clock - the arbiter model in
  `platforms/tsconf/tsconfarbiter.h`.

Emulator: functional behavior (hit returns cached word even if RAM changed
underneath) is phase 1 correctness; the wait tables are phase 8 timing.

## 3. Register space (`#xxAF`)

### 3.1 Decode

Any port with low byte 0xAF; register = `A[15:8]`; `0x00AF` is register 0.
IORQ is masked by M1 (INT-ack never hits ports); a hit blocks the ZX-bus
(`iorq1_n`) so external devices never answer ([V] `zports.v:252,314,321,492-496`).

### 3.2 Register map (built registers only)

| Reg | Name | R/W | Reset | Function / effect class |
|:--|:--|:--|:--|:--|
| 0x00 | `V_CONFIG` / `STATUS` | W / R | 0x00 | W: `[1:0]` mode, `[7:6]` geometry, `[5]` NOGFX, `[4]` NOTSU, `[3]` GFXOVR, `[2]` VDAC2 msel — **line-latched**. R: STATUS (§3.3) |
| 0x01 | `V_PAGE` | W | 0x05 | video page — line-latched (7FFD path: immediate) |
| 0x02/0x03 | `G_X_OFFS_L/H` | W | 0 | 9-bit gfx X offset — line-latched |
| 0x04/0x05 | `G_Y_OFFS_L/H` | W | 0 | 9-bit gfx Y offset — immediate, row counter reloaded at next line start |
| 0x06 | `T_CONFIG` | W | 0x00 | `[7]` S_EN, `[6]` T1_EN, `[5]` T0_EN, `[3]` T1Z, `[2]` T0Z, `[0]` 360-wide TS window; `[4],[1]` unused — immediate |
| 0x07 | `PAL_SEL` | W | 0x0F | `[3:0]` gfx palette bank, `[5:4]` T0 bank, `[7:6]` T1 bank — line-latched |
| 0x0F | `BORDER` | W | *not reset* | 8-bit CRAM index — immediate |
| 0x10-0x13 | `PAGE0-3` | W (2,3 R) | 0,5,2,0 | window pages (§2.1) |
| 0x15 | `FMAPS` | W | MEN=0 | §2.4 |
| 0x16 | `T_MAP_PAGE` | W | *not reset* | tilemap page — immediate |
| 0x17/0x18 | `T0_G_PAGE`/`T1_G_PAGE` | W | *not reset* | tile graphics pages — line-latched |
| 0x19 | `SG_PAGE` | W | *not reset* | sprite graphics page — immediate |
| 0x1A-0x1C | `DMAS_AL/AH/AX` | W | — | DMA source (§6) |
| 0x1D-0x1F | `DMAD_AL/AH/AX` | W | — | DMA destination |
| 0x20 | `SYS_CONFIG` | W | 0x00 | `[1:0]` clock (0 3.5, 1 7, 2/3 14 MHz), `[2]` cache-all → CACHE_CONFIG, `[4:3]` AYCLK latched but **not wired** (AY fixed 1.75 MHz) |
| 0x21 | `MEM_CONFIG` | W | 0x04 | §2.2 |
| 0x22 | `HS_INT` | W | 0x01 | frame-INT tact 0-223 (≥224 never matches) — immediate |
| 0x23/0x24 | `VS_INT_L/H` | W | 0 | frame-INT line 0-319, 9 bit (≥320 never matches); `VS_INT_H[7:4]` = VINT_INC, stored but inert (no AUTO_INT) |
| 0x25 | `DMAWPD` | W | — | wait-port DMA device: `[1:0]` 0 = GluClock, 1 = COM port |
| 0x26 | `DMA_LEN` | W | — | words − 1 (1..256) |
| 0x27 | `DMA_CTRL` / `DMA_STATUS` | W / R | — | W launches (§6); R `[7]` = busy |
| 0x28 | `DMA_NUM` | W | — | blocks − 1 (**8-bit**, 1..256; `DMA_NUMH` 0x2C only in FDR builds) |
| 0x29 | `FDD_VIRT` | W | 0x00 | `[3:0]` drive A-D virtual, `[7]` VG_OPEN (Beta ports reachable outside DOS) |
| 0x2A | `INT_MASK` | W | 0x01 | `[0]` frame, `[1]` line, `[2]` DMA, `[3]` wait-port; writing 0 to a bit **clears that pending INT** |
| 0x2B | `CACHE_CONFIG` | W | 0x00 | `[3:0]` per-window cache enable |
| 0x2D | `DMAWPA` | W | — | wait-port DMA address |
| 0x40-0x47 | `T0_X_L/H`, `T0_Y_L/H`, `T1_X_L/H`, `T1_Y_L/H` | W | *not reset* | 9-bit layer offsets; X line-latched, Y immediate (see §4.4 for Y prefetch delay) |

Evidence: [V] `zports.v:170-223,558-576,586-600,616-617,758`,
`video_ports.v:45-165`; XLS `#nnAF`. "*not reset*" = cleared only at FPGA
configuration; an emulator power-on sets 0, a warm reset keeps the value.

**Readable registers are only 0x00, 0x12, 0x13, 0x27**; every other `#xxAF`
read returns 0xFF ([V] `zports.v:414-442`). An IM1 handler therefore cannot
identify the INT source.

### 3.3 STATUS (read `0x00AF`)

`[7]` copper ready (0 — not built), `[6]` `PWR_UP` (set at FPGA configuration,
cleared on the trailing edge of the read), `[5]` FDR version (0), `[4:3]` 0,
`[2:0]` `VDAC_VER` (§0.1) ([V] `zports.v:236-248,371-399,420-422`).

### 3.4 Other writes that hit TS state

- `OUT (#FE)`: `BORDER = {PAL_SEL[3:0], 0, D[2:0]}` using the **latched**
  `PAL_SEL` ([V] `video_ports.v:109`) — equals `0xF0|c` only while
  `PAL_SEL[3:0] = 0xF` (reset). [U] `io.cpp:629` and [M] hardcode `0xF0|c`.
  Latched means the copy taken at the line start (`video_ports.v:160`): a
  `PAL_SEL` write shows in `#FE` writes from the next line on. (The emulator took
  the register at once until the 2026-10-05 audit.)
- DMA register writes while a transfer runs take effect live (§6.6).

## 4. Video

### 4.1 Raster

| Constant | Value | Evidence |
|:--|:--|:--|
| Line / frame | 448 dots (224 T) / 320 lines | `video_sync.v:84,94` |
| HBlank / HSync | dots 1-88 (docs: 0-87; 1-dot pipeline skew) / 11-42 | `video_sync.v:77-78,136,140` |
| VBlank / VSync | lines 0-31 / 8-10 | `video_sync.v:86-87,93` |
| Origin | dot 0 = start of HBlank, line 0 = start of VBlank | `video_sync.v` |
| Flash | toggles every 16 frames | `video_sync.v:201-209` |

### 4.2 Modes and geometry

`V_CONFIG[1:0]` = mode; `V_CONFIG[7:6]` = geometry (`rres`), **applies to every
mode including ZX** ([V] `video_mode.v:154-201`):

| rres | Window | Origin (dot, line) | Tiles fetched/line | TXT hires chars |
|:--|:--|:--|:--|:--|
| 0 | 256×192 | (140, 80) | 34 | 64×24 (512×192) |
| 1 | 320×200 | (108, 76) | 42 | 80×25 (640×200) |
| 2 | 320×240 | (108, 56) | 42 | 80×30 (640×240) |
| 3 | 360×288 | (88, 32) | 47 | 90×36 (720×288) |

Outside the window: `BORDER` color (full 8-bit CRAM index).

| Mode | Pixel format | Address (22-bit physical) | CRAM index |
|:--|:--|:--|:--|
| 0 ZX | 1 bpp + attrs, flash, bright | `V_PAGE<<14` + Spectrum layout, attrs +0x1800 | `{PAL_SEL[3:0], BRIGHT, ink/paper[2:0]}` |
| 1 16C | 4 bpp, **high nibble = left pixel** | `(V_PAGE&0xF8)<<14 \| y<<8 \| x>>1` | `{PAL_SEL[3:0], nibble}` |
| 2 256C | 8 bpp | `(V_PAGE&0xF0)<<14 \| y<<9 \| x` | byte |
| 3 TXT | 1 bpp font, 14 MHz (hires) pixels | row = 256 B at `V_PAGE`: 128 B chars then 128 B attrs; font at page `V_PAGE^1`, offset `char*8 + line` | `{PAL_SEL[3:0], dot ? attr[3:0] : attr[7:4]}` |

Evidence: `video_mode.v:204-220`, `video_render.v:43-54`.

- ZX in rres ≠ 0: columns wrap at 32 bytes; rows ≥ 192 read through the
  attribute area (hardware-true garbage — do not special-case).
- **TXT flattens everything to 4 bits**: border and TSU pixels are cut to their
  low nibble in the `PAL_SEL[3:0]` bank and doubled to 14 MHz
  ([V] `video_render.v:82`, `video_out.v:61`). No flash/bright in TXT.
- Gfx X/Y offsets are 9-bit, wrap 512 px / 512 rows; the Y offset applies in
  ZX (row wrap 256) and TXT (pixel-line vertical scroll) too ([V]
  `video_sync.v:176-181`).
- **`G_X_OFFS` is a pixel scroll only in 16C / 256C.** It loads the DRAM column
  counter (`cstart = G_X_OFFS >> 2`, fine shift `G_X_OFFS[1:0]` dots; [V]
  `video_mode.v` `x_offs_mode`, `video_sync.v` `cnt_col` / `cptr`), and in ZX and
  TXT the counter also picks what each fetch reads. Measured on the Verilog
  with `tools/machines/tsconf/rtl-sim` (test GX1, 174 lines):
  - ZX: only `G_X_OFFS[6:0]` counts; scroll `8 × G_X_OFFS[6:2] + G_X_OFFS[1:0]`
    pixels; an odd `G_X_OFFS[6:2]` swaps pixel and attribute bytes on the whole
    line; the last `G_X_OFFS[1:0]` pixels come from a 33rd fetch.
  - TXT: whole character pairs from `((G_X_OFFS >> 2) + 3) >> 2`, fine shift
    `2 × G_X_OFFS[1:0]` hires pixels; a nonzero `G_X_OFFS[3:2]` shows raw
    character codes instead of glyphs (one or both per pair, phase 1 in the
    previous pair's colors).
  - The rules: `ScreenTSConf::ZxSourceOf` / `TxtSourceOf`. (Until the
    2026-10-05 audit the emulator, like [U] apart from ignoring it, treated it
    as a pixel scroll.)
- `G_Y_OFFS` write: at the next line start the row counter reloads with the new
  value (not value + elapsed lines); the first reload per frame is on line 31
  ([V] `video_sync.v:176-199`).
- `NOGFX`: graphics window shows `BORDER`, TSU still overlays, graphics DRAM
  fetch stops (frees bandwidth, `video_sync.v:237`).
- `NOTSU`: TSU pixels not shown.
- `GFXOVR`: graphics pixel wins over TSU when "visible" — ZX: ink dot after
  flash; 16C/256C: index ≠ 0; TXT: font bit set ([V] `video_render.v:70-80`).

### 4.3 Palette (CRAM)

Entry: `[14:10]` R, `[9:5]` G, `[4:0]` B, `[15]` VDAC mode flag.

- The FPGA never interprets bit 15; it is forwarded to the external VDAC.
- **No-VDAC build** (`TS_VDAC=OFF`): per channel, the top 2 bits of the 5-bit
  value (CRAM bits 14:13, 9:8, 4:3) drive a 2-bit DAC and the low 3 bits
  (12:10, 7:5, 2:0) a temporal PWM that boosts it by one level; no boost when
  the 2-bit value is already 3, so levels 24-31 saturate ([V]
  `video_out.v:75-132`). Emulator: static time-average, i.e. [M]'s 32-entry
  `pwm_to_rgb` LUT.
- **VDAC builds** (`5BIT`): the board's CPLD converts each channel
  ([V] `pentevo/vdac/vdac1/cpld/top.v`, module `lut`, the same table as the
  VDAC2 card's): bit 15 = 1 → `{level, 3'b0}`, white = 248; bit 15 = 0 → the
  PWM-compatible linear table round(v × 255 / 24), 255 from 24 ([U]
  `tsconf.cpp:53-64` the same). 3 / 4-bit DACs have no build or board to check
  against: the emulator takes the top 3 / 4 bits scaled to full 255. (Until the
  2026-10-05 audit the 5-bit build was scaled to 255 too, white 255 and seven
  linear levels one low.)
- **Writes are immediate** (CRAM is dual-port, read per pixel): a mid-line
  write changes the rest of that line ([V] `zmaps.v:64-77`, `video_out.v:135-151`).
- **Power-on contents** = `video/mem/video_cram.mif` loaded at FPGA
  configuration (not at Z80 reset): 0x00-0x3F RGB222 cube, 0x40-0xFF twelve
  ZX palettes (normal 0x10, bright 0x18 levels). Emulator: ship the .mif as a
  512 B table, load at power-on only; warm reset keeps CRAM. ([U] reloads
  0xF0-0xFF every reset and [M] a `cram-init.bin` — divergence, §12.)

### 4.4 TSU (tiles and sprites)

**Layer order** (bottom → top, fixed): S0, T0, S1, T1, S2 — later writes
overwrite earlier ones; a TSU pixel is written only when its color nibble ≠ 0
([V] `video_ts.v:83-88`, `video_ts_render.v:99`).

**TS window**: TSU pixels are visible only inside the geometry window, or the
full 360×288 (over the border) when `T_CONFIG[0]` is set. Sprite/tile
coordinates are relative to the TS window origin and wrap mod 512
([V] `video_mode.v:196-201`, `video_sync.v:188,192`, `video_ts.v:326-328`).

**Tilemaps** (`T_MAP_PAGE`, 16 KB): 64×64 entries per layer; byte address =
`row*256 + layer*128 + col*2`, row/col mod 64. Entry: `[11:0]` tile, `[13:12]`
palette, `[14]` XF, `[15]` YF ([V] `video_ts.v:75-78,132,135`). **Tile number 0
is skipped (transparent) unless TxZ is set** (`T1Z` = bit 3, `T0Z` = bit 2),
then drawn like any tile ([V] `video_ts.v:202`). Pixel index =
`{PAL_SEL[5:4] or [7:6], entry.pal[1:0], nibble}`. First tile starts at
`-(Xoffs & 7)` ([V] `video_ts.v:170`; [U] and unreal-ng the same - the `- 8`
this line had until the 2026-10-05 audit was wrong).

**Sprites** (SFILE, 85 descriptors = words 0-254):

| Word | Bits |
|:--|:--|
| W0 | `[8:0]` Y, `[11:9]` YS (height = (n+1)×8), `[12]` —, `[13]` ACT, `[14]` LEAP, `[15]` YF |
| W1 | `[8:0]` X, `[11:9]` XS (width = (n+1)×8), `[14:12]` —, `[15]` XF |
| W2 | `[11:0]` TNUM, `[15:12]` PAL |

Pixel index = `{PAL, nibble}`. Descriptors are consumed sequentially: S0 runs
until the first `LEAP`, S1 resumes after it until the next `LEAP`, S2 runs to
the **third `LEAP`** or to descriptor 84, whichever comes first: the layer machine
then has no layer left, so the descriptors behind the third `LEAP` are never
processed (a program ends its list with a `LEAP` descriptor and may leave anything
behind it). `LEAP` counts on inactive/invisible descriptors too, and the
descriptor carrying it belongs to the layer it ends
([V] `video_ts.v:229-268,313-327`). **85 is a hard per-frame cap**; more
sprites need mid-frame SFILE rewrites.

**Graphics bitmaps** (`T0_G_PAGE`, `T1_G_PAGE`, `SG_PAGE`): 512×512 at 4 bpp =
128 KB at `page & 0xF8`; `TNUM[11:6]` = 8-line row, `TNUM[5:0]` = 8-px column
(4096 cells) ([V] `video_ts_render.v:60-72`).

**Render timing**: the TSU renders line L+1 into one of two 512-px line buffers
while line L is displayed (buffer cleared as it is read); rendering starts at
`ts_start` of the previous line (dot `hpix_beg_ts - 1`: the window start of the
latched geometry, or dot 88 with `T_CONFIG[0]`; [V] `video_sync.v:130`,
`video_mode.v:196`) with the tile pages, tile X offsets and `PAL_SEL` latched
for that previous line (`video_ports.v:153-164`), and is **reset at the next `ts_start`** —
objects not rendered in time (DRAM starvation) are dropped for that line
([V] `video_top.v:198-206,508`, `video_ts.v:98-121`).

**Tilemap prefetch**: on TS line L the TSU fetches 8 map words per enabled
layer for the tile row containing line L+16 into a 4-row ring; the prefetch
window is lines `[ts_beg-17, ts_end-9)`. Consequence: `T0_Y/T1_Y` bits [8:3]
act ~16 lines late, bits [2:0] immediately ([V] `video_ts.v:127-139,163,224`,
`video_sync.v:234`).

## 5. Interrupts

| Source | `INT_MASK` bit | Vector | Event | Clears |
|:--|:--|:--|:--|:--|
| Frame | 0 (reset 1) | 0xFF | `vcount == VS_INT && hcount == {HS_INT,0}` | self after 32 CPU clocks, or on ack |
| Line | 1 | 0xFD | `hcount == 447` on **every one of the 320 lines** (≈ tact 0 of the next line) | on ack |
| DMA | 2 | 0xFB | falling edge of DMA busy | on ack |
| Wait-port | 3 | 0xF9 | AVR wait-port DMA completion | on ack |

[V] `zint.v:57-196`, `video_sync.v:123-132`, `top.v:1092-1095`.

- The full vector byte is driven during INTACK; IM2 reads `(I<<8)|vector`.
  IM1 jumps to 0x38 regardless (the ack still clears the highest source). IM0
  executes the byte (0xFF = RST 38; 0xFD/0xFB would run as prefix/EI).
- **Priority** frame > line > DMA > wait-port; an ack clears only the source it
  served; others stay pending and re-assert after `EI`.
- Frame pulse length = 32 **CPU clocks at the current speed** (the counter
  runs on zpos), frozen while vdos or WAIT is active.
- Masking a bit clears its pending flag; unmasking never creates one.
- **vdos**: the INT *output* is gated (`int_all = … && !vdos`); the latches still
  set. **Frame, line and DMA events are deferred, not lost** — they fire after
  vdos ends ([V] `zint.v:89-91,194` code; the `zint.v:29-31` comment says
  otherwise and is wrong). [U] loses line INT during vdos, [M] loses frame+line+DMA.
  The controller's `vdos` is `pre_vdos` ([V] `top.v:1106`): the gate and the
  frozen pulse counter start at the trapped VG93 I/O cycle, one M1 before the
  RAM page #FF is mapped. A frame pulse running then has its remaining clocks
  after vdos; one whose event falls inside vdos runs its 32 clocks from the end.
- In the VDAC2 build with `V_CONFIG[2]` set the line source becomes the FT812
  INT — not modeled (D1).

## 6. DMA

One engine, one transaction at a time; the "device code" selects the task.

### 6.1 Addresses

`addr = (AX << 14) | ((AH & 0x3F) << 8) | (AL & 0xFE)` — a 21-bit *word*
address into DRAM only (DMA cannot reach ROM) ([V] `dma.v:363-366`).

### 6.2 `DMA_CTRL` (0x27 write)

| Bit | Meaning |
|:--|:--|
| 7 | `RW` — direction for device transfers; task selector within the RAM family |
| 6 | `OPT` — BLT2 saturation |
| 5 | `S_ALGN` |
| 4 | `D_ALGN` |
| 3 | `ASZ` — alignment block 256 B (0) / 512 B (1); also BLT byte (1) vs nibble (0) granularity |
| 2-0 | device |

Code = `{ctrl[7], ctrl[2:0]}` ([V] `dma.v:223-227`):

| Code | Task | Accesses per word | Built? |
|:--|:--|:--|:--|
| 0x1 | RAM → RAM copy | 2 | yes |
| 0x9 | BLT1: copy, keep dst where **source** byte (ASZ=1) / nibble (ASZ=0) is 0 | 3 | yes |
| 0x2 / 0xA | SPI → RAM / RAM → SPI (little-endian, 2 SPI bytes per word) | 1 DRAM; the word takes 34 fclk (two 17-fclk exchanges, `spi.v`; the DRAM cycle overlaps the second byte's shift, the DMA's `spi_stb` being the SPI start) whatever the video load | yes |
| 0x3 / 0xB | IDE → RAM / RAM → IDE (16-bit words, data register; §8.3) | 1 DRAM + 1 IDE bus cycle (6 fclk; the emulator takes 12 fclk per word: the bus cycle on the 4-fclk grid, then the DRAM cycle) | `IDE_HDD` build only (emulator: when an IDE board is fitted) |
| 0x4 | FILL: read the first word once per transaction, then write it | 1 after first read | yes |
| 0x6 | BLT2: dst += src per byte (ASZ=1) / nibble (ASZ=0); wraps, saturates if `OPT` | 3 | `XTR_FEAT` |
| 0x7 | wait-port (AVR) transfer via `DMAWPD/DMAWPA` | AVR-paced | yes (out of v1 scope) |
| 0xC | RAM → CRAM, entry = dst byte-address `[8:1]` | ~2 | yes |
| 0xD | RAM → SFILE, entry = dst `[8:1]` | ~2 | yes |
| 0x0, 0x5, 0x8, 0xE, 0xF (+0x3/0xB without an IDE board) | undefined / not built | — | **hangs**: busy stays 1, no INT, until the next `DMA_CTRL` write or reset |

`tsconf_en.md`'s BLT1 "if destination ≠ 0" is wrong; [V], [U], [M] test the source.

### 6.3 Length, blocks, alignment

- `DMA_LEN` = words − 1 (1..256), reloaded per block; `DMA_NUM` = blocks − 1
  (1..256), copied at launch.
- Without `x_ALGN` the address runs linearly across blocks.
- With `S_ALGN`/`D_ALGN`: inside a block the low 7 (ASZ=0) / 8 (ASZ=1) word
  bits wrap; at block end the base advances by 256 / 512 bytes and the low
  offset reloads to the value the CPU originally wrote ([V] `dma.v:344-349,377-382`).
- The address registers *are* the live counters: final addresses remain readable
  to the engine (not to the CPU — they read 0xFF).

### 6.4 Status and completion

`DMA_STATUS[7]` = busy. Completion (busy 1→0) latches the DMA INT
([V] `dma.v:406-410`, `zports.v:425-426`).

### 6.5 Timing

DRAM: one access per 7 MHz dot = 448 accesses per line. Arbitration ([V]
`arbiter.v:168-191`): video fetches in 8-cycle blocks, served just in time —
**urgent video > CPU > pending video > tilemap > sprites > DMA > refresh**.
The CPU goes first until the remaining block cycles equal the remaining video
accesses; at full video bandwidth (8 of 8) the CPU stalls for the block. DMA
gets what is left. (The "Z80 low priority" column in the `arbiter.v:40-50`
comment is dead code, `dev_over_cpu = 0`.)

### 6.6 Register writes during a transfer

[V]: address writes modify the live counters (a same-clock increment wins);
`DMA_LEN` is used at the next block reload; `DMA_NUM` only at launch; a
`DMA_CTRL` write **relaunches** immediately with counters reloaded, busy never
falls, so the aborted transfer raises no INT ([V] `dma.v:220-231,286,309-371`).
[U] drops such writes — divergence, follow [V].

## 7. Sound

| Device | Decode | Notes |
|:--|:--|:--|
| AY-3-8912/YM | `xxFD` with A15 = 1, BC1 = A14 | physical chip (external port), **fixed 1.75 MHz** (`ay_mod` hardwired 0, `top.v:531-533`); single AY, no TurboSound select |
| Beeper + Covox | `OUT (#FE)` bit 4 / any `xxFB` | **one shared 8-bit PWM register, last write wins**: FE forces 0x00/0xFF from bit 4; `xxFB` writes the byte, never gated ([V] `sound.v:25-34`, `zports.v:254,490`) |
| Soundrive | — | absent |
| General Sound | `#B3`/`#BB` (classic), `#33` (NeoGS only) | ZX-bus card, not decoded by the FPGA (answers via IORQGE, `zbus.v:25-32`) |

## 8. Storage

### 8.1 SD card (FPGA SPI master)

| Port | Direction | Semantics |
|:--|:--|:--|
| `0x57` | W | start an SPI exchange sending D |
| `0x57` | R | return the byte received by the **previous** exchange and start a new exchange sending 0xFF |
| `0x77` | W | `[1]` SD CS_n (0 = selected); `[2]` FT812 CS, `[3]` SD2 CS, `[4]` ESP CS (1 = selected; out of scope); `[0]`,`[7]` ESP32 build only |
| `0x77` | R | **0x00** constant ("card present, writable") ([V] `zports.v:460-465`; [U] `zc.cpp:81-82`) |

Decoded in every mode (not DOS-gated). A byte takes 16 fclk (SCK 14 MHz) —
effectively instant for the CPU; DMA shares the master ([V] `zports.v:296-302,689-717`,
`spi.v`, `top.v:1168-1189`). Xpeccy's 0x77 "inserted/WP" bits are not hardware.

Card protocol: SDHC, CMD0/8/9/10/12/13/16/17/18/24/25/55/58/59 + ACMD41, 512 B
sectors, data token 0xFE, CRC16 (tolerated off unless CMD59 enables it).
unreal-ng master has **no** SD implementation (`portdecoder_atm3.cpp:45-55`
returns 0xFF on 0x57); the shared card model `SdCardSpi` exists on the NeoGS
branch and is reused (technical-design §3.11).

### 8.2 Beta-128 (VG93) and virtual drives

- Ports **0x1F, 0x3F, 0x5F, 0x7F** (VG93) and **0xFF** (system register),
  low-byte decode (high byte ignored), active only when `DOS || FDD_VIRT[7]`
  ([V] `zports.v:276-280,327-334,345`). There is no 0x9F.
- **vdos on**: an IN or OUT to 1F/3F/5F/7F/FF while `DOS && !vdos` and the
  currently latched drive (system register bits 1-0) has its `FDD_VIRT` bit set;
  takes effect at the **next M1** (`pre_vdos`) ([V] `zports.v:640-651`, `zmem.v:100-116`).
- **vdos off**: an IN/OUT to 1F/3F/5F/7F (not FF) while vdos; immediate.
- While vdos: writes to FF only update the drive-select bits; window 0 = RAM
  page 0xFF writable; INT output gated (§5); CMOS writable, its reads float (§9).
- The "virtual drive" is therefore **Z80 code in RAM page 0xFF** (placed by the
  BIOS/software); the emulator only implements the swap — nothing is served
  host-side.

### 8.3 Nemo IDE

Built in the standard `quartus` build (`IDE_HDD`). The `quartus_vdac` and
`quartus_vdac2` builds use the same FPGA pins for the video DAC, so a real
board has **either** IDE **or** a VDAC ([V] `top.v:71-90, 430-506`). The
emulator has no pin conflict: under D1 (superset) IDE works whatever `TS_VDAC`
says, and `[HDD] Scheme` alone decides whether an IDE board is fitted. TS-BIOS
lists IDE Nemo/SMUC boot devices. **v1 decision (D2, 2026-09-29): emulated**,
through the shared IDE core (`IdeAdapter` scheme `NEMO-DIVIDE`, on master since
`f5fc5f05`).

**Port decode** ([V] `zports.v:256-274, 336-341, 408-412`), low address byte
only; the ATA register is `A7..A5`:

| Port | Condition | Meaning |
|:--|:--|:--|
| `rrr10000` (#10 #30 … #F0) | `loa[2:0] = 000`, `loa[4:3] = 10` | CS0, register `rrr` (#10 = data) |
| `rrr01000` (#08 #28 … #E8) | `loa[2:0] = 000`, `loa[4:3] = 01`, except #C8 | CS0 aliases of the same registers; #08 is a data access whose high byte is lost |
| #C8 | | CS1 register 6 (alternate status / device control) |
| #11 | | the high-byte latch (not a bus cycle) |

The ports answer **always**: in and out of DOS, inside vdos, in every clock
mode (`porthit` has no DOS term for them). They do not overlap the Beta-128
ports (1F/3F/5F/7F/FF have `loa[2:0] ≠ 000`).

**16-bit data** ([V] `zports.v:783-861`). The data register is 16 bits, the Z80
bus 8. Two orders work, and the hardware tells them apart with triggers:

| Order | Read | Write |
|:--|:--|:--|
| Nemo | `IN #10` = low byte (the bus cycle), `IN #11` = high byte from the latch | `OUT #11,hi` (latched), `OUT #10,lo` (the bus cycle writes `hi:lo`) |
| DivIDE | first `IN #10` = low byte (the bus cycle), second `IN #10` = high byte from the latch | first `OUT #10,lo` (latched), second `OUT #10,hi` (the bus cycle writes `hi:lo`) |

Any access to another IDE port or to #11 clears the pending read/write pair.
Worked example: `OUT (#11),#AB : OUT (#10),#CD` writes the word #ABCD; a disk
image stores `CD AB`. This is bit for bit the ZX-Evo BaseConf logic
(`zx-evo/pentevo/fpga/base/z80/zports.v:520-615`); unreal-ng's
`IdeAdapter::EvoIn/EvoOut` implement it.

**The read latch is shared with DMA.** `iderdreg` loads on *every* completed
IDE bus cycle (`ide_stb`), the CPU's and the DMA's ([V] `zports.v:849-854`).
After an IDE → RAM DMA, `IN #11` (or a second `IN #10`) returns the high byte
of the last word the DMA read.

**Bus cycle and CPU stall — TSConf only** ([V] `common/ide.v`,
`zports.v:766-781`, `z80/zclock.v:96-121`). Unlike BaseConf, whose IDE strobes
are the Z80's own `IORQ`/`RD`/`WR`, TSConf runs every IDE access through a
fixed PIO-4 state machine clocked at 28 MHz: `go` + 5 states = **6 fclk
(≈ 214 ns)** per word. The drive's IORDY is not used; the cycle never waits
for the device. While it runs, `ide_stall` freezes the Z80 clock. Only real
bus cycles stall (`ide_req`: CS0/CS1 ports, not #11, not a #10 that is served
from the latch). In Z80 T-states of the current clock:

| CPU clock | Z80 edge spacing | Stall |
|:--|:--|:--|
| 3.5 MHz | zpos every 8 fclk | 1 T |
| 7 MHz | zpos every 4 fclk | 2 T (1 T in some phases) |
| 14 MHz | clock toggles every fclk except while stalled | 3 T |

The 7 MHz value depends on where the access falls against the clock phase;
the emulator takes the upper value. The stall is emulated, **off by default**
(`[HDD] IdeStall=0`): software does not depend on it, and default timing then
matches the ancestor emulator. `IdeStall=1` adds it, for timing studies.

**IDE DMA** (codes 0x3 IDE → RAM, 0xB RAM → IDE; [V] `dma.v:98, 136, 250-251,
441-445`, `ide.v`). The DMA moves **16-bit words** to and from the data
register (CS0, register 0: `ide_a = 0`), one IDE bus cycle per word. It skips
the Z80 latches, so the byte order is the image's: the low byte goes to the
even address. `DMA_LEN`/`DMA_NUM` count words like every other DMA device
(512-byte sector = `LEN 0xFF`, `NUM 0`). When the CPU and the DMA ask at the
same time, the DMA drives the bus (`ide.v`: `dma_req` selects every signal).
A CPU IDE access during an IDE DMA is not modeled (the CPU would read the
DMA's word); software does not do it. Without an IDE board (`Scheme=NONE`,
the equivalent of a VDAC build) codes 0x3/0xB are "not built" and hang (§6).
With a board but no drive, the words read are what the shared channel returns
for an empty unit (#FFFF), and the transfer completes.

**Reference check:**

| Point | [V] | [U] `tsconf.cpp:378-420` | [X] `tslab.c:407-422` | Decision |
|:--|:--|:--|:--|:--|
| Z80 port decode, both orders | as above | `IDE_NEMO_DIVIDE` scheme, config-selected (`io.cpp:128`) | Nemo | [V] = `NEMO-DIVIDE` |
| DMA unit | 16-bit word | 16-bit word (`hdd.read_data()`) | 8-bit, low byte only | [V] + [U]: words |
| CPU stall | 6 fclk per bus cycle | none | none | emulated, off by default |
| DMA updates the read latch | yes | no | no | [V] |

## 9. Other peripherals

| Device | Decode | Notes |
|:--|:--|:--|
| Gluk CMOS | low byte `F7`, A8 = 1; `#DFF7` address (A13 = 0), `#BFF7` data (A14 = 0), `#EFF7` (A12 = 0) | `#EFF7` writable only outside DOS, only bit 7 used (CMOS enable). CMOS reachable when `(EFF7[7] \|\| DOS) && (!DOS \|\| vdos)` — i.e. **not** from the TR-DOS ROM; inside vdos the AVR takes writes and sees reads, but a read gives `#FF`: `porthit` takes `#xxF7` only while `!dos` (`zports.v:330`) and vdos is always in DOS ([V] `zports.v:719-732`; [U] answers in any DOS state — divergence; unreal-ng answered inside vdos until the 2026-10-05 audit) |
| Gluk extension | CMOS regs **0xF0-0xFF**; mode selected by writing F0 | 0 config version, 1 bootloader version, 2 PS/2 keyboard scancode log, 3 config/modes; reg 0x0C = 0 disables EEPROM mode first (`zx-evo-docs/GluExt`) |
| Kempston mouse | `xxDF`: A8 = 0 → `{wheel[3:0], 1, btn[2:0]}`; A8 = 1 → A10 ? Y : X | `#FADF/#FBDF/#FFDF` ([V] `zkbdmus.v:107`) |
| Kempston joystick | `0x1F`, 8-bit | only when `!DOS && !FDD_VIRT[7]` ([V] `zports.v:334,450-455`) |
| Keyboard | `#FE` matrix (from AVR) + PS/2 log via Gluk ext 2 | dual feed |
| COM port / ZiFi | low byte `0xEF`, any high byte | relayed to the AVR as a wait-port; [M] map: 00EF-BFEF ZiFi data, C0EF-FFEF ZiFi cmd/status, F8EF-FFEF 16550 — **v1: reads 0xFF** |
| Floating bus | unclaimed ports | **0xFF** ([V] `top.v:435`, `zbus.v:32`) |
| NMI | — | not generated (`top.v:422,1111-1124`) |

## 10. Reset, boot, snapshots

- **Reset** (warm, [V] `zports.v:558-576,626-628,670-673,725-727`,
  `video_ports.v:95-105`, `zmem.v:91-112`): pages {0,5,2,0}, `MEM_CONFIG=0x04`,
  `SYS_CONFIG=0`, `INT_MASK=1`, `FMAPS.MEN=0`, `FDD_VIRT=0`, `CACHE_CONFIG=0`,
  `V_CONFIG=0`, `V_PAGE=5`, `PAL_SEL=0x0F`, `T_CONFIG=0` (**TSU off**),
  `G_X/G_Y=0`, `HS_INT=1`, `VS_INT=0`, `lock48=0`, `EFF7=0`, DOS=0, vdos=0,
  SPI CS all deselected, DMA idle. Not reset: `BORDER`, `T_MAP_PAGE`,
  `T0/T1_G_PAGE`, `SG_PAGE`, T0/T1 offsets, CRAM, SFILE, cache contents.
- **Power-on** additionally: all "not reset" registers 0, CRAM from the .mif
  (§4.3), `PWR_UP=1`.
- **Boot**: normal mode + `PAGE0=0` → ROM page 0 = TS-BIOS. The DOS trap is
  inactive until the BIOS switches to mapped mode.
- **ROM image**: `data/rom/zxevo.rom` (512 KB, group 0 = TS-BIOS build
  28.04.2018 / TR-DOS 5.04T / 128 / 48); ini `TSL=rom\zxevo.rom`. The 64 KB
  `ts-bios*.rom` variants are valid too if padded (the current `rom.cpp:350-356`
  demands exactly 32 banks — technical-design §3.11 relaxes this to "≥ 4 banks,
  padded with 0xFF").
- **SPG snapshots** (`zx-evo-docs/Formats` SPG v1.0/v1.1; [U] `snapshot.cpp:190-328`):
  header with SP, PC, IFF1, CPU clock/INT byte (0x35), page 3 (0x34); blocks
  `{page, offset, size, compression}` with compression **0 = none, 1 = MegaLZ,
  2 = Hrust**. Load defaults: IY=5C3A, HL'=2758, I=3F, IM 1, 7FFD=0x10.
  [U] accepts versions 0x00/0x01/0x02/0x10 (rejects v1.1 = 0x11 — fix in the
  port); depackers exist in [U] `depack.cpp` (`dehrust` :62, `demlz` :278).
  SPG carries no CRAM/SFILE/TSU/DMA state.

## 11. Timing summary

| Constant | Value |
|:--|:--|
| Dot clock | 7 MHz; TXT pixels 14 MHz |
| T/line, lines, T/frame | 224, 320, 71680 (48.828 Hz, 20480 µs) |
| Frame INT default | line 0, tact 1 |
| Line INT | the strobe on dot 447 (`line_start_s`, the line's last fclk); latched from raster tact 224 n, the next line's first tact (all 320; the last line's at tact 0 of the next frame) |
| DRAM | 448 accesses/line; urgent video > CPU > video > TM > TS > DMA > refresh |
| Clock switch | **immediate** after the `OUT` I/O cycle (`top.v:228`; the `zclock.v:22` "at RFSH" comment is not implemented) |
| 14 MHz external I/O (AY, VG93) | fixed stall of 8 fclk (one 3.5 MHz tact) per access ([V] `zclock.v:75-90`) — not a switch to 7 MHz |
| CPU stall at 3.5/7 MHz | only when the arbiter withholds a slot (full video bandwidth) |

## 12. Reconciliation record

| Topic | [U] | [M] | [X] | [V] → decision |
|:--|:--|:--|:--|:--|
| ROM group | 0 sys/1 dos/2 128/3 48 | same | same | same — **no divergence** (v1 of this doc was wrong) |
| 7FFD decode | A15=0, lo=FD | `port & 0x8002 == 0` | — | A15=0, lo=FD, `lock48` → [V] |
| 7FFD 128K mode | PAGE3 hi bits 0 | keeps PAGE3[7:3] (bug) | — | hi bits 0 → [V]/[U] |
| Border via `#FE` | `0xF0\|c` | `0xF0\|c` | — | `{PAL_SEL[3:0],0,c}` → [V] |
| CRAM power-on | reload F0-FF every reset | `cram-init.bin` every reset | — | .mif at power-on only → [V] |
| CRAM write timing | immediate | immediate | next line | immediate → [V] |
| FM reads | fall through | map CRAM/SFILE | map | fall through; writes also hit RAM → [V] |
| Cache | 256 entries | single line | none | 256 entries, DMA doesn't invalidate → [V] |
| Sprites | `snum < 85` | ≤85 | ≤85 | 85 (SFILE = 256 words) → all agree |
| Tile 0 | skipped unless TxZ | — | — | skipped unless TxZ → [V] |
| T_CONFIG[1:0] | `t0ys_en/t1ys_en` | interleave | interleave | bit 0 = 360 TS window, bit 1 unused → [V] |
| Line INT position | `line_t` += 224 | every line | HBlank start | strobe on dot 447, latched from raster tact 224 n (the next line's first tact; [U] the same), all 320 lines → [V] |
| INT during vdos | frame deferred, line lost, DMA deferred | all lost | — | all deferred → [V] |
| Reset HS_INT | 2 | 0 | 0 | 1 → [V] |
| DMA writes while busy | dropped | — | — | live / relaunch → [V] |
| DMA undefined code | NOP, not busy | — | — | hangs busy → [V] |
| DMA_NUM width | 8 | 10 | — | 8 (no FDR) → [V] |
| DMA per-word cost | 1 unit SPI/CRAM/SFILE | 28 MHz ticks | instant | §6.2 table → [V] (phase 8) |
| Clock switch | immediate | immediate | immediate | immediate → [V] |
| zclk = 3 | 14 MHz | 28 MHz (bug) | 14 | 14 → [V] |
| AY clock | fixed | fixed | fixed | fixed 1.75 (no `ayclk` decode) → [V] |
| CMOS in DOS | allowed | — | — | blocked in DOS; inside vdos writes reach the AVR, reads float (`#FF`) → [V] |
| 0x77 read | 0x00 | — | inserted/WP bits | 0x00 → [V] |
| Floating bus | — | — | attribute byte | 0xFF → [V] |
| BLT2 | additive+sat | absent | nibble blend | byte/nibble add, OPT saturates, XTR_FEAT → [V] (D1 superset) |
| SPG versions | 0x00-0x02, 0x10 | v1.0 | — | accept v1.0 and v1.1 |
| CPU DRAM cycles in the budget | reads and writes (`memcpucyc`, `z80_main.inl:143-150`) | — | — | reads and writes to writable RAM ([V] `zmem.v:121`) |
| Border via `#FE`, which PAL_SEL | bank F (fixed) | bank F | — | the PAL_SEL latched at the line start (`video_ports.v:109,160`) → [V] |
| 5-bit VDAC levels | `{level, 3'b0}` / rounded table | — | — | the board CPLD's table ([V] `pentevo/vdac/vdac1/cpld/top.v`), as [U] |
| Frame INT pulse during vdos | frozen | — | — | frozen from `pre_vdos` ([V] `zint.v:194`, `top.v:1106`) |
| INT vector moment | re-checked 3 tacts later | — | — | at the INTA IORQ; `int_sel` kept when nothing is left ([V] `zint.v:117-132`) |

## 13. Corrections from v1 (2026-09-27)

ROM group order and `ROM128` polarity reversed · 7FFD decode, `lock48`,
auto-lock rule · readable registers (only 0x00/0x12/0x13/0x27) · cache =
256 entries, not one line · FM writes also hit RAM · reg 0x25 = DMAWPD, reg
0x2D added · wait-port INT (4th source) · clock switch immediate, zclk 3 =
14 MHz, 14 MHz I/O stall not a 7 MHz fallback · STATUS `VDAC_VER` per build ·
three firmware builds (IDE *is* in the standard build; XTR_FEAT is not) ·
reset list (TSU off; border/TSU pages not reset) · sprites = 85 (SFILE 512 B) ·
tile 0 transparency rule · 720×288, not 720×576 · CRAM 256 entries, bit 15 is
a VDAC flag, writes immediate, power-on .mif · TXT 1 bpp font, hires in every
geometry · geometry applies to ZX mode · `#FE` border formula · Y-offset
reload and tilemap prefetch delay · INT during vdos deferred, not lost · line
INT at dot 447 on all lines · DMA code table incl. hangs, 8-bit `DMA_NUM`,
alignment advance, live register writes, CRAM/SFILE addressing · SPI 0x77
semantics, no ZC SD in unreal-ng · Beta ports (no 0x9F, no mirror claim) and
vdos entry/exit rules · CMOS decode/gating, Gluk ext at 0xF0 · AY fixed clock
(no `ayclk`) · Covox/beeper shared register · SPG compression codes · duplicate
sentences removed.

2026-09-29: §8.3 Nemo IDE written in full (decode, both byte orders, the
latch shared with DMA, the TSConf-only 6-fclk CPU stall, IDE DMA in 16-bit
words, VDAC builds have no IDE); D2 decided: emulated.
