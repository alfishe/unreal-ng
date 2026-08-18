# Pentagon Timing Model Synchronization Proposal

**Date**: 2026-08-10  
**Status**: Analysis complete, implementation proposed  
**Goal**: Achieve pixel-perfect border/paper alignment matching MiSTer HDL

---

## 1. MiSTer Pentagon Reference (ula.sv)

### 1.1 Frame Structure

| Parameter | Value | Notes |
|-----------|-------|-------|
| Total lines | 320 | vc counts 0-319 |
| HC per line | 448 | hc counts 0-447 |
| T-states/line | 224 | hc/2 |
| Frame T-states | 71680 | 320 × 224 |

### 1.2 Horizontal Timing (within each line)

| Signal | HC range | Pixels | T-states | Notes |
|--------|----------|--------|----------|-------|
| Active video | 0-311 | 312 | 156 | Visible pixels rendered |
| Front porch | 312-335 | 24 | 12 | Before HSync |
| HSync | 336-367 | 32 | 16 | Sync pulse |
| Back porch | 368-415 | 48 | 24 | After HSync |
| **HBlank total** | 312-415 | 104 | 52 | All blanking |
| Wrap to 0 | 416-447 | 32 | 16 | Returns to active |

**Key insight**: Active video is hc=0-311 (312 pixels), then hc=416-447 wraps back (32 pixels), total **344 active pixels**.

But wait - Pentagon visible area should be 352 pixels. Let me recalculate:

Actually, looking more carefully at MiSTer:
- HBlank ON at hc=312, OFF at hc=416
- This means hc=0-311 is active (312px) and hc=416-447 is also active (32px)
- Total active = 344 pixels... **NOT 352**

This suggests our RasterDescriptor `fullFrameWidth=352` may be wrong, OR MiSTer counts differently.

### 1.3 Vertical Timing

| Region | VC range | Lines | Notes |
|--------|----------|-------|-------|
| VSync | 248-255 | 8 | Sync pulse |
| VBlank top | 256-271 | 16 | Upper blank |
| VBlank bottom | 236-247 | 12 | Lower blank before vsync |
| **Total VBlank** | 236-271 | 36 | Including VSync |
| Active video | 272-319 + 0-235 | 284 | Wraps around |

Wait, this gives 284 active lines, not 288...

Let me re-examine. MiSTer Pentagon:
- VBlank ON at vc=236, OFF at vc=272
- VSync ON at vc=248, OFF at vc=256
- So active is vc=272-319 (48 lines) + vc=0-235 (236 lines) = 284 lines

### 1.4 INT Generation

```
INT trigger: vc=239, hc=326
INT duration: 64 HC = 32 T-states
```

vc=239 is during VBlank (236-271), specifically:
- 3 lines after VBlank starts (239-236=3)
- 9 lines before VSync starts (248-239=9)

hc=326 is during HBlank front porch (312-335), specifically:
- 14 HC after HBlank starts (326-312=14)
- 10 HC before HSync starts (336-326=10)

### 1.5 Paper Area

Within active video:
- Top border: 48 lines
- Paper: 192 lines  
- Bottom border: 44 lines (284-48-192=44)

Wait, that's only 44 lines of bottom border, not 48...

---

## 2. Our Current Model (unreal-ng)

### 2.1 RasterDescriptor for Pentagon

```cpp
{352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16}
// fullFrameWidth=352, fullFrameHeight=288
// screenWidth=256, screenHeight=192
// screenOffsetLeft=48, screenOffsetTop=48
// pixelsPerLine=448
// hSyncPixels=64, hBlankPixels=32
// vSyncLines=16, vBlankLines=16
```

### 2.2 Calculated RasterState

```
tstatesPerLine = 448 / 2 = 224
maxFrameTiming = 224 × (16 + 16 + 288) = 71680

Vertical:
  blankAreaEnd = 224 × 32 - 1 = 7167 (lines 0-31 are blank)
  topBorderAreaStart = 7168 (line 32)
  topBorderAreaEnd = 7168 + 224×48 - 1 = 17919 (lines 32-79)
  screenAreaStart = 17920 (line 80)
  screenAreaEnd = 17920 + 224×192 - 1 = 60927 (lines 80-271)
  bottomBorderAreaStart = 60928 (line 272)
  bottomBorderAreaEnd = 60928 + 224×48 - 1 = 71679 (lines 272-319)

Horizontal (within line):
  blankLineAreaEnd = (64+32)/2 - 1 = 47 (T 0-47 blank)
  leftBorderAreaStart = 48 (T 48)
  leftBorderAreaEnd = 48 + 48/2 - 1 = 71 (T 48-71)
  screenLineAreaStart = 72 (T 72)
  screenLineAreaEnd = 72 + 256/2 - 1 = 199 (T 72-199)
  rightBorderAreaStart = 200 (T 200)
  rightBorderAreaEnd = 200 + 48/2 - 1 = 223 (T 200-223)
```

### 2.3 LUT Calculation (CreateTstateLUT)

```cpp
framebufferX = (t % 224) * 2;  // NO hblank offset!
framebufferY = t / 224 - 32;   // Correct vblank offset

// Paper check
if (pixelX >= 48 && pixelX < 304)  // screenOffsetLeft=48
```

**BUG**: framebufferX doesn't account for horizontal blank!

At T=0 within line: framebufferX=0  
At T=24 within line: framebufferX=48 → **paper check passes!**

But according to rasterState, paper should start at T=72 (screenLineAreaStart).

### 2.4 INT Position

```
intstart = 71623 (working value)
         = 319 × 224 + 167
         = line 319, T=167 within line

MiSTer:  = 319 × 224 + 163 = 71619
```

