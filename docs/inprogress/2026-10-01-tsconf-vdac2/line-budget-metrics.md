# VDAC2: FT812 line budget metrics and TTD

**Created:** 2026-10-02. **Status:** design, implementation started (branch `vdac2-line-metrics`).

## 1. Why

Every FT812 screen line has a fixed time to be drawn: the line period, `HCYCLE x PCLK`
system clocks (1344 in the 1024 x 768 @ 59 Hz mode VDAC2 uses). A line whose display list
needs more than that comes out broken on the card: it is drawn up to some point and the
rest is black or stale, with the cut point moving from line to line (wedge-shaped streaks
across the overloaded band). This is the hardest part of writing software for the FT812.
On real hardware developers keep about 10 % below the theoretical 1344 clocks per line.

eve-emu already computes the cost of every drawn line (`EveLineCost`: display list
commands, fill clocks, total, budget, over or not). What is missing:

- a per-frame result that stays with the frame;
- a practical warning threshold below the hard budget;
- access from every automation surface and a debugger view;
- the result when going back in time (TTD scrubbing) without drawing the frame again.

## 2. Terms

| Term | Meaning |
|:--|:--|
| Line cost | FT812 clocks one screen line needs: display list commands plus pixel filling |
| Hard budget | the clocks a line has: `HCYCLE x PCLK` (spec §5.2) |
| Soft budget | the warning threshold: the hard budget minus a margin (default 10 %) |
| Frame metrics | the metrics block of one frame: summary plus the cost of every line |

## 3. Design

### 3.1 A metrics block in the chip state, replaced at frame end

The chip's control state (`ControlState`, saved whole by `EveSaveState`) gets one more
member, the metrics of the last completed frame:

```cpp
struct FrameMetrics
{
    uint64_t frame;            // REG_FRAMES value of the frame the block describes
    uint32_t valid;            // 1: every visible line of the frame was drawn and measured
    uint32_t lines;            // visible lines (VSIZE)
    uint32_t hardBudget;       // clocks per line
    uint32_t softBudget;       // the warning threshold used for this frame
    uint32_t worstLine;        // the most expensive line
    uint32_t worstClocks;      // and its cost
    uint64_t totalClocks;      // sum over the lines
    uint32_t linesOverSoft;    // softBudget < cost <= hardBudget
    uint32_t linesOverHard;    // cost > hardBudget
    uint16_t lineClocks[kMaxLines]; // cost per visible line, saturating at 65535
};
```

- **When it changes:** in `FrameEvent` (the chip's frame end, where `REG_FRAMES`
  increments). The current frame's line costs (`lineCosts`, already kept per line) are
  folded into the block and it replaces the previous one. Nothing else writes it.
- **Cost:** the line costs are computed by the drawing anyway; the fold is a pass over the
  visible lines once per frame (768 additions and compares), and the copy of 2 x VSIZE
  bytes. No measurable effect on the frame time.
- **Size:** `kMaxLines` is 4096 (VSIZE is 12 bits): the block is about 8 KB of the state.
  The state version goes from 7 to 8. Old `.evr` captures that start with a state record
  no longer load (captures from power-on are unaffected); re-take them.
- **Not drawn = not valid:** the library draws a frame only when the host asks for the
  picture. A frame that was not drawn leaves `valid = 0` and zero costs. The host makes
  the chip draw every frame while metrics are wanted (§3.3), as it already does while
  capturing the bus.

### 3.2 Soft budget

`EveSetLineBudgetMargin(chip, percent)`, default 10, range 0..50. It is host
configuration, not chip state (it is not saved); every frame block records the soft budget
it was measured with, so a restored block reads the same as when it was made.

### 3.3 In unreal-ng

- **Vdac2Card:** reads the block (`EveGetFrameMetrics`), owns `[VDAC2] LineBudgetMargin=`
  (percent, default 10) and the "measure every frame" flag: on while the FT812 Debug window
  is open or automation asked for metrics in the last seconds, and while capturing.
- **Vdac2Control** (the facade every surface calls): `GetFrameMetrics(withLines)`,
  `SetLineBudgetMargin(percent)`, `SetMetricsAlways(on)`.
- **Automation (all five surfaces, same data):**
  - WebAPI: `GET /api/v1/emulator/{id}/vdac2/metrics[?lines=1]`,
    `PUT /api/v1/emulator/{id}/vdac2/metrics` `{"margin":10,"always":true}`; OpenAPI.
  - MCP: `inspect_state` aspect `vdac2_metrics`.
  - CLI: `vdac2 metrics [lines]`, `vdac2 metrics margin <percent>`.
  - Lua `vdac2_metrics(lines)`, Python `emu.vdac2_metrics(lines=False)`.
  - Recipe: `.recipe/machines/tsconf-vdac2.md`.
