# VDAC2: the FT812 line budget - how it is calculated and what overflow does

**Created:** 2026-10-02. **Status:** model in eve-emu, metrics in the chip state
([line-budget-metrics.md](line-budget-metrics.md)); calibration on a card pending (§7).

This document explains, for someone writing or debugging VDAC2 software, how much time the
FT812 has to draw one screen line, what a display list costs per line, how the emulator
calculates both, how the per-line numbers add up to a frame, and what happens when a line
asks for more than it has. The hardware facts and their sources are in
[ft812-behavior-spec.md](ft812-behavior-spec.md) §5.2; this file adds the reasoning, the
worked examples and what is still assumed.

## 1. Terms

| Term | Meaning |
|:--|:--|
| System clock | the FT812's internal clock. VDAC2 runs it at 64 MHz in the 1024 x 768 mode (8 MHz crystal x 8) |
| Pixel clock | system clock / `REG_PCLK`. VDAC2 uses `PCLK` = 1: one pixel per system clock |
| Line period | the time one screen line takes on the monitor, visible part plus blanking: `REG_HCYCLE` pixel clocks |
| Display list (DL) | the program of up to 2048 commands the FT812 runs for **every** screen line to draw it |
| Line cost | the system clocks one line needs: its display list commands plus its pixel filling |
| Hard budget | the clocks a line has: `HCYCLE x PCLK` (1344 in the 1024 x 768 mode) |
| Soft budget | the warning line below the hard budget: hard budget minus a margin, 10 % by default (1210 clocks) |
| Overflow | a line whose cost is above the hard budget; on the card it comes out broken (§5) |

## 2. The budget of one line

The FT812 does not keep a frame buffer. It draws each screen line just before the monitor
needs it, from the display list, and it has to finish within one line period. So the time
per line is fixed by the video mode:

```
line period   = HCYCLE pixel clocks = HCYCLE x PCLK system clocks
hard budget   = HCYCLE x PCLK        (minus an overhead, TO VERIFY, §7)
```

The VDAC2 modes the software uses:

| Mode | System clock | PCLK | HCYCLE x VCYCLE | Frame rate | Line period | Hard budget | Soft budget (10 %) |
|:--|:--|:--|:--|:--|:--|:--|:--|
| 1024 x 768 (mode 7: R-Type, Zuma, `ftview`) | 64 MHz | 1 | 1344 x 806 | 59.08 Hz | 21.0 us | **1344** | 1210 |
| 640 x 480 (HMM2) | per the program | 1 | per the program | 74 Hz | | `HCYCLE` | 90 % of it |

The Programmers Guide adds that the time "is never less than 2048 internal clock cycles"
[PG §2.5.7]. Read literally that would give every VDAC2 line 2048 clocks, more than the
line lasts, which cannot be: the chip cannot spend longer on a line than the line takes.
The emulator applies the 2048 floor only when `HCYCLE x PCLK` is itself at least 2048 (slow
pixel clocks on small panels), so on VDAC2 the budget is the line period (TO VERIFY, §7).

The practical number: developers of VDAC2 software keep about **10 % below** the theoretical
1344. R-Type, the most demanding program measured, puts its usable ceiling "around 1300"
and its worst line at 1247 by its own cost tool, with no broken lines on the card since
then. The 10 % margin is the soft budget's default (eve-emu `EveSetLineBudgetMargin`, 0..50;
the `[VDAC2] LineBudgetMargin` setting comes with phase L2).

## 3. What a line costs

Two parts, added:

```
line cost = commands + fill clocks
```

### 3.1 Commands: one clock each, on every line

The graphics engine fetches **one display list command per system clock**, and it runs the
whole list for every screen line, from the first command to `DISPLAY`. A command costs its
clock on every line whether or not what it draws touches that line:

- a list of 600 commands costs 600 clocks on each of the 768 lines, even on a line where
  only the background is visible;