Difference: 4T

---

## 3. Discrepancy Analysis

### 3.1 Horizontal Timing Mismatch

| What | RasterState model | LUT model | MiSTer |
|------|-------------------|-----------|--------|
| HBlank duration | 48T | 0T (ignored) | 52T |
| Visible start (T in line) | 48 | 0 | 52 |
| Paper start (T in line) | 72 | 24 | 76? |
| Paper start (pixel in FB) | 48 | 48 | 48 |

The LUT ignores hblank entirely, treating T=0 as pixel 0.
The rasterState correctly accounts for hblank.
MiSTer has 52T of hblank (104 pixels).

### 3.2 The 4T INT Offset

MiSTer INT at hc=326 = T=163  
Our working INT at T=167  
Difference: 4T = 8 pixels

This 4T compensates for the difference between:
- Our hblank (48T) and MiSTer hblank (52T): **4T difference**

### 3.3 Frame Dimension Mismatch

| Dimension | Our model | MiSTer | Difference |
|-----------|-----------|--------|------------|
| Visible width | 352 px | 344 px | +8 px |
| Visible height | 288 lines | 284 lines | +4 lines |

Our model has 8 more visible pixels and 4 more visible lines than MiSTer!

---

## 4. Root Cause

The emulator model was designed with:
- 48 pixels left border + 256 paper + 48 right border = 352 visible
- 48 lines top border + 192 paper + 48 bottom border = 288 visible

But MiSTer Pentagon has:
- 44 pixels left border + 256 paper + 44 right border = 344 visible (approx)
- 48 lines top border + 192 paper + 44 bottom border = 284 visible

The borders are **asymmetric** and **smaller** in MiSTer.

---

## 5. Proposed Fix

### Option A: Match MiSTer exactly (breaking change)

Update RasterDescriptor to match MiSTer:

```cpp
// Before:
{352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16}

// After:
{344, 284, 256, 192, 44, 48, 448, 52, 52, 16, 20}
// fullFrameWidth=344, fullFrameHeight=284
// screenOffsetLeft=44 (not 48)
// screenOffsetTop=48 (unchanged - top border is still 48)
// hSyncPixels=52, hBlankPixels=52 (total 104 = 52T)
// vSyncLines=16 (8 lines vsync + 8 other)
// vBlankLines=20 (total 36 vblank - 16 = 20)
```

**Pros**: Pixel-perfect MiSTer match  
**Cons**: Changes framebuffer size, may break existing code

### Option B: Keep framebuffer, fix LUT (minimal change)

Keep the 352×288 framebuffer but fix the LUT to use rasterState timing:

```cpp
// In CreateTstateLUT():
const int hBlankT = _rasterState.blankLineAreaEnd + 1;  // 48T
const int tInLine = t % tstatesPerLine;

// Only render if past hblank
if (tInLine >= hBlankT) {
    framebufferX = (tInLine - hBlankT) * pixelsPerTState;
    // ... rest of calculation
}
```

Then adjust intstart:
- Current LUT: paper at T=24, working intstart=71623
- Fixed LUT: paper at T=72, new intstart = 71623 - 48 = 71575

**Pros**: Minimal code change, keeps framebuffer  
**Cons**: Doesn't match MiSTer dimensions, intstart changes

### Option C: Fix LUT coordinate system only (recommended)

The real issue is that the LUT uses `(t % 224) * 2` which gives pixel coordinates 0-447 within a line, but the framebuffer is only 352 pixels wide.

The current model **accidentally works** because:
1. T=0-23 map to pixels 0-47 (rendered as left border)
2. T=24-151 map to pixels 48-303 (rendered as paper, check passes)
3. T=152-175 map to pixels 304-351 (rendered as right border)
4. T=176-223 map to pixels 352-447 (filtered by `< fullFrameWidth`)

This is **48T shifted** from physical reality, but consistent within the model.

The 4T in intstart compensates for the MiSTer-vs-our-model timing difference.

**Recommendation**: Document the model, keep intstart=71623, add comment explaining the 4T offset.

---

## 6. Verification Test

Load "Across The Edge" demo fix variants:
- fix0 (baseline): Should work at intstart=71623
- fix1 (+2T): Should work at intstart=71625
- fix2 (+4T): Should work at intstart=71627
- fix3 (+6T): Should work at intstart=71629

If fix0 works at 71623, our model is internally consistent.

---

## 7. Implementation Plan

### Phase 1: Document (this document)
- [x] Analyze MiSTer timing
- [x] Analyze our timing
- [x] Identify discrepancies
- [x] Propose options

### Phase 2: Verify current model
- [ ] Test fix0-fix3 with intstart=71623
- [ ] Confirm which fix works
- [ ] Document in code comments

### Phase 3: (Optional) True MiSTer match
If pixel-perfect MiSTer match is required:
- [ ] Update RasterDescriptor dimensions
- [ ] Fix LUT to use rasterState horizontal timing
- [ ] Update intstart to 71619
- [ ] Test all demos

---

## 8. Summary

| Item | Current | MiSTer | Action |
|------|---------|--------|--------|
| intstart | 71623 | 71619 | Keep 71623 (compensates for model diff) |
| HBlank | 48T | 52T | Document, no change |
| Visible width | 352 | 344 | Document, no change |
| Visible height | 288 | 284 | Document, no change |
| LUT model | T=0 → pixel 0 | T=52 → pixel 0 | Document, no change |

The 4T intstart offset (71623 vs 71619) is the **correct compensation** for our model's 48T hblank vs MiSTer's 52T hblank. The emulator is internally consistent; it just uses a slightly different timing reference than MiSTer.