- **Qt: Debug -> FT812 Debug** (a tool window). The menu item exists only while the active
  machine is TS-Conf with the VDAC2 card (`TSL-VDAC2`); for every other machine it is hidden,
  not grayed out, and an open window closes when the active machine changes to one without
  the card:
  - the line cost histogram of the frame, one bar per line, with the soft and hard budget
    marked (green below soft, orange above soft, red above hard);
  - the summary: frame, worst line and its cost, lines over soft / hard, budget, margin;
  - **always the same moment as the main screen**, in every state: running, paused, at a
    breakpoint, single-stepping, and TTD scrubbing. It refreshes on the same signal the main
    screen does (frame presented, `refreshViewport` after a step, a pause or a seek), never on
    its own timer, so the two never show different moments.
  - What "the same moment" means follows the screen's rule (§3.4): at a frame boundary (a
    running machine's presented frame, a seek by frame number) the window shows that frame's
    finished block; at an exact moment inside a frame (a breakpoint, a step, a seek by T-state
    or time point) it shows the FT812 frame in flight: the lines measured up to that moment
    (`EveGetLineCost`, marked "in progress") and the rest of the lines from the previous
    finished block, with the boundary line marked, as the screen shows the picture drawn so
    far over the previous frame.
- **TTD:** the block is part of the chip state, so a checkpoint carries it and a seek
  brings back the metrics of that frame without drawing it again (§3.4).

### 3.4 TTD for the FT812 (found missing, built in this branch)

Finding (2026-10-02): the integration design (§9) planned a `Vdac2` device blob plus TTD v2
memory regions, and until those exist a refusal to record (§9.3). Neither is in the code:
a TTD recording on `TSL-VDAC2` runs and silently leaves the FT812 out. After a seek the
chip's memory, display lists, coprocessor and registers are those of the present, so its
answers and its picture are wrong.

What this branch builds, until TTD v2 memory regions exist (the Sprinter precedent:
large memories as whole blobs, `ttdsprinter.h`):

- **Blob `Vdac2`** (the next free id): the card's time (`Vdac2Card::Time`), its INT edge
  queue and select state, and `EveSaveState` (the chip's control state, with the metrics
  block of §3.1).
- **Blob `Vdac2Memory`:** the chip's state-bearing regions (arch §4.5: `RAM_G` 1 MB, both
  display lists, `REG`, `CMD`, `SPECIAL`, `INFLIGHT`), whole. The ROM is not state. Zstd
  compresses the mostly unchanged `RAM_G`; when TTD v2 regions land the blob becomes
  regions with dirty pages (eve-emu already tracks dirty 4 KB pages).
- **Restore:** regions written back, `EveMemoryRestored`, then `EveLoadState`, then the
  card's own fields; the FT812 framebuffer is not state and is drawn again.
- **The picture after a seek follows the ZX screen rule** (TTD `ComposeDisplay`):
  - seek to a **frame number**: the picture as it was when the frame's rendering finished;
  - seek to an exact **T-state / time point**: the frame drawn from its start up to that
    moment, over the previous frame's final picture for the part not drawn yet.
  The FT812 runs on its own clock (frame ~16.9 ms) inside the machine's frame (~20 ms):
  "the FT812 frame" is the one in flight at the target moment, its lines due by then drawn,
  the rest from its previous frame. During the replay the chip draws every line (as while
  capturing), so nothing depends on whether the host looked at the picture.
- **Tests (core-tests, a VDAC2 SDK program from `testdata/machines/tsconf/vdac2-sdk`):**
  record, seek to frame N and to a T-state inside a frame, compare with a straight run
  stopped at the same point: chip state blob, every region, registers, metrics block and
  the FT812 picture byte for byte; restore round trip of both blobs; a recording after a
  seek continues identically.

## 4. Phases

| Phase | Content | Checks |
|:--|:--|:--|
| L1 | eve-emu: `FrameMetrics` in the state (version 8), fold at frame end, margin, public API `EveGetFrameMetrics` / `EveSetLineBudgetMargin`, tests; vendored into unreal-ng | library tests (block after a frame, valid flag, worst line, soft / hard counts, save / load round trip); R-Type replay bit-exact |
| L2 | Vdac2Card / Vdac2Control, `[VDAC2] LineBudgetMargin`, the five automation surfaces, OpenAPI, recipe | core tests; live check on R-Type through the WebAPI |
| L3 | Qt FT812 Debug window in the Debug menu | build, manual check |
| T1 | FT812 TTD: `Vdac2` and `Vdac2Memory` blobs, restore, the picture after a seek (frame vs T-state rule), ksy / analyzer ids, tests above | core tests: seek vs straight run byte for byte |
| L4 | calibration of the cost model on a VDAC2 board (line overhead, fill rates: spec V7 TO VERIFY); optional drawing of the overflow cut as on the card | after board measurements |

## 5. Open points

- The absolute clocks are a model (spec §5.2, V7: line overhead 0 vs ~44 in R-Type,
  fill rates TO VERIFY). Until calibrated, the metrics are right for comparing frames and
  finding heavy lines; the exact threshold of a real overflow may differ.
- What exactly an overflowing line shows on the card (spec V7) is open; a reference video
  of the effect exists locally.
