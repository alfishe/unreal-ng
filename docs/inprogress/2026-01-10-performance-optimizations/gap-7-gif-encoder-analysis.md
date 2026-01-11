# GIF Encoder Performance Analysis

## Key Finding: Palette Recalculation on Every Frame

The current `gif.cpp` implementation rebuilds the color palette **on every frame**:

```cpp
// GifWriteFrame (line 698-725)
bool GifWriteFrame(GifWriter* writer, const uint8_t* image, ...) {
    // Palette rebuilt here - expensive!
    GifMakePalette((dither ? NULL : oldImage), image, width, height, bitDepth, dither, &pal);
    
    if (dither)
        GifDitherImage(...);  // Uses palette
    else
        GifThresholdImage(...);  // Uses palette
    
    GifWriteLzwImage(...);
}
```

### Cost Breakdown: `GifMakePalette`

1. **Memory allocation**: Copies entire frame (~192KB for 256×192)
2. **Changed pixel detection**: O(width × height)
3. **k-d tree construction**: O(n log n) median split
4. **Palette averaging**: O(n) per color

---

## ZX Spectrum Reality

| Mode | Unique Colors | Palette Changes |
|------|---------------|-----------------|
| Standard ZX | 16 | Never |
| Timex hi-color | 16 | Never |
| TSConf/modern | Up to 256 | Rarely (mode switch) |

**For standard ZX Spectrum:**
- Palette is fixed at 16 colors
- Palette recalculation is 100% wasted CPU time
- We know the exact colors at compile time

---

## Optimization Opportunities

### Option 1: Pre-built Fixed Palette (Recommended)

```cpp
// Create ZX Spectrum palette once at startup
GifPalette CreateZXSpectrumPalette()
{
    GifPalette pal;
    pal.bitDepth = 4;  // 16 colors
    
    // Standard ZX colors
    const uint32_t zxColors[16] = {
        0x000000, 0x0000CD, 0xCD0000, 0xCD00CD,  // Black, Blue, Red, Magenta
        0x00CD00, 0x00CDCD, 0xCDCD00, 0xCDCDCD,  // Green, Cyan, Yellow, White
        0x000000, 0x0000FF, 0xFF0000, 0xFF00FF,  // Bright variants
        0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF
    };
    
    for (int i = 0; i < 16; i++) {
        pal.r[i] = (zxColors[i] >> 16) & 0xFF;
        pal.g[i] = (zxColors[i] >> 8) & 0xFF;
        pal.b[i] = zxColors[i] & 0xFF;
    }
    
    // Build k-d tree for fast lookup
    BuildKDTree(&pal);
    
    return pal;
}
```

### Option 2: Palette Caching with Dirty Flag

```cpp
class GIFEncoder {
    GifPalette _cachedPalette;
    bool _paletteValid = false;
    
    void InvalidatePalette() { _paletteValid = false; }
    
    void OnVideoFrame(...) {
        if (!_paletteValid) {
            BuildPalette();
            _paletteValid = true;
        }
        // Use cached palette
    }
};
```

### Option 3: Sprite Processor Notification (User Suggested)

For TSConf/modern modes with dynamic palettes:
```cpp
// Called by sprite processor when palette changes
void OnPaletteChanged(const uint32_t* newPalette, size_t count) {
    RebuildGifPalette(newPalette, count);
}
```

---

## Implementation Plan

### Phase 1: Fixed Palette for Standard ZX (Immediate Win)

1. Add `useFixedPalette` flag to `EncoderConfig`
2. Pre-compute ZX Spectrum 16-color palette
3. Skip `GifMakePalette` call entirely when using fixed palette
4. Keep existing `GifThresholdImage` for color mapping

**Expected speedup:** ~10-20x for typical content

### Phase 2: Palette Notification System (Future)

1. Add palette change callback to GIFEncoder
2. Wire to video processor for mode switches
3. Only rebuild palette on actual changes

---

## Benchmark Projection

| Scenario | Current | With Fixed Palette |
|----------|---------|-------------------|
| Static content | ~355 μs | ~50 μs (estimate) |
| Menu scroll | ~1.17 ms | ~200 μs (estimate) |
| High-change | ~14 ms | ~1 ms (estimate) |

---

## Files to Modify

- `encoder_config.h` - Add `useFixedPalette` flag
- `gif_encoder.h` - Add fixed palette member
- `gif_encoder.cpp` - Use fixed palette path
- May need to modify `gifanimationhelper.h` or create alternative path