- commands inside a subroutine (`CALL`) count each time they are called;
- state commands (`COLOR_RGB`, `BITMAP_HANDLE`, `SAVE_CONTEXT`, ...) cost the same as
  drawing commands: a clock each.

The command count is the cost that scales worst: it is paid 768 times per frame.

### 3.2 Fill: pixels per clock, by bitmap format and filter

For every primitive that covers part of the line, the engine fills its pixels on that line
(after the scissor and the screen edge cut the span). The rate depends on the bitmap format
and the filter [PG §2.5.7 table]:

| Filter | Bitmap formats | Pixels per clock | Clocks for 1024 pixels |
|:--|:--|:--|:--|
| NEAREST | all except the four below | 16 | 64 |
| NEAREST | TEXT8X8, TEXTVGA, PALETTED4444, PALETTED565 | 8 | 128 |
| BILINEAR | all except the four below | 4 | 256 |
| BILINEAR | TEXT8X8, TEXTVGA, PALETTED4444, PALETTED565 | 2 | 512 |
| (not a bitmap) | points, lines, rectangles, edge strips | 16 (TO VERIFY) | 64 |

The PG text says "1/4 pixel per clock" for bilinear but its table says 4; the table is used
(TO VERIFY). Overdraw counts in full: pixels drawn twice cost twice, transparent pixels
cost the same as opaque ones.

The emulator adds the fill in 1/16 clock steps per span and rounds the line's total up, so a
13-pixel NEAREST span costs 13/16 clock, not a whole one.

### 3.3 Worked examples (1024 x 768, hard 1344, soft 1210)

| Line content | Commands | Fill | Line cost | Verdict |
|:--|:--|:--|:--|:--|
| A full-width RGB565 background, NEAREST, plus 20 state commands | 23 | 1024 / 16 = 64 | 87 | 6 % of the budget |
| The same background, BILINEAR (scaled) | 23 | 1024 / 4 = 256 | 279 | fine |
| The same, PALETTED4444, BILINEAR | 23 | 1024 / 2 = 512 | 535 | 40 %: one such layer is affordable, three are not |
| Background (NEAREST, 64) + 40 sprites 64 px wide, PALETTED4444, NEAREST, all on this line; 300 commands in the list | 300 | 64 + 40 x 64 / 8 = 384 | 684 | fine |
| The same 40 sprites with BILINEAR | 300 | 64 + 40 x 64 / 2 = 1344 | 1644 | **overflow** by 300 clocks |
| A list of 1250 commands (many small objects), only the background on this line | 1250 | 64 | 1314 | **over soft**: the command count alone fills the line |

The last two rows show the two ways to break a line: too many filled pixels where objects
pile up on the same lines, and too many commands anywhere in the list.

## 4. From lines to a frame

- **Each line stands alone.** Time not used on one line is not saved for the next, and an
  overflow on one line does not delay the next one. What matters is the worst line, not the
  average.
- The frame's lines have the same display list (unless the program swaps it mid-frame with
  `DLSWAP_LINE`, which no measured program does), so the command part is the same on every
  line and the fill part follows where the objects are. Overloaded lines therefore come in
  bands: the rows where sprites, text or scaled bitmaps overlap.
- The emulator folds every finished frame into a **metrics block** in the chip state
  ([line-budget-metrics.md](line-budget-metrics.md) §3.1), replaced at the frame's end:

| Field | Meaning |
|:--|:--|
| `frame` | the chip's frame number (`REG_FRAMES`) the block describes |
| `valid` | every visible line of that frame was drawn and measured |
| `lines` | visible lines (`VSIZE`) |
| `hardBudget`, `softBudget` | the budgets the frame was measured against |
| `worstLine`, `worstClocks` | the most expensive line and its cost |
| `totalClocks` | the sum over all lines: compares how heavy frames are, not a limit |
| `linesOverSoft` | lines between the soft and the hard budget: risky on a real card |
| `linesOverHard` | lines over the hard budget: broken on a real card |
| `lineClocks[]` | the cost of every line (the histogram in the FT812 Debug window) |

