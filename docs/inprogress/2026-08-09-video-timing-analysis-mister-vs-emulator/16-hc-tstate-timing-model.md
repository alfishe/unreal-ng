# 16 — HC vs T-State Timing Model

**Date:** 2026-08-13  
**Scope:** Horizontal Counter (HC) to T-state mapping, attribute prefetch pipeline  
**Reference HDL:** `ZX-Spectrum_MISTer/rtl/ula.sv`  
**Reference core:** `core/src/emulator/video/zx/screenzx.cpp`

---

## 1. Clock Domains

The ZX Spectrum has two primary clock domains:

| Clock | Frequency | Purpose |
|-------|-----------|---------|
| **Pixel clock** | 7 MHz | ULA pixel output, horizontal counter (HC) |
| **CPU clock** | 3.5 MHz | Z80 T-states |

**Fundamental relationship:**
```
1 T-state = 2 HC cycles = 2 pixels
```

---

## 2. Horizontal Counter (HC)

The Horizontal Counter increments once per pixel clock (7 MHz). It counts pixels within each scanline.

### 2.1 Pentagon Line Structure (448 pixels = 224 T-states)

```
HC:     0 ──────────────────────────────────────────────────────────── 447
        |← visible (352 px) →|←── hBlank+hSync (96 px) ──→|

Pixels: |← left border (48) →|← screen (256) →|← right (48) →|← invisible (96) →|
```

### 2.2 HC to T-state Conversion

```
T-state = HC / 2  (integer division)
HC = T-state * 2  (start of T-state)
```

Example:
- HC 0-1 → T-state 0
- HC 2-3 → T-state 1
- HC 144-145 → T-state 72 (first screen pixel)

---

## 3. Character Cell Timing (16 HC = 8 T-states = 8 pixels)

Each character cell is 8 pixels wide. The ULA processes two cells per 16-HC period using a pipelined fetch-latch-shift architecture.

### 3.1 MiSTer ULA Pipeline (from ula.sv)

```verilog
// Address generation
case(hc_next[3:0])
    'h8,'hC: vaddr <= pixel_address;    // Set VRAM address for pixel byte
    'hA,'hE: vaddr <= attr_address;     // Set VRAM address for attribute
endcase

// Data latching
case(hc_next[3:0])
    'h9,'hD: bits <= vram_dout;         // Latch pixel byte
    'hB,'hF: attr <= vram_dout;         // Latch attribute byte
endcase

// Shift register load (display start)
if(hc_next[2:0] == 4) begin
    SRegister <= bits;                   // Load shift register
    AttrOut <= attr;                     // Apply attribute colors
end
```

### 3.2 16-HC Cell Timing Diagram

```
HC (hex): 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
HC (dec): 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
          ├──────── Cell N ────────┼──────── Cell N+1 ────────┤

T-state:  ├──0──┼──1──┼──2──┼──3──┼──0──┼──1──┼──2──┼──3──┤
Pixels:   0  1  2  3  4  5  6  7  0  1  2  3  4  5  6  7

Actions:                    ↓  ↓  ↓  ↓        ↓  ↓  ↓  ↓
                           8  9  A  B        C  D  E  F
                           │  │  │  │        │  │  │  │
                           │  │  │  └─ Attr N+1 latched
                           │  │  └──── Addr for attr N+1
                           │  └─────── Pixels N+1 latched
                           └────────── Addr for pixels N+1
                              ↓
                           At HC 4 (T2): Load shift reg, apply colors
```

### 3.3 Timing Summary Table

| HC (hex) | HC (dec) | T-state | Pixel | Action |
|----------|----------|---------|-------|--------|
| 0-3 | 0-3 | 0-1 | 0-3 | Shift out pixels, display |
| 4 | 4 | 2 | 4 | **Load shift register, apply attr colors** |
| 5-7 | 5-7 | 2-3 | 5-7 | Continue shifting |
| 8 | 8 | 4 | 0 | Set address for next pixel byte |
| 9 | 9 | 4 | 1 | **Latch pixel byte** |
| A | 10 | 5 | 2 | Set address for next attribute |
| B | 11 | 5 | 3 | **Latch attribute byte** |
| C | 12 | 6 | 4 | Load shift register (for 2nd half) |
| D | 13 | 6 | 5 | Latch pixel byte (for next cell) |
| E | 14 | 7 | 6 | Set address for attribute |
| F | 15 | 7 | 7 | Latch attribute (for next cell) |

---

## 4. Attribute Prefetch Timing

### 4.1 Key Insight

The attribute is **fetched before it's displayed**. From the table above:

- **Attribute latched at:** HC B/F (pixels 3/7 of current display)
- **Attribute applied at:** HC 4 of next 8-pixel group (pixel 4)
- **Prefetch lead time:** 5 HC cycles = 2.5 T-states

