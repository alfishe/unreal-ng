# Xpeccy+ vs Unreal-NG Video Rendering Analysis

## Summary

Based on a thorough analysis of the Xpeccy+ rendering engine, it does **not** visualize the actual VBLANK area, but it **does** render the invisible overscan portion of the border, which creates the extremely tall frame visible in that emulator.

## Key Findings

### 1. VBLANK is Strictly Skipped in Xpeccy+

In `src/libxpeccy/video/video.c` and `vidcommon.h`, Xpeccy+ strictly separates the full frame size from the blank periods. During the rendering loop (`vidSync` / `vid_dot_full`), it checks `vid->vvis` and `vid->hvis` to ensure the raster beam is within the visible boundaries (`lcut.y` and `rcut.y`). The `blank.y` area is explicitly clipped out and not drawn to the buffer.

### 2. The "Invisible" Overscan is Rendered

The massive top border visible in Xpeccy+ is defined in `src/xcore/config.cpp` (Line 107). The author explicitly documented the Pentagon geometry:

```cpp
// Pentagon geometry:
// rows: 16Vblk + (16 invis + 48 vis) top border + 192 screen + 48 bottom border = 320 rows
// cols: 64Hblk + 72 left border + 256 screen + 56 right border = 448 dots (224T)
vLayout vlay = {{448,320},{72,64},{64,16},{256,192},{0,0},64};
```

In this layout struct:
- `blank.y = 16` — This is the true VBLANK, and it is **not** rendered
- `bord.y = 64` — This is the top border start position

The key is in the comment: `(16 invis + 48 vis) top border`. Xpeccy+ combines:
- 48 standard visible border lines
- 16 invisible overscan border lines that immediately follow VBLANK

Because the emulator maps the entire `bord.y` property to the output window, users see those extra 16 lines of border that a real CRT monitor's bezel or overscan calibration would physically hide.

## Frame Size Comparison

| Property | Xpeccy+ | Unreal-NG | MiSTer |
|----------|---------|-----------|--------|
| Total frame | 448×320 | 352×288 | 340×284 |
| Visible area | 384×288 | 352×288 | 340×284 |
| Screen | 256×192 | 256×192 | 256×192 |
| Left border | 72 px | 48 px | 42 px |
| Top border | 64 lines (16 invis + 48 vis) | 48 lines | 46 lines |
| Right border | 56 px | 48 px | 42 px |
| Bottom border | 48 lines | 48 lines | 46 lines |

## Dynamic Viewport in Xpeccy+

Xpeccy+ implements a `brdsize` slider (0.0 to 1.0) that dynamically shifts the visible viewport:

```c
// With brdsize slider:
lcut.y = bord.y * (1.0 - brdsize);   // Cut from TOP
rcut.y = bord.y + scrn.y + brdr.y * brdsize;  // Cut from BOTTOM
```

| brdsize | Top lines shown | Visible height |
|---------|-----------------|----------------|
| 1.0 | 64 (full overscan) | 304 |
| 0.75 | 48 (standard border) | 288 |
| 0.5 | 32 | 256 |
| 0.0 | 0 (screen only) | 192 |

## Conclusion for Unreal-NG

Unreal-NG's decision to crop the image to 352×288 (48-line top border) is actually **more faithful to physical hardware display constraints**. Xpeccy+ exposes the "perfect mathematical canvas" by rendering the overscan border, which has led some demo authors (e.g., TmK) to unknowingly draw effects into an area that users on real Pentagon hardware couldn't even see due to CRT overscan.

## Proposed Solution

Introduce a new video mode in Unreal-NG that includes overscan areas for:
1. Demo development/debugging (see what's rendered in overscan)
2. Compatibility testing with Xpeccy+ content
3. Accurate timing visualization

### Proposed Frame Dimensions

**Standard Mode (current):**
- 352×288 visible (48px border on each side)
- Matches typical CRT visible area

**Overscan Mode (new):**
- 384×304 visible (matching Xpeccy+ full border)
- Or 384×288 (matching horizontal, keeping vertical standard)
- Exposes the 16 invisible overscan lines at top

### Implementation Notes

1. Add `VideoModeEnum::M_PENTAGON_OVERSCAN` or similar
2. Extend `RasterDescriptor` with overscan variants
3. Adjust framebuffer allocation for larger frame
4. Update Screen rendering to include overscan lines
5. Consider making this a runtime toggle vs. separate mode

## References

- Xpeccy+ source: `src/libxpeccy/video/video.c`, `vidcommon.h`
- Xpeccy+ config: `src/xcore/config.cpp` (line 107)
- MiSTer Pentagon: `ula.sv` (340×284 framebuffer, intstart=7)
- Unreal-NG: `core/src/emulator/video/screen.h` (RasterDescriptor)
