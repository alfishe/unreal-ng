# 07 — INT Signal Positions and Timings

**Date:** 2026-08-09
**HDL reference:** `ula.sv` lines 169-173
**Emulator reference:** `z80.cpp` lines 374-403, `config.cpp` line 268-269, `data/configs/*/unreal.ini`

---

## 1. The Pentagon's unique INT position

The Pentagon generates its INT at a position that is fundamentally different
from all Sinclair models. This is the single most important timing
characteristic to get right.

### 1.1 MiSTer HDL INT generation

```verilog
// ula.sv lines 169-173

// ZX-48K and ZX-128K:
if( mZX && (vc_next == 248) && (hc_next == (m128 ? 8 : 4))) INT <= 1;

// Pentagon:
if(!mZX && (vc_next == 239) && (hc_next == 326)) INT <= 1;

// INT duration (counter-based):
if(INT) INTCnt <= ((m128 && INTCnt == 71) || (~m128 && INTCnt == 63)) ? 7'd0 : (INTCnt + 1'd1);
if(INTCnt == 0) INT <= 0;
```

### 1.2 What makes the Pentagon INT unique

| Property | Pentagon | ZX-48K | ZX-128K |
|---|---|---|---|
| INT trigger vc | **239** | 248 | 248 |
| INT trigger hc | **326** | 4 | 8 |
| Lines after paper end | **47** | 56 | 56 |
| Position within line | **73% through** (hc=326/448) | **1%** (hc=4/448) | **2%** (hc=8/456) |
| INT duration (HC) | 64 | 64 | 72 |
| INT duration (T-states) | **32** | **32** | **36** |

The Pentagon INT fires:
1. **Mid-line** — at hc=326, which is 326/448 = 73% through the horizontal cycle
2. **Earlier in VBlank** — only 47 lines after paper end vs 56 lines for Sinclair models
3. **At an odd horizontal position** — not at the start of a line like Sinclair

This mid-line INT is the defining characteristic of Pentagon timing. Demos and
software written for the Pentagon rely on this exact position to calculate where
the raster beam is when the ISR starts executing.

---

## 2. MiSTer VC counter layout (physical frame structure)

The MiSTer's VC counter starts at the first **screen paper line** (vc=0 = top of
pixel area). The frame wraps at the end of the top border:

### Pentagon (320 lines)
```
vc 0–191:    Screen paper     (192 lines)
vc 192–235:  Bottom border    (44 lines)
vc 236–271:  VBlank/VSync     (36 lines, VSync pulse at 248–256)
vc 272–319:  Top border       (48 lines)
─── wraps to vc 0 ───
```

### ZX-48K (312 lines)
```
vc 0–191:    Screen paper     (192 lines)
vc 192–235:  Bottom border    (44 lines)
vc 236–263:  VBlank/VSync     (28 lines, VSync pulse at 240–244)
vc 264–311:  Top border       (48 lines)
─── wraps to vc 0 ───
```

### ZX-128K (311 lines)
```
vc 0–191:    Screen paper     (192 lines)
vc 192–235:  Bottom border    (44 lines)
vc 236–263:  VBlank/VSync     (28 lines)
vc 264–310:  Top border       (47 lines)
─── wraps to vc 0 ───
```

---

## 3. Emulator frame layout — different origin!

The emulator places VSync at **t-state 0** (top of frame), not at the paper start.
This creates a coordinate offset between the emulator and MiSTer:

### Emulator Pentagon frame (71680 t-states)
```
t-state 0–3583:      VSync       (16 lines)
t-state 3584–7167:   VBlank      (16 lines)
t-state 7168–17919:  Top border  (48 lines)
t-state 17920–60927: Paper       (192 lines)
t-state 60928–71679: Bottom border (48 lines)
```

The emulator's VSync+VBlank = 32 lines at the top, but MiSTer puts these at
the bottom (vc 236–271 = 36 lines for Pentagon).

### Coordinate mapping: emulator ↔ MiSTer

```
MiSTer vc 0 (paper start) = emulator line 80 = emulator t-state 17,920
```

