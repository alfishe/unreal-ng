# 18 — INT-to-Paper Geometry: The Missing +16T (71619 → 71635)

**Date:** 2026-08-14  
**Scope:** Pentagon `intstart` final calibration — raster window geometry vs real hardware  
**Reference Core:** `core/src/emulator/video/zx/screenzx.cpp` (`CreateTstateLUT`), `core/src/emulator/config.cpp`  
**Trigger:** User retest of *Across the Edge* `fix_0` after the doc-17 HandleINT fix: with `intstart=71619` the border effect lands ~16T (32 px) ahead of paper; sweeping `intstart` shows **71635 is closest, 2 px (1T) residual**.

---

## 1. What the Doc-14 Formula Missed

Doc 14 converts the MiSTer HDL trigger (`vc=239`, `hc=326`) into an emulator frame t-state:

```
intstart = emulatorLine * tstatesPerLine + hc/2 = 319*224 + 163 = 71619
```

This maps **line starts** correctly (our line 80 ↔ MiSTer `vc=0`, both first paper lines),
but it silently assumes the horizontal position of paper **within a line** also matches.
It does not — and that is the entire remaining discrepancy.

## 2. Our Actual Raster Line Layout (from Code)

`ScreenZX::CreateTstateLUT()` / `TransformTstateToFramebufferCoords()`
(`core/src/emulator/video/zx/screenzx.cpp`):

```cpp
framebufferX = (t % tstatesPerLine) * pixelsPerTState;   // x = T_in_line * 2
framebufferY =  t / tstatesPerLine - (vSyncLines + vBlankLines);
// paper: pixelX in [screenOffsetLeft, screenOffsetLeft+screenWidth) = [48, 304)
```

Framebuffer x=0 sits at **line T-state 0**; the invisible 48T (hSync+hBlank) is
**clipped at the end** of the line (x ≥ 352 → `RT_BLANK`). Pentagon line layout:

```
T_in_line:  0 ........ 23 | 24 .............. 151 | 152 ... 175 | 176 ... 223
Pixel x:    0 ........ 47  | 48 ................. 303 | 304 ... 351 | (clipped)
            left border    |       PAPER 256px       | right border | invisible
```

- **Paper first pixel: T = 80×224 + 24 = 17944** (line 80, 24T in).
- Note: `_rasterState`'s horizontal areas (blank 0..47T, border 48..71T, paper
  72..199T) describe a *different* (blank-at-line-start) layout; they feed the
  contention model, **not** the framebuffer mapping. For Pentagon (no contention)
  this inconsistency is harmless — but it is why paper-at-72T derivations were wrong.

## 3. The Invariant: INT-to-Paper Distance

The physically meaningful, window-independent quantity is the distance from the
INT trigger to the first paper pixel. Software (incl. *Across the Edge*) is tuned
to this on real hardware:

| Implementation | INT position (own frame) | Paper first pixel | INT→paper |
|---|---|---|---|
| **Real Pentagon** (Unreal Speccy calibration, `conf.paper=17989`, INT at frame wrap) | t=0 | 80×224+69 = **17989** | **17989 T** |
| ZXMAK2 Pentagon (`c_ulaIntBegin=0`, line 80 tact 65) | t=0 | 80×224+65 = 17985 | 17985 T |
| MiSTer (INT `vc239/hc326`, paper at `hc=0` of `vc=0`) | — | — | 17981 T |
| **Ours @ 71619** | 71619 | 17944 | 61+17944 = **18005 T** |
| **Ours @ 71635** | 71635 | 17944 | 45+17944 = **17989 T** ✓ |

- With `71619` our INT→paper is **18005 T = +16 T** vs the real-Pentagon 17989 T
  → border effects (timed from INT, hardware-tuned) hit **16 T (32 px) early**
  vs paper — exactly the user's `fix_0` observation.
- `71635` reproduces the real-hardware distance **exactly**:
  `intstart = paper_T − D_real + frame = 17944 − 17989 + 71680 = 71635`.

### 3.1 Reading the Formula Term by Term

The same derivation lives as a one-line comment in `config.cpp`
(`ApplyModelTimingDefaults`) and in the Pentagon INIs:

```ini
intstart=71635 ; t-states before int (real Pentagon INT-to-paper 17989T: paper@17944 - 17989 + frame 71680)
```

Each term:

