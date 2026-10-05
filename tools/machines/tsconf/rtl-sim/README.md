# TS-Conf video RTL simulator

A small [Verilator](https://verilator.org/) harness that runs the real Verilog of the
ZX-Evolution TS-Conf video pipeline and records, for one raster line, the palette index
of every pixel. It answers questions the Verilog alone leaves open ("what exactly
appears on screen when register X is set to Y?") and produces reference data that
unreal-ng tests can compare against.

The first question it answers is what the graphics X offset register `G_X_OFFS` does in
ZX mode and text mode: see [G_X_OFFS in ZX and TXT](#g_x_offs-in-zx-and-txt).

## What is simulated

| Part | Source |
|:--|:--|
| Clock strobes (`f1`, `h1`, `c0`..`c3` from the 28 MHz clock) | RTL `common/clock.v` |
| DRAM arbiter (video access blocks, CPU priority) | RTL `dram/arbiter.v` |
| Whole video block: ports, mode decoder, raster sync, fetcher, renderer, output, TSU | RTL `video/video_*.v` |
| DRAM chip | behavioral model (`tbtop.v`): the word address the arbiter presents at the start of a DRAM cycle is read from a 4 MB memory image and held on the data bus for that cycle |
| Altera `altdpram` blocks (CRAM, TS line buffers, VGA buffer, sprite file, tile buffer) | behavioral stand-in (`altdpramstub.v`), starts zeroed |
| CPU | one input, `cpu_req`: either idle or asking for every DRAM cycle (worst-case contention) |

Not simulated: Z80, DMA, copper, VGA mode (`vga_on` = 0, TV raster), 60 Hz. The palette
(CRAM) is not loaded: the harness records the 8-bit index that enters the CRAM
(`video_out.vdata`), not RGB. The build uses the standard `quartus/tune.v` defines
(no `XTR_FEAT`, no `PENT_312`, TSU enabled).

Registers are written through the same write strobes and data bus that `top.v` uses
(`vconf_wr`, `vpage_wr`, `gx_offsl_wr`, ... with `xt_wr_data`) right after reset, so the
port module latches them at the next line start as on the board.

### Files

| File | What it is |
|:--|:--|
| `tbtop.v` | Simulation top: clock + arbiter + `video_top` + DRAM read model |
| `altdpramstub.v` | Behavioral `altdpram` |
| `harness.cpp` | Testbench: memory images, register writes, line capture, self-checks, result writer |
| `build.sh` | Builds the simulator with Verilator |
| `check-rules.py` | Rebuilds every captured line from the rules below and reports differences |
| `results/zx-gxoffs.txt`, `results/txt-gxoffs.txt` | Captured lines (format below) |

## Build and run

You need Verilator 5 and a C++ compiler, plus a checkout of the ZX-Evo sources
(`pentevo/fpga/current` from the zx-evo repository). Pass that folder as the first
argument or in `TSCONF_RTL_DIR`. The build goes to `scratch/rtl-sim-build/` (git-ignored)
and uses half of the CPU cores.

```bash
tools/machines/tsconf/rtl-sim/build.sh <path-to>/pentevo/fpga/current
# or: TSCONF_RTL_DIR=<path-to>/pentevo/fpga/current tools/machines/tsconf/rtl-sim/build.sh

scratch/rtl-sim-build/tsconf-video-sim sanity                    # self-checks only
scratch/rtl-sim-build/tsconf-video-sim all tools/machines/tsconf/rtl-sim/results
scratch/rtl-sim-build/tsconf-video-sim line zx 4                 # print one line: zx|txt|16c|256c, G_X_OFFS
scratch/rtl-sim-build/tsconf-video-sim line txt 12 83            # optional V_CONFIG in hex
python3 tools/machines/tsconf/rtl-sim/check-rules.py             # rules vs captures
```

Timing on the development Mac: a clean build takes a few seconds, `sanity` about 3.5 s, `all` about 14 s
(174 captured lines, each simulated twice: without and with CPU contention). One line
costs about 0.03 s: every run starts from reset and simulates about 90 raster lines.

### Self-checks

`sanity` and `all` first prove the harness against pictures whose answer is known:

1. ZX mode, `G_X_OFFS` = 0: the window is the plain ZX screen (first pixel = bit 7 of
   column 0) and the border dots on both sides are the border color.
2. Text mode (`V_CONFIG` = 03h and 83h), `G_X_OFFS` = 0: the plain text screen.
3. 16-color and 256-color modes (`V_CONFIG` = 01h, 02h, 81h, 82h), `G_X_OFFS` from 0 to 40
   and then every 37 up to 484: the window is the picture scrolled left by exactly
   `G_X_OFFS` pixels, wrapping at 512.
4. (`all` only) every ZX and text capture is identical when the CPU asks for every DRAM
   cycle, so DRAM contention does not change the result.

The delay between the raster counter and the index at the CRAM input is found once by
check 1 (it is 1 dot) and then kept fixed, so checks 2 to 4 test it independently.

### Test pattern

The memory images (see `harness.cpp`) let every byte be identified on screen:

- ZX (`V_PAGE` = 05h): pixel byte of column `c`, pixel line `y` = `81h | c << 1 | (y & 1) << 6`
  (bit 7 and bit 0 always ink, bit 6 paper on even lines). Attribute of column `c`, text
  row `r`: ink = `c & 7`, paper = `(ink + 1 + (c >> 3)) & 7`, bright = `r & 1`, no flash.
  Ink and paper always differ, and the pair (ink, paper) identifies the column.
- Text (`V_PAGE` = 10h, font in page 11h): character code at column `c` = `80h | c`,
  attribute = ink `c & 15`, paper `(ink + 1 + ((c >> 4) & 7)) & 15`. Glyph line `l` of
  character `ch` = `(ch * 1Dh + 35h + l * 40h) & FFh`, which differs from `ch` itself, so a
  raw character code drawn as pixels can be told from its glyph.
- `PAL_SEL` = 0, `BORDER` = EEh, `G_Y_OFFS` = 0, TSU off (`TS_CONFIG` = 0).

## Result file format

`results/zx-gxoffs.txt` and `results/txt-gxoffs.txt`: lines starting with `#` are
comments. Every other line is one captured raster line:

```
<V_CONFIG hex> <G_X_OFFS decimal> <pixels per dot> <index> <index> ...
```

- The captured line is window line 9 (the 10th): ZX pixel line 9 (text row 1 of the
  attributes), text row 1 glyph line 1.
- Indices are two hex digits, one per pixel, left to right: 8 border dots, the window,
  8 border dots.
- `pixels per dot` is 1 for ZX (one pixel per 7 MHz dot: 8 + 256 + 8 = 272 indices) and 2
  for text mode (two hi-res pixels per dot: 16 + 512 + 16 = 544 indices at 256 wide,
  16 + 640 + 16 = 672 at 320 wide).
- The border is EEh in ZX mode. In text mode the 4-bit hi-res plex carries only the low
  nibble and `video_out.v` puts `PAL_SEL[3:0]` in front, so the border shows as 0Eh.
- `G_X_OFFS` values: 0..40, 64..68, 128..131, 255, 256, 257, 300, 508..511.
  `txt-gxoffs.txt` has the set twice, for `V_CONFIG` 03h (256x192) and 83h (320x240).

## G_X_OFFS in ZX and TXT

`video_mode.v` splits `G_X_OFFS` for every mode except 256-color into two parts:

- **fine shift** `f = G_X_OFFS & 3`: the picture starts `f` dots (7 MHz pixels) later in
  its data stream. This part works as expected.
- **column counter start** `n = G_X_OFFS >> 2`: the DRAM column counter starts at `n`
  instead of 0. Each counter step is one 16-bit DRAM fetch. In 16-color mode one fetch
  is 4 pixels, so `4n + f` = `G_X_OFFS` and the scroll is linear. In ZX and text mode a
  fetch is not "the next few pixels": the low counter bits select which *kind* of data
  is fetched (pixels or attributes; codes, attributes or glyphs), and the fetched word is
  routed by a separate toggle that always starts at "first kind". When `n` does not
  start on a fetch-group boundary, the kinds land in the wrong slots for the **whole
  line**, not just the first cell.

`check-rules.py` rebuilds all 174 captured lines from the rules below with no
differences.

### ZX mode (V_CONFIG = 00h)

Only `G_X_OFFS[6:0]` matters (the address uses counter bits 4..0), so 128, 256 and 384
look like 0, and 300 looks like 44.

Let `c = (G_X_OFFS >> 2) & 31` and `f = G_X_OFFS & 3`. The line is drawn as 16-pixel
groups. Group `m` takes its pixel bits from fetch `c + 2m` and its colors from fetch
`c + 2m + 1`. Fetch number `k` reads, for byte columns `2 * ((k >> 1) & 15)` and the next
one: the pixel bytes of the current pixel line if `k` is even, the attribute bytes if
`k` is odd. The window shows the group stream starting at pixel `f`.

- **`c` even** (`G_X_OFFS` 0..3, 8..11, 16..19, ...): a correct picture scrolled left by
  `8c + f` pixels, wrapping at 256. So the scroll in pixels is
  `8 * (G_X_OFFS >> 2) + (G_X_OFFS & 3)`, not `G_X_OFFS`: `G_X_OFFS` = 8 scrolls by 16
  pixels, 9 by 17, 16 by 32.
- **`c` odd** (`G_X_OFFS` 4..7, 12..15, ...): pixel and attribute data are swapped on the
  whole line. Window cell `j` (before the fine shift) draws the **attribute byte** of
  column `c - 1 + j` as its pixel pattern and colors it with the **pixel byte** of column
  `c + 1 + j` (two columns to the right) used as an attribute (ink, paper, bright, and
  flash from its bit 7). Example `G_X_OFFS` = 4: cell 0 = pattern of attribute byte 0,
  colors from pixel byte 2; cell 1 = attribute byte 1 with pixel byte 3; ...
- **Right edge, `f` > 0**: the last `f` dots come from a 17th group that got only its
  pixel-slot fetch (the 33rd fetch of the line; the fetch window ends before the 34th).
  Their pixel bits come from fetch `c + 32`, which reads the same columns as fetch `c`
  (the column bits wrap), and their colors are still the previous group's color word. Example `G_X_OFFS` = 3: the last 3
  dots show bits 7..5 of pixel byte 0 in the colors of attribute byte 30.

The fine shift does not depend on `c`: `G_X_OFFS` = 1 is the plain picture moved left by
1 pixel (with the right-edge effect above).

### Text mode (V_CONFIG = 03h 256x192, 83h 320x240)

Text mode draws two hi-res pixels per 7 MHz dot. A character is 8 hi-res pixels; a
character pair (16 hi-res pixels) needs four fetches: codes of the pair, attributes of the
pair, glyph line of the first code, glyph line of the second code. The counter's low two
bits select the kind (0 codes, 1 attributes, 2 first glyph, 3 second glyph), bits 7..2
select the pair (64 pairs = 128 characters per text row).

Let `n = G_X_OFFS >> 2` (0..127), `p = n & 3`, `f = G_X_OFFS & 3`. Pair `q` = characters
`2q` and `2q + 1` of the text row. The first pair shown is `q0 = (n + 3) >> 2`, i.e.
`n >> 2` when `p` = 0 and `(n >> 2) + 1` otherwise; pair `m` of the window is `q0 + m`
(wrapping at 64). The window shows this pair stream starting `2f` hi-res pixels in.

| `p` (`G_X_OFFS[3:2]`) | First character of each pair | Second character of each pair | Colors |
|:--|:--|:--|:--|
| 0 | glyph of its code | glyph of its code | its own attributes |
| 1 | **raw code byte** as pixels | **raw code byte** as pixels | attributes of the **previous** pair (`q - 1`) |
| 2 | **raw code byte** as pixels | **raw code byte** as pixels | its own attributes |
| 3 | glyph of its code | **raw code byte** as pixels | its own attributes |

Why: the four fetches of a group start at kind `p`, and the code fetch of the next pair
overwrites the glyph slots before the group is shown (`p` = 1, 2) or the second glyph slot
(`p` = 3). The glyph fetches themselves use whatever codes are in the fetch register at
that moment, but those results are always overwritten, so leftovers from the previous
line never reach the screen.

So the coarse scroll that keeps a readable picture is 2 characters (16 hi-res pixels) per
16 of `G_X_OFFS`, the fine scroll is 2 hi-res pixels per unit of `G_X_OFFS[1:0]`, and any
`G_X_OFFS[3:2]` other than 0 corrupts the whole line. In hi-res pixels a valid scroll is
`16 * (G_X_OFFS >> 4) + 2 * (G_X_OFFS & 3)`. The right edge is always clean: the
fetch window covers a whole extra group.

Examples (256x192, window line 9):

| G_X_OFFS | Window starts with |
|:--|:--|
| 0 | char 0, 1, 2, ... normally |
| 1, 2, 3 | char 0 moved left by 2, 4, 6 hi-res pixels |
| 4 | code byte of char 2 colored by attr 0, code of char 3 by attr 1, code of 4 by attr 2, ... |
| 8 | code byte of char 2 with attr 2, code of char 3 with attr 3, ... |
| 12 | glyph of char 2, code byte of char 3, glyph of char 4, code byte of char 5, ... (own attributes) |
| 16 | char 2, 3, 4, ... normally |
| 256 | char 32, 33, ... normally |
| 300 | as 12 but starting at pair 19 (`n` = 75): glyph of char 38, code byte of char 39, ... |

## Limits

- One frame from reset, so the ZX FLASH phase is always "not inverted"; with FLASH set
  in a used attribute (in ZX mode with `c` odd, pixel bytes act as attributes) the real
  picture inverts every 16 frames.
- The DRAM model returns data for the whole DRAM cycle; on the board the data bus is
  valid from CAS to the end of the cycle, and the RTL latches it at the end. Timing inside
  a DRAM cycle is not modeled beyond that.
- Only TV raster timing, 50 Hz, 320-line frame. VGA (scan doubler) and 60 Hz are not
  exercised.
- CPU contention is modeled only as "idle" or "every cycle"; DMA, TSU and tilemap DRAM
  traffic are absent (the arbiter gives video priority over them anyway).
