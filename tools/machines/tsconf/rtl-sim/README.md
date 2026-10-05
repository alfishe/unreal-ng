# TS-Conf video RTL simulator

A small [Verilator](https://verilator.org/) harness that runs the real Verilog of the
ZX-Evolution TS-Conf video pipeline and records, for one raster line, the palette index
of every pixel. It answers questions the Verilog alone leaves open ("what exactly
appears on screen when register X is set to Y?") and produces reference data that
unreal-ng tests can compare against.

The first question it answers is what the graphics X offset register `G_X_OFFS` does in
ZX mode and text mode: see [G_X_OFFS in ZX and TXT](#g_x_offs-in-zx-and-txt).

A second simulator, `tsconf-cpu-sim`, adds the CPU side (Z80 clock generator, memory
manager, CPU cache) and measures how long the CPU waits for DRAM: see
[CPU DRAM waits at 14 MHz](#cpu-dram-waits-at-14-mhz).

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
| `tbcpu.v` | CPU simulation top: clock + Z80 clock (`zclock.v`) + bus decoding (`zsignals.v`) + memory manager (`zmem.v`) + arbiter + `video_top` + DRAM model |
| `cpuharness.cpp` | CPU testbench: Z80 bus-cycle model, test programs, self-checks, result writer |
| `results/cpu-waits.txt` | Measured CPU waits (format in [Result format](#result-format-cpu-waitstxt)) |

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

scratch/rtl-sim-build/tsconf-cpu-sim sanity                      # CPU self-checks only
scratch/rtl-sim-build/tsconf-cpu-sim all tools/machines/tsconf/rtl-sim/results
scratch/rtl-sim-build/tsconf-cpu-sim trace 14 42 150 140 1 M1.8000,RD.C000,M1.0100
                                       # fclk-by-fclk trace: MHz, V_CONFIG, line, dot, phase, program
```

`build.sh` builds both simulators. `tsconf-cpu-sim all` takes about 8 s (19 simulations,
1554 tests, all of them twice: see the pin delay below).

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

## CPU DRAM waits at 14 MHz

At 14 MHz the TS-Conf Z80 runs from the same DRAM as the video, and the FPGA stops the Z80
clock until its data is there. `tsconf-cpu-sim` measures these waits on the real RTL and
was used to settle two questions of the RTL audit (`docs/inprogress/2026-10-05-tsconf-rtl-audit/interrupts.md`
rows 33 and 35): how long a 14 MHz data read waits (the `zmem.v` comment table and
unreal-ng disagreed) and whether the 3.5 / 7 MHz DRAM stall `stall357` ever happens.

Short answers: the reads wait **+4..+7 fclk**, not the comment's +2..+5, and unreal-ng
matches the RTL in every looped test; `stall357` **never fires**.

### Terms

| Term | Meaning |
|:--|:--|
| fclk | One 28 MHz FPGA clock (35.7 ns). A 14 MHz Z80 clock is 2 fclk, so a T-state is 2 fclk |
| DRAM cycle, phase c0..c3 | DRAM works in cycles of 4 fclk (one per 7 MHz pixel), numbered c0, c1, c2, c3 (`common/clock.v`). The arbiter decides at c3 who owns the next cycle |
| T1, T2, T3, T4 | The Z80 clock periods of a machine cycle: an opcode fetch (M1) has 4, a memory read or write 3 |
| request | `zmem.v` `dram_beg`: the fclk in which the CPU asks the arbiter for a DRAM cycle |
| wait | How many fclk a machine cycle took beyond its nominal length (2 fclk per T-state at 14 MHz) |
| refused cycle | A DRAM cycle the arbiter keeps for video although the CPU may want it (`cpu_next` = 0): video needs every cycle left in its block, because the CPU already took the spare ones |
| `f` | An fclk number in the frame: `f = 4 * (line * 448 + dot) + phase`, counted from c0 of the first DRAM cycle of line 0 |

### What is simulated

| Part | Source |
|:--|:--|
| Z80 clock generation and stalls (`zpos` / `zneg` strobes, 14 / 7 / 3.5 MHz) | RTL `z80/zclock.v` |
| Z80 bus decoding (`memrd`, `memwr`, `opfetch`, strobes latched on `zpos`) | RTL `z80/zsignals.v` |
| CPU memory manager: windows, ROM / RAM, CPU cache, 14 MHz stall logic, `stall357` | RTL `z80/zmem.v` |
| Arbiter, clock strobes, video (the DRAM load the CPU competes with) | RTL `dram/arbiter.v`, `common/clock.v`, `video/*.v` |
| DRAM chip | behavioral (`tbcpu.v`): the cycle decided at c3 reads, writes or refreshes. A read puts the word on the data bus **only during c2 and c3** (while CAS is low in `dram.v`) and a garbage pattern otherwise, so a CPU that takes its byte at the wrong moment gets garbage and the self-checks see it. A write stores the byte latched at c0 (`dram.v` `dram_wd`) |
| Z80 | a bus-cycle model in `cpuharness.cpp` (below) |

Configuration: `MEM_CONFIG` = 04h (window 0 = ROM, no DOS mapping), pages 20h, 21h, 22h in
windows 1..3, `CACHE_CONFIG` = 0 or 0Fh, `V_CONFIG` per test, TSU and DMA off (the arbiter
never lets them refuse the CPU: `dev_over_cpu` = 0), no I/O cycles. `SYS_CONFIG` = 2
stands for 14 MHz (3 is the same: only bit 1 is used).

### The Z80 bus-cycle model

The Z80 core is not in the RTL tree, so the testbench plays it. The RTL produces the Z80
clock: a `zpos` strobe means the Z80 clock rises at the end of that fclk, `zneg` that it
falls (`zclock.v`; the edge comes about 6 ns before the next fclk posedge). On each edge
the model changes the Z80 pins as the Z80 data sheet timing diagrams do:

| Machine cycle | T1 rising | T1 falling | T2 falling | T3 rising | T3 falling | T4 falling |
|:--|:--|:--|:--|:--|:--|:--|
| M1 (opcode fetch, 4 T) | address, M1 low, RFSH high | MREQ, RD low | | **byte taken**; MREQ, RD, M1 high; RFSH low, refresh address | MREQ low (refresh) | MREQ high |
| Memory read (3 T) | address, RFSH high | MREQ, RD low | | | **byte taken**; MREQ, RD high | |
| Memory write (3 T) | address, RFSH high | MREQ low, data out | WR low | | MREQ, WR high | |

The Z80 drives write data until the next machine cycle's T1 falling edge. A test program
is a list of machine cycles; a sync step idles the Z80 until a chosen `f` (at 14 MHz it
also shifts the Z80 clock by single fclk through `zclock.v`'s `ide_stall` input until a
T1 falls on the chosen phase), so one simulation runs many tests, one or several per
raster line.

**Pin delay.** A pin the Z80 changes at a clock edge is first seen by the FPGA registers
one fclk after the posedge that follows the edge (pin delay 1): the edge is about 6 ns
before a posedge and Z80 output delays are longer than 6 ns and shorter than 6 + 35.7 ns.
Everything is run a second time with pin delay 2 (a Z80 slower than 41 ns); the results
differ only in the refused-cycle race described below. With pin delay 0 (an impossible
Z80 that answers within 6 ns) the request moves one Z80 half-clock earlier, to T1's
falling edge: a self-check proves that the measurement sees this.

### Self-checks

`sanity` and `all` check:

1. With no DRAM access (ROM fetches) the Z80 clock runs at its nominal rate at 14, 7 and
   3.5 MHz, rising and falling edges alternate, nothing stalls.
2. At 14 MHz a T1 can be placed on each of c0..c3, and the request (`dram_beg`) comes
   exactly at T2's falling edge, 3 fclk after T1.
3. Every byte the Z80 takes at its sampling edge equals memory (taken off the DRAM bus or
   from the cache data register), and every written byte reaches the DRAM, in order.
4. Control: pin delay 0 moves the request to T1 + 1.
5. (`all`) Checks 1-3 hold in every one of the 1554 tests, at pin delay 1 and 2; the
   number of tests whose waits change with pin delay 2 is reported, not checked (20, all
   of them the refused-cycle race below).

### Measured rules

**1. Request.** The CPU asks for DRAM at T2's falling edge: 3 fclk after T1 when the clock
did not stop inside T1-T2 (the same for M1, read and write). Its DRAM phase decides the
wait. With nothing else using the DRAM (border lines, and window lines where video does not
refuse the CPU):

| Request phase | M1 wait | Read wait | Write wait |
|:--|:--|:--|:--|
| c3 | +3 | +4 | 0 |
| c2 | +4 | +5 | 0 |
| c1 | +5 | +6 | 0 |
| c0 | +6 | +7 | 0 |

The M1 column is the `zmem.v` comment table. **The read column is the comment's +2..+5
plus 2.** Why: the CPU gets the DRAM cycle after the request's cycle; an M1 takes its byte
at T3's rising edge, which `zmem.v` releases (`stall14_fin`, cleared at c1 for an M1) so
that this edge falls in c3 of the CPU's DRAM cycle, straight off the DRAM bus
(`cpu_latch`). A read takes its byte at T3's *falling* edge, one Z80 half-clock later;
`zmem.v` releases it at c2, so the byte is taken in c1 of the following DRAM cycle, from
the cache data register (`cache_data`, written by `cpu_strobe` at the end of c2 even with the
cache off). A read therefore costs exactly one fclk more than an M1 with the same request
phase.

Example: `LD A,(HL)` with T1 of its M1 at c2 of a DRAM cycle: the M1's request is at c1
(+5), so the M1 lasts 8 + 5 = 13 fclk; the read's T1 is then at c3, its request at c2
(+5), the read lasts 6 + 5 = 11 fclk: 24 fclk = 12 Z80 clocks.

**2. Steady state of loops** (fclk per instruction, from any start phase):

| Loop | Border | ZX, 16C, TXT window | 256C window | Where it settles |
|:--|:--|:--|:--|:--|
| NOP from RAM | 12 | 12 | 12 | M1 request at c2 (+4) |
| NOP from ROM | 8 | | | no DRAM |
| `LD A,(HL)` (M1 + read) | 24 | 24 | 24 | M1 at c1 (+5), read at c2 (+5) |
| `LD (HL),A` (M1 + write) | 20 | 20 | 23.7 .. 24 | M1 at c0 (+6), write free |
| `POP` (M1 + 2 reads) | | 36 | 36 | |
| `PUSH` (M1 of 5 T + 2 writes) | | 28 | 32 | |
| `LDI` (2 M1 + read + write of 5 T) | | 44 | 47.7 | |

**3. Cache** (`CACHE_CONFIG` = 0Fh): a hit (the 16-bit word was read before) makes no DRAM
request and no wait. A cache line is one word, so a NOP run through fresh memory misses on
the even bytes (+4 each once settled) and hits on the odd ones: 10 fclk per NOP; the same
NOPs again: 8.

**4. ROM** (window 0): no request, no wait.

**5. Video.** In the fetch window video takes its cycles as early as it can, so in ZX,
16C and TXT a CPU that asks at most once every 3 DRAM cycles is never refused (all the
loops above). In 256C (blocks of 2 cycles, 1 for video) a CPU access granted the first
cycle of a block makes video refuse it the second one. A refused cycle stops the Z80 clock
**on each of its fclk in which the Z80 is not in a memory read** (`stall14_cyc = memrd ?
stall14_cycrd : !cpu_next`, with `memrd` = MREQ and RD straight from the pins); a read
waits for its grant through `stall14_cycrd` instead. After a write the refused cycle
usually falls entirely into the write's T3 and the next T1, so it costs 4 fclk: hence
`LD (HL),A` 24 and `PUSH` 32 in 256C.

**6. 3.5 and 7 MHz.** No wait at all and `stall357` never fires (384 tests: 256C and TXT
at 320 and 256 pixels wide, NOP, `LD A,(HL)`, `LD (HL),A`, `POP`, `PUSH`, `LDI`, eight DRAM
cycle alignments each). `stall357 = cpureq_357 && !cpu_next` needs a refused cycle, which
needs the CPU to have taken every spare cycle of a block: 1 of 2 in 256C, 4 of 8 in TXT.
At 7 MHz a T-state is one DRAM cycle and the CPU asks once per machine cycle, so its
requests are at least 3 DRAM cycles apart (at 3.5 MHz 6): it gets at most one cycle of a
256C block and at most 3 of a TXT block. No mode uses all 8 cycles of a block (the
arbiter's `bw_full`), and the TSU and the DMA never refuse the CPU.

### Comparison with unreal-ng

unreal-ng (`TsConfArbiter::CpuAccess`, `TsConfMemory::DramWait` / `CacheRead`) was
compared through a throwaway Python port (it reproduces unreal-ng's own
`TsConfArbiter_Test` values), replaying every 14 MHz test from the same start fclk. The
unreal-ng tests `*TIM1*` and `*TsConfArbiter*` pass on the same tree; their expected
values (NOP 6 clocks, `LD A,(HL)` 12, `LD (HL),A` 10, 256C write 24 fclk) all equal the
RTL. unreal-ng's fetch windows (`FetchOf`: first and last block decision per mode) equal
the RTL's.

| Case | RTL fclk | unreal-ng fclk | Match |
|:--|:--|:--|:--|
| Isolated M1, request at c3 / c2 / c1 / c0 | 3 / 4 / 5 / 6 | 3 / 4 / 5 / 6 | yes |
| Isolated read, request at c3 / c2 / c1 / c0 | 4 / 5 / 6 / 7 | 4 / 5 / 6 / 7 | yes |
| Isolated write | 0 | 0 | yes |
| Cache hit; NOP fill pass; NOP second pass | 0; 10.0; 8 | 0; 10.0; 8 | yes |
| NOP from ROM | 8 | 8 | yes |
| NOP loop, border and all four window modes | 12 | 12 | yes |
| `LD A,(HL)` loop, all | 24 | 24 | yes |
| `LD (HL),A` loop, border / ZX / 16C / TXT; 256C | 20; 23.7 .. 24 | 20; 23.7 .. 24 | yes |
| `POP`, `PUSH`, `LDI` loops, four window modes | 36, 28 / 32, 44 / 47.7 | same | yes |
| Total time of all 118 loop tests | | | identical, fclk for fclk |
| Isolated M1 / read / write at 64 fclk positions in ZX, 16C, TXT windows; M1 / read in 256C | | | yes (all 64) |
| Isolated write in 256C, 8 of 64 positions | +1 (pin delay 2: +2) | +1 (before the fix: +4) | yes (pin delay 1) |
| Three writes and a read in TXT, 13 of 64 positions | see below | same | yes (before the fix: per test -4 .. +6) |
| Every machine cycle of all 1170 14 MHz tests | | | yes (before the fix: 48 tests differed) |
| 3.5 / 7 MHz, 384 tests | 0 | 0 | yes |

Both differences came from refused cycles (rule 5) and are fixed (2026-10-05, test
`TsConfArbiter_Test.ARB6_CpuWaitsMatchTheRtl`, which replays every 14 MHz test of this file through the emulated
CPU's bus cycles):

- **Part of a refused cycle overlaps a read.** When the cycle after the CPU's grant is
  refused and the Z80 starts its next memory read (MREQ and RD seen) inside it, the RTL
  stops the clock only for the fclk before that, 1 to 3 of the 4; unreal-ng charged the
  whole cycle (4 fclk). Which fclk the read starts in depends on the Z80's MREQ / RD output
  delay (pin delay 1 and 2 differ by one fclk here), so this is an RTL race, not a fixed
  number; unreal-ng follows pin delay 1. It needs a refused cycle, so it happens only after
  the CPU took a block's spare cycles: a write granted at the first cycle of a 256C block
  and followed by a read cycle at certain alignments, or back-to-back writes in TXT. The
  loops above never hit it.
- **When the frozen fclk are charged.** The RTL stretches the machine cycle the refused
  cycle falls in (for example the ROM fetch after a read); unreal-ng added refused cycles
  that pass between two DRAM accesses to the second access.

unreal-ng now keeps the refused cycles open after the grant (`TsConfArbiter`), simulates
the clock in them fclk by fclk as `stall14_cyc` and `zclock.v` do (stopped in every fclk in
which no memory read is seen on the pins, the edge already on its way still comes, stopped
to the end once two fclk in a row stop) against the machine cycles that follow, including
ROM and cache-hit reads and opcode fetches, and charges each stop to the machine cycle
whose clock edge it delays.

Audit verdicts: row 33, unreal-ng's read wait +4..+7 is right and the `zmem.v` comment
table is 2 fclk short for reads (its M1 column is right); row 35, `stall357` cannot
trigger, so applying no DRAM waits at 3.5 / 7 MHz matches the RTL.

### Result format: cpu-waits.txt

Lines starting with `#` are comments (the header records the pin delay and the memory
setup). Each test is five lines:

```
run <group> <test> mhz=<14|7|3.5> cache=<CACHE_CONFIG> vconf=<V_CONFIG> t0=<f> line=<n> dot=<n> phase=<0-3> fetch=<a>..<b> total=<fclk> stalls=<fclk> stall357=<fclk> ok=<0|1>
  prog <cycle>,<cycle>,...
  wait <w> <w> ...
  req <r> <r> ...
  sample <s> <s> ...
```

- `run`: `t0` is the `f` of the first machine cycle's T1 (the fclk at whose end the Z80
  clock rises); `line`, `dot`, `phase` are the same position. `fetch` is the first and
  last DRAM cycle (dot) of that line in which the arbiter started a video block (`-1..-1`:
  none; the last one only as far as the simulation ran). `total`: fclk from the first T1
  to the end of the last machine cycle. `stalls`: fclk with `cpu_stall` = 1. `stall357`:
  fclk with `stall357` = 1 at 3.5 / 7 MHz. `ok`: all checks of this test passed.
  `cache` and `vconf` are hex.
- `prog`: the machine cycles. `M1.aaaa` opcode fetch (4 T; `M1.aaaa.5` 5 T), `RD.aaaa`
  memory read (3 T), `WR.aaaa.dd` memory write of byte `dd` (3 T; `WR.aaaa.dd.5` 5 T),
  `IDn` n internal T-states. Addresses 0000-3FFF are ROM, 4000-FFFF RAM.
- `wait`: per machine cycle, its length in fclk minus the nominal length (T-states x 2 at
  14 MHz, x 4 at 7, x 8 at 3.5).
- `req`: per machine cycle, fclk from T1 to the request (`dram_beg` at 14 MHz, the first
  `cpu_req` at 3.5 / 7 MHz), `-` for none.
- `sample`: per machine cycle reading RAM, where the Z80 took its byte: `L` off the DRAM
  bus (`cpu_latch`), `C` from the cache data register, followed by the DRAM phase of that
  fclk; `-` otherwise.

Groups: `border14` (14 MHz, line above the window; isolated accesses `iso-*-p<phase of
T1>` and loops), `border14c` (the same with the cache on), `win14-<mode>` (loops starting
24 dots before the fetch window of line 100 onward, four start phases), `win14-<mode>-iso`
(an isolated M1 / read / write, and three writes plus a read, `-o<n>` = n fclk after a
fixed point 40 dots into the window, n = 0..63 covers two blocks of 8 DRAM cycles),
`border14-256c`, `s7-<mode>` / `s35-<mode>` (3.5 / 7 MHz, `-s<n>`: started n dots later).

### What the bus-cycle model could get wrong

- **Pin delay.** All rules hold for any Z80 whose outputs reach the FPGA between 6 and
  77 ns after its clock edge (pin delays 1 and 2) except the refused-cycle overlap, which
  moves by one fclk. A Z80 faster than about 6 ns would move every request half a Z80
  clock earlier (the control check), which no real part does.
- **All pins of one edge change together.** On a real Z80 the address follows T1's rising
  edge and MREQ / RD the falling edge; both are needed only at the request (T2 falling),
  so their exact spread does not matter while the address is valid before MREQ.
- **Write data hold.** `dram.v` latches write data at c0 of the granted DRAM cycle. A
  write that does not wait is granted that cycle after the Z80 has finished the write:
  in some isolated writes the latch comes one fclk after the next T1's rising edge. The
  model keeps the data on the bus until the next T1's falling edge; whether a real Z80
  still drives it then (its data float delay) was not checked. This is about the written
  value, not about timing.
- **DRAM access time.** The DRAM model drives read data during c2 and c3. The Z80 takes an
  M1 byte at c3 off the bus and a read byte from the cache register, so a DRAM slower
  than one fclk after CAS would not change the waits.
- Not exercised: I/O cycles and their 14 MHz fallback (`io_stall`), interrupt
  acknowledge, HALT, DOS / VDOS switching stalls, IDE stalls, clock switches between
  speeds, DMA / TSU / tilemap traffic (they never refuse the CPU), and the cache
  invalidation by writes beyond the self-checks.