### 4.2 Implications for Multicolor Effects

Demos that change attributes mid-line must write **before** the ULA latches the value:

```
Timeline (HC within line):
    0   4   8   12  16  20  24  ...
    │   │   │   │   │   │   │
    │   │   └── Attr for cell 0 latched at HC 11
    │   └────── Cell 0 display starts at HC 4
    └────────── Border ends, paper begins
    
If demo writes attr at:
  - HC < 11: Change takes effect for cell 0 ✓
  - HC ≥ 11: Change takes effect for cell 1 (too late for cell 0)
```

### 4.3 Pentagon Discrete Logic

Pentagon uses discrete logic instead of a ULA chip, but the same prefetch pipeline applies. The video circuitry still needs to fetch data before displaying it. The main differences:

1. **Border update:** Immediate (1T) vs ULA's 4T latching
2. **No memory contention:** Pentagon doesn't stall CPU during video fetch
3. **Same prefetch timing:** Data must be fetched before display

---

## 5. Emulator Implementation

### 5.1 pixelXBit Mapping

Our emulator uses `pixelXBit` which represents `zxX % 8`:

| pixelXBit | Pixels | T-state within cell | HC equivalent |
|-----------|--------|---------------------|---------------|
| 0 | 0-1 | 0 | 0-1 |
| 2 | 2-3 | 1 | 2-3 |
| 4 | 4-5 | 2 | 4-5 |
| 6 | 6-7 | 3 | 6-7 |

Note: pixelXBit increments by 2 because each T-state covers 2 pixels.

### 5.2 Correct Fetch Point (Implemented)

To match MiSTer timing (attribute latched at HC 11):

```cpp
// Fetch occurs at pixelXBit == 2 (HC 4-5) for the CURRENT cell
// Apply occurs at pixelXBit == 4 (HC 8-9, matching MiSTer's shift register load)

// In CreateTstateLUT():
if ((zxX % 8) == 2) {
    entry.fetchSymbolX = entry.symbolX;  // Fetch for current cell
    ...
}

// In Draw():
if (lut.pixelXBit == 4 || (lut.pixelXBit == 0 && lut.symbolX == 0)) {
    _latchedPixels = _prefetchedPixels;
    _latchedAttributes = _prefetchedAttributes;
}
```

### 5.3 Pipeline Model (Implemented)

```
Cell 0:
  Border (1T before paper): Prefetch cell 0 data
  pixelXBit 0:              Apply prefetched values, start cell 0

Cells 1-31:
  pixelXBit 0-1:            Display pixels 0-3 with previous latched values
  pixelXBit 2:              Prefetch current cell's data from memory
  pixelXBit 4:              Apply prefetched values (shift register load)
  pixelXBit 4-6:            Display pixels 4-7 with newly applied values
```

---

## 6. Cross-Reference to Other Emulators

### 6.1 ZXMAK2 (C#)

Uses `UlaAction` enum with prefetch built into the action table:
- `Shift2AndFetchA1` at phase 7 of cell
- Colors applied at `Shift1Last` (phase 3)

### 6.2 Xpeccy (C)

In `ula_dot()` function:
```c
case 12: nxtbyte = fetch_pixel;   // HC 12 = pixel 4
case 14: nxtatr = fetch_attr;     // HC 14 = pixel 6  
case 0:  apply(nxtbyte, nxtatr);  // HC 0 = next cell start
```

### 6.3 Original Unreal

Uses batch rendering (`drawnomc.cpp`) for non-multicolor mode. The `alco` structure pre-computes per-line data for AlCo mode.

---

## 7. Summary

| Concept | Value | Notes |
|---------|-------|-------|
| Pixel clock | 7 MHz | HC increments |
| CPU clock | 3.5 MHz | T-states |
| HC per T-state | 2 | Fixed ratio |
| Pixels per T-state | 2 | Fixed ratio |
| Character cell width | 8 pixels | 4 T-states |
| 16-HC period | 8 T-states | Two cells |
| Attr latch point | HC 11/15 | Pixels 3/7 |
| Shift register load | HC 4/12 | Pixels 4/4 |
| Prefetch lead time | ~5 HC | ~2.5 T-states |

---

## 8. Files Modified

| File | Change |
|------|--------|
| `screenzx.h` | Added `fetchSymbolX`, `fetchScreenOffset`, `fetchAttrOffset` to LUT; prefetch/latch registers |
| `screenzx.cpp` | Prefetch at pixelXBit 2 (was 6), apply at pixelXBit 4 (was 0), fetch current cell (was next) |

---

## 9. Related Documents

- [03-multicolor-latching.md](03-multicolor-latching.md) — Multicolor effect analysis
- [14-mister-reference-model.md](14-mister-reference-model.md) — MiSTer timing reference
- [15-mister-frame-formation.md](15-mister-frame-formation.md) — Frame structure