- A frame is healthy when `linesOverHard` is 0; it has margin when `linesOverSoft` is 0
  too. `worstClocks / hardBudget` is the headroom in one number (R-Type boot: 621 / 1344 =
  46 %, no overflow, eve-emu's model; the game's own tool reports 1247 for its heaviest
  scene).
- Coprocessor work (`CMD_*` commands, image decoding, memory copies) does not come out of
  the line budget: the coprocessor runs beside the graphics engine. It decides how fast a
  program can build new display lists, not whether a line breaks.

## 5. What overflow looks like

On the card, a line whose cost is over the budget is not finished in time. From a reference
video of an overloaded scene recorded on a VDAC2 card (kept locally, not in the repository):

- each overloaded line is drawn from the left up to a **cut point** and the rest of the line
  is missing: what appears there instead (black, or the pixels the line buffer held before)
  is TO VERIFY;
- the cut point **moves from line to line** with each line's cost, so an overloaded band
  shows **wedge-shaped streaks**: the more a line is over, the further left it breaks;
- the picture outside the overloaded band is intact; the band flickers as objects move and
  lines go in and out of overflow from frame to frame.

The emulator today draws every line completely, counts the overflow and reports it (the
metrics block, the debugger, automation): an overflow is never hidden, but not yet shown as
the card shows it. Drawing the cut (where the engine stops, with the leftover filled as the
card does) is phase L4 of [line-budget-metrics.md](line-budget-metrics.md), after the
measurements of §7.

Where the cut falls is a consequence of the model: commands run in list order, so the
commands and spans at the end of the display list are the ones a too-expensive line loses.
Putting the background and the important objects first in the list makes a marginal
overflow cost less visible content.

## 6. Keeping lines inside the budget

- **Fewer commands.** Every command is paid on all 768 lines. Merge state changes, draw many
  objects in one `BEGIN` .. `END` block, avoid `CALL` of a shared routine in a loop.
- **Cheaper fill.** NEAREST instead of BILINEAR where the bitmap is not scaled (4x cheaper);
  a non-paletted format where memory allows (2x); no full-screen transparent overlays.
- **Less overlap per line.** Spread objects vertically; use the scissor so a primitive's span
  does not run past where it is visible (the scissor cuts the fill, not the command).
- **Measure.** The FT812 Debug window (Debug menu, `TSL-VDAC2` only; phase L3) and every
  automation surface (phase L2) give the worst line and the per-line costs of the frame on screen, also when going
  back in time.

## 7. What is assumed and how to check it

| Item | Assumed now | How it gets settled |
|:--|:--|:--|
| Overhead below `HCYCLE` per line | 0 (R-Type's ceiling of ~1300 suggests ~44) | a test pattern with a growing command count on the card: the count where lines start breaking |
| The 2048-clock floor | only when `HCYCLE x PCLK` >= 2048 | the same pattern at a slower pixel clock |
| BILINEAR rates | 4 and 2 pixels per clock (PG table, not the PG text) | a pattern of growing bilinear spans |
| Points, lines, rectangles, edge strips | 16 pixels per clock | a pattern of growing rectangles |
| What the rest of a broken line shows | unknown (black or stale) | photograph the pattern on the card |
| Soft budget margin | 10 % (developer practice) | stays a setting; the calibration narrows it |

Until these are measured the absolute numbers are a model: right for comparing frames and
finding the heavy lines, possibly off by a few percent at the exact point of a real
overflow. The open items are listed in [vdac2-tdd.md](vdac2-tdd.md) §12.1 and the spec's V7;
the line timing experiment is
[`tools/poc/021-eve-accel/08-hardware-timing-model`](../../../tools/poc/021-eve-accel/08-hardware-timing-model/),
the board measurement plan for the coprocessor
[`10-coprocessor-timing`](../../../tools/poc/021-eve-accel/10-coprocessor-timing/).