General formula:
```
emulator_line = MiSTer_vc + (vSyncLines + vBlankLines + screenOffsetTop)
emulator_tstate = emulator_line * tstatesPerLine + hc / 2
```

---

## 4. The critical bug: `intstart=13` is wrong for all models

### 4.1 Current configuration

All three model INI files use the same INT position:
```ini
intstart=13   ; t-states before int
intlen=32     ; int length in t-states
```

This means INT fires at **t-state 13** — which is 13 t-states into the first
VSync line. That's at the very **beginning** of the emulator frame.

### 4.2 What the correct positions should be

Using the coordinate mapping above:

**Pentagon** (vc=239, hc=326):
```
emulator_line = 239 + 80 = 319
emulator_tstate = 319 * 224 + 163 = 71,619
```
INT should fire at **t-state 71,619** out of 71,680 — near the END of the frame.

**ZX-48K** (vc=248, hc=4):
```
emulator_line = 248 + 72 = 320
Frame = 312 lines, so wraps: 320 mod 312 = 8
emulator_tstate = 8 * 224 + 2 = 1,794
```
INT should fire at **t-state 1,794** — in the VBlank area after VSync.

**ZX-128K** (vc=248, hc=8):
```
emulator_line = 248 + 72 = 320
Frame = 311 lines, so wraps: 320 mod 311 = 9
emulator_tstate = 9 * 228 + 4 = 2,056
```
INT should fire at **t-state 2,056**.

### 4.3 Summary of INT position errors

| Model | Current `intstart` | Correct `intstart` | Error |
|---|---|---|---|
| Pentagon | 13 | **71,619** | Off by 71,606 t-states |
| ZX-48K | 13 | **1,794** | Off by 1,781 t-states |
| ZX-128K | 13 | **2,056** | Off by 2,043 t-states |

### 4.4 Visual impact

With `intstart=13`, the INT fires 13 t-states into the frame. The ISR starts
executing immediately, and the CPU has the **entire frame** (71,680 t-states for
Pentagon) before the next INT. On real Pentagon hardware, the ISR fires near the
end of the frame and has only about **61 t-states** in the current frame before
it wraps to the next frame.

This means:
- **Timing-relative-to-INT calculations are completely wrong** — any code that
  counts t-states from INT to determine raster position will be off by the
  entire frame offset
- **Synchronized effects (raster bars, multicolor) start at the wrong position**
- **Frame-precise code runs at the wrong time relative to screen content**
- The bug is masked for simple ROM-based programs that just increment a frame
  counter in the ISR, but breaks anything cycle-precise

---

## 5. INT duration per model

The MiSTer HDL also shows model-specific INT duration:

| Model | INTCnt max | Duration (HC) | Duration (T-states) | Current `intlen` |
|---|---|---|---|---|
| Pentagon | 63 | 64 | 32 | 32 ✅ |
| ZX-48K | 63 | 64 | 32 | 32 ✅ |
| ZX-128K | 71 | 72 | 36 | 32 ❌ |

ZX-128K INT lasts 4 t-states longer than the others. This is because the ZX-128K
ULA (6C EDITION) generates a slightly longer INT pulse.

---

## 6. Pentagon mid-line INT — why it matters

The Pentagon's INT at hc=326 (mid-line) is the most unique characteristic. Here's
why it's critical:

### 6.1 Raster position at ISR entry

When the Pentagon INT fires at hc=326, the raster beam is:
- On line vc=239 (47 lines below the last paper line)
- At horizontal position 326/448 = 73% through the line
- In the right border / HSync area

When the ZX-48K INT fires at hc=4, the raster beam is:
- On line vc=248 (56 lines below the last paper line)  
- At horizontal position 4/448 < 1% — essentially the start of the line
- In the left border area

### 6.2 Effect on cycle-counted code

A Pentagon demo might do:
```z80
; ISR entry: raster is at vc=239, hc=326
; After ISR overhead (~100 T): raster advanced by 100 T
; Need to wait until vc=272 (top border start of NEXT frame)
; Distance = (320-239)*224 - 326/2 + ... = specific cycle count
```