| Term | Value | Meaning |
|---|---|---|
| `paper@17944` | 17944 | **Our** frame coordinate of the first paper pixel: first paper line is raster line 80, paper starts 24T into the line (48px left border ÷ 2px/T) → 80×224 + 24. Computed by `Screen::GetPaperStartTstate()`. |
| `INT-to-paper 17989T` | 17989 | **Physical constant of real Pentagon hardware**: T-states between INT assertion and the next frame's first paper pixel. Calibrated on real machines (Unreal Speccy `conf.paper=17989`, INT at its frame wrap). |
| `frame 71680` | 71680 | Pentagon frame length: 320 lines × 224T. |

The `+ frame` is a **wrap-around**: the hardware distance exceeds our paper
coordinate (17989 > 17944), so plain subtraction would go negative — INT fires
*before* the frame ends, and the remaining distance to paper continues in the
next frame:

```
frame t:  0 ──────────────── 71635 ──── 71680 │ 0 ────── 17944 ────
                                ▲ INT          │        ▲ paper (next frame)
                                └──── 45T ─────┴─ 17944T ┘
                                total: 45 + 17944 = 17989T ✓
```

The formula is deliberately a **recipe, not a constant**: the INT's absolute
frame coordinate is not portable between implementations (t=0 anchors and
paper-in-line offsets differ — MiSTer's trigger reads 71619 in our frame, see
§1). The INT→paper distance is the only invariant cycle-precise software is
tuned to, so `intstart` must always be re-derived from **our own** paper
position, never copied as an absolute value from another core.

The `; t-states before int` prefix is the original upstream comment convention:
the value counts from frame start to the INT trigger.

The +16T decomposes as `(61 − 0) + (24 − 69) mod 224`: MiSTer's INT sits 61T
before line end while Unreal Speccy's sits at its line-0 boundary, and our paper
sits 24T into the line vs real hardware's 69T-in-its-window. Same pixels on
screen, different T-window conventions.

## 4. Residual 1T (2 px) at 71635

Within the spread between "correct" implementations (MiSTer 17981 / ZXMAK2
17985 / Unreal Speccy 17989 — 8T total). Possible contributors:
- MiSTer registers INT one HC after the `vc/hc` compare (`INT <= 1` on next edge) → +0.5T;
- T80 IM2 response ≈ 18T vs our 19T (doc 17 §2.2) → −1T;
- true hardware value may be 17988/17990 rather than 17989.

~~Absorbable via INI override (`intstart=71634/71636`) or demo fix variants
(±2T steps) if a specific production needs pixel-exact. Default stays 71635.~~

> [!IMPORTANT]
> **Superseded by [doc 19](19-io-port-write-tstate-placement.md):** the user's
> `intstart` sweep showed the residual is **NOT absorbable** — the demo's effect
> anchors to a mid-loop INT acceptance (staircase-quantized to instruction
> boundaries), so `intstart` moves the effect only in 4..13T jumps. The actual
> cause of the last 2 px was the IO port write landing at the IO-cycle entry
> (T1) instead of the IORQ T-state (T2) — fixed 2026-08-14 in all 18 IO opcodes.
> This section's ±1T candidates (IM2 18T/19T, +0.5T HC) remain relevant only if
> a residual survives that fix.

## 5. Changes

| File | Change |
|---|---|
| `core/src/emulator/config.cpp` | `MM_PENTAGON` `intstart` 71619 → **71635** with derivation comment |
| `data/configs/pentagon128k/unreal.ini` | `intstart=71635` |
| `data/configs/pentagon512k/unreal.ini` | `intstart=71635` |
| `core/tests/emulator/video/int_timing_test.cpp` | Pentagon expectations 71619 → 71635 |
| `14-int-fine-tuning.md` | Master table Pentagon row updated, note points here |

## 6. Follow-ups (Not in This Change)

- **ZX-48K check**: ours INT→paper = 16152−1794 = **14358 T** — already inside the
  classic 14335..14361 range; likely fine, verify with a 48K multicolor test.
- **ZX-128K check**: ours = 16212−2056 = **14156 T** vs Unreal Speccy 14361 T
  (≈ −205 T ≈ −0.9 line — the `vBlankLines 16→15` change may need revisiting).
  Needs the same geometry pass before release.
- `_rasterState` horizontal areas (blank-at-line-start) vs render mapping
  (blank-at-line-end) should eventually be unified to avoid future confusion.

## 7. Verification

- [x] `INTTiming_Test.*` 29/29 with 71635 assertions
- [x] `ninja core` clean under `-Werror`
- [x] Arithmetic: 17944 − 17989 + 71680 = 71635 (exact, no empirical fudge)
- [ ] *Across the Edge* `fix_0` retest at 71635 — done 2026-08-14: 2 px residual
      remained; root-caused and fixed in [doc 19](19-io-port-write-tstate-placement.md)
      (IO port write T placement), awaiting retest