If the emulator fires INT at t-state 13 instead of 71,619, the cycle count
from INT to any screen position is off by ~71,606 t-states. The demo will:
- Either run its effect at the completely wrong time
- Or crash because it expects to be near the end of the frame

### 6.3 Why INT wraps across the frame boundary

On real Pentagon hardware, INT fires at t-state ~71,619 out of 71,680. The ISR
starts executing in the current frame but quickly wraps into the next frame:

```
Frame N:     ... paper ... bottom_border ... INT at 71,619 ...
                                                ↓
Frame N+1:   ISR continues ... VSync ... top_border ... paper ...
```

The emulator's `Z80FrameCycle` handles this via:
```cpp
if (int_end >= frameLimit) {
    int_end -= frameLimit;
    cpu.int_pending = true;
    int_occurred = true;
}
```

So the emulator's infrastructure **can** handle INT wrapping — it just needs
the correct `intstart` value.

---

## 7. Required configuration changes

### 7.1 Per-model INI fixes

**`data/configs/pentagon128k/unreal.ini`:**
```ini
intstart=71619   ; Pentagon INT at vc=239, hc=326
intlen=32
```

**`data/configs/spectrum48/unreal.ini`:**
```ini
intstart=1794    ; ZX-48K INT at vc=248, hc=4 (wraps to line 8)
intlen=32
```

**`data/configs/spectrum128/unreal.ini`:**
```ini
intstart=2056    ; ZX-128K INT at vc=248, hc=8 (wraps to line 9)
intlen=36        ; ZX-128K INT is 36 T-states, not 32
```

### 7.2 Verification formula

For any model, the correct `intstart` can be verified:

```
MiSTer vc, hc → emulator tstate:
  paper_start_line = vSyncLines + vBlankLines + screenOffsetTop
  emulator_line = (MiSTer_vc + paper_start_line) mod total_lines
  intstart = emulator_line * tstatesPerLine + hc / 2
```

### 7.3 Architectural note

The current design mixes INT generation (which belongs in the ULA/video
controller) with the CPU frame loop (in `Z80::Z80FrameCycle`). The code even
has a TODO comment:

```cpp
// z80.cpp line 558
// TODO: move INT forming logic to Screen class since in reality
// it's formed by ULA / frame counters
```

Moving INT generation to the Screen class would be the clean architectural fix,
making the INT position a property of the video mode rather than a config value
that can be set independently.

---

## 8. INT Duration: Cycle-by-Cycle HDL Proof

The HDL uses a counter (`INTCnt`) to control INT duration. Here is the exact
trace proving why ZX-48K/Pentagon = 32 T-states and ZX-128K = 36 T-states.

### 8.1 HDL code under analysis

```verilog
// ula.sv lines 169-173 — clocked on ce_7mn (7 MHz HC rate)

// Trigger:
if( mZX && (vc_next == 248) && (hc_next == (m128 ? 8 : 4))) INT <= 1;
if(!mZX && (vc_next == 239) && (hc_next == 326)) INT <= 1;

// Counter increments while INT is high:
if(INT)  INTCnt <= ((m128 && INTCnt == 71) || (~m128 && INTCnt == 63)) ? 7'd0 : (INTCnt + 1'd1);

// De-assert when counter wraps to 0:
if(INTCnt == 0) INT <= 0;
```

**Clock domain:** `ce_7mn` fires at 7 MHz = HC rate. CPU T-states are at 3.5 MHz.
Therefore **2 HC = 1 T-state**.

### 8.2 Cycle trace (ZX-48K / Pentagon, terminal count = 63)

Initial state after previous frame's INT: `INTCnt = 1` (residual, see §8.4)

| HC cycle | INT (before) | INTCnt (before) | INTCnt (after) | Event |
|---|---|---|---|---|
| T (trigger) | 0→**1** | 1 | 2 | Trigger fires. `if(INTCnt==0)` is false (it's 1) |
| T+1 | 1 | 2 | 3 | Counting |
| T+2 | 1 | 3 | 4 | |
| ... | 1 | k | k+1 | |
| T+61 | 1 | 62 | 63 | |
| T+62 | 1 | 63 | **0** | Terminal count (63) hit → wraps to 0 |
| T+63 | 1→**0** | 0 | 1 | `if(INTCnt==0)` fires → INT goes low. Counter increments to 1 |

**Total: INT high for 64 HC cycles = 32 T-states** ✅

### 8.3 Cycle trace (ZX-128K, terminal count = 71)

| HC cycle | INT (before) | INTCnt (before) | INTCnt (after) | Event |
|---|---|---|---|---|
| T (trigger) | 0→**1** | 1 | 2 | Trigger fires |
| ... | 1 | k | k+1 | Counting |
| T+69 | 1 | 70 | 71 | |
| T+70 | 1 | 71 | **0** | Terminal count (71) hit → wraps to 0 |
| T+71 | 1→**0** | 0 | 1 | INT de-asserts |

**Total: INT high for 72 HC cycles = 36 T-states** ✅

### 8.4 The INTCnt residual — why it matters

After INT de-asserts, `INTCnt` is left at **1**, not 0. This is critical:

- At the next frame's trigger point (lines 169-170), `INT <= 1` is set
- On the same cycle, line 173 checks `if(INTCnt == 0) INT <= 0`
- Since `INTCnt = 1` (not 0), the de-assertion does **not** fire
- INT stays high, and the counter begins counting from 2

If `INTCnt` were 0 at trigger time, lines 169-170 and line 173 would fire
simultaneously: INT would be set to 1 and immediately back to 0, and INT
would never fire. The residual value of 1 prevents this race condition.

### 8.5 Emulator equivalence

The emulator models this with a simpler but functionally equivalent approach:

```cpp
// ProcessInterrupts() — z80.cpp lines 559-566

// INT asserted when cpu.t enters the window
if (!int_occurred && cpu.t >= int_start) {
    int_occurred = true;
    cpu.int_pending = true;
}

// INT de-asserted when cpu.t exits the window
if (cpu.int_pending && (cpu.t >= int_end))
    cpu.int_pending = false;
```

Where `int_end = intstart + intlen`. This produces the same net result:

| Model | intstart | intlen | INT high from t-state | INT high until t-state | Duration |
|---|---|---|---|---|---|
| Pentagon | 71619 | 32 | 71619 | 71651 | 32 T ✅ |
| ZX-48K | 1794 | 32 | 1794 | 1826 | 32 T ✅ |
| ZX-128K | 2056 | 36 | 2056 | 2092 | 36 T ✅ |

The emulator does **not** model the INTCnt residual or re-trigger protection.
This is acceptable because:
1. INT fires exactly once per frame (no re-trigger scenario)
2. The window `[intstart, intstart+intlen)` is always within one frame
3. For Pentagon, `int_end (71651) < frameLimit (71680)` — no wrap needed

### 8.6 Verification: all 11 call sites use config.intlen

Every INT-handling code path reads `config.intlen` — there are no hardcoded
duration values anywhere in the emulator:

| File | Method | Line | Uses `config.intlen` |
|---|---|---|---|
| `z80.cpp` | `Z80FrameCycle()` | 380 | ✅ |
| `emulator.cpp` | `RunSingleCPUCycle()` | 1236 | ✅ |
| `emulator.cpp` | `RunNCPUCycles()` | 1284 | ✅ |
| `emulator.cpp` | `RunFrame()` | 1344 | ✅ |
| `emulator.cpp` | `RunNFrames()` | 1441 | ✅ |
| `emulator.cpp` | `RunTStates()` | 1501 | ✅ |
| `emulator.cpp` | `RunUntilScanline()` | 1556 | ✅ |
| `emulator.cpp` | `RunNScanlines()` | 1618 | ✅ |
| `emulator.cpp` | `RunUntilNextScreenPixel()` | 1705 | ✅ |
| `emulator.cpp` | `RunUntilInterrupt()` | 1783 | ✅ |
| `emulator.cpp` | `RunUntilCondition()` | 1860 | ✅ |
