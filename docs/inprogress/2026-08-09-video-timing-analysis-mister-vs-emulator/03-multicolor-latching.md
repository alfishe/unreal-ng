# 03 — Multicolor Attribute Latching Analysis

**Date:** 2026-08-09
**HDL reference:** `ula.sv` lines 189-216
**Emulator reference:** `screenzx.cpp` lines 649-669, `screenzx.cpp` lines 139-214 (CreateTstateLUT)

---

## 1. What is "multicolor" / "racing the beam"?

The ZX Spectrum ULA has a fixed 8x8 attribute grid: each 8x8 pixel block shares
one ink/paper color pair. "Multicolor" effects bypass this limitation by changing
the attribute byte in RAM while the ULA is still rendering the same scanline.

The trick: write a new attribute byte to the VRAM address the ULA **has already
fetched** but before it fetches the **next** one. If timed correctly, different
8-pixel segments of the same scanline can have different colors.

This requires cycle-exact timing knowledge of when the ULA reads each attribute.

## 2. MiSTer HDL: the fetch-latch pipeline

### 2.1 VRAM address generation

The ULA generates VRAM addresses at specific HC positions within each 16-HC
character cell:

```verilog
// ula.sv lines 190-198
if(~tmx_cfg[1]) case(hc_next[3:0])
    'h8,'hC: vaddr       <= {…, vc[7:6], vc[2:0], vc[5:3], hc_next[7:4], hc_next[2]};
    'hA,'hE: vaddr[14:7] <= {…, 3'b110, vc[7:5]};  // CAS for attributes
endcase
```

The VRAM address is set at HC positions 8 and C (for pixel data) and A and E
(for attribute CAS — column address strobe).

### 2.2 Data latching

Two HC cycles after address setup, the data is latched:

```verilog
// ula.sv lines 210-213
case(hc_next[3:0])
    'h9,'hD: bits <= vram_dout;    // Pixel byte latched
    'hB,'hF: attr <= vram_dout;   // Attribute byte latched
endcase
```

So the attribute byte for character cell N is latched at HC position B or F —
which is **2 t-states before** the shift register uses it.

### 2.3 Shift register loading

```verilog
// ula.sv lines 175-176
if(hc_next[2:0] == 4) begin
    SRegister <= VidEN ? bits : 8'd0;
```

Every 8 HC cycles (4 t-states), the latched pixel byte is loaded into the shift
register and serially shifted out at 1 pixel per HC cycle.

### 2.4 Pipeline timing diagram

```
HC:    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
       |-- pixel display from shift register --------|
                         ^              ^
                         |              |
                    set addr      latch data
                   (pixel+attr)   (pixel+attr)
                         |
                    Latch point:
                    After this, CPU changes to this cell's
                    attribute won't take effect this line
```

### 2.5 Critical timing window

For character cell at column X on scanline Y:
- **Attribute fetch address set at:** HC = 8 + X*16 + 2 (for CAS at A)
- **Attribute data latched at:** HC = 8 + X*16 + 3 (at position B)
- **Display starts at:** HC = 8 + X*16 (next cell, when shift reg reloads)

The CPU has from the **start of the scanline** until HC position B of cell X to
modify the attribute at VRAM address for cell X. After latching, changes are
ignored for the current line.

## 3. Emulator: per-t-state memory reads

### 3.1 Current implementation

```cpp
// screenzx.cpp lines 649-669
if (lut.renderType == RT_SCREEN)
{
    uint8_t* zxScreen = _activeScreenMemoryOffset;
    uint8_t pixels = *(zxScreen + lut.screenOffset + lut.symbolX);
    uint8_t attributes = *(zxScreen + lut.attrOffset + lut.symbolX);
    uint32_t colorInk = _rgbaColors[attributes];
    uint32_t colorPaper = _rgbaFlashColors[attributes];

    // ... branch-free pixel selection for 2 pixels
}
```

`Draw()` is called once per t-state. Within each 8-pixel character cell (which
spans 4 t-states / 4 Draw calls), the method reads:
- `pixels` from `zxScreen + screenOffset + symbolX`
- `attributes` from `zxScreen + attrOffset + symbolX`

### 3.2 The problem

Since `Draw()` reads directly from RAM at every call, and `DrawPeriod()` replays
from `_prevTstate` to `currentTstate`, the emulator effectively shows **whatever
is in RAM at the exact t-state being rendered**.

If the CPU writes a new attribute between t-state N and N+1 within the same
character cell, the emulator will:
- Use the old attribute for t-states before the write
- Use the new attribute for t-states after the write

This produces a **split character cell** — different colors within the same 8px
block — which is impossible on real hardware.

## 4. Impact on multicolor demos

### 4.1 What works correctly

The emulator's per-t-state rendering correctly handles the case where the CPU
changes the attribute for the **next** character cell before the ULA reaches it.
This is the primary multicolor technique and it works because the emulator reads
the new value when it processes the next cell's t-states.

### 4.2 What is wrong

The emulator incorrectly allows **intra-cell** attribute changes. On real
hardware, once the ULA has latched the attribute (at a specific t-state), later
writes to that address during the same cell don't affect the current scanline.

This causes:
- **"Phantom" color splits** within character cells that don't exist on hardware
- **Different appearance** for demos that push timing limits — effects that look
  clean on hardware may show artifacts in the emulator
- **Timing-sensitive routines** that rely on the exact latch point may produce
  shifted or garbled output

### 4.3 Example scenario

Consider a demo that changes the attribute at VRAM address 0x5800 (first cell)
at t-state T:

| T-state of write | Hardware behavior | Emulator behavior |
|---|---|---|
| Before latch (HC < B) | Change takes effect this line | Change takes effect |
| At latch (HC = B) | Undefined / change may or may not show | Change takes effect |
| After latch (HC > B) | Change ignored until next line | **Change shows** ❌ |

## 5. Required fix

### 5.1 Add latched pixel/attribute bytes to ScreenZX

```cpp
// screenzx.h - add to protected fields
uint8_t _latchedPixels;     // Last fetched pixel byte
uint8_t _latchedAttributes; // Last fetched attribute byte
uint8_t _lastLatchedSymbolX;// Symbol X index of last latch (0xFF = none)
uint8_t _lastLatchedRow;    // Screen row of last latch
```

### 5.2 Latch at correct t-state boundary

In `Draw()` or `CreateTstateLUT()`, add a flag to the LUT indicating whether
this t-state is a "latch point" for the current character cell:

```cpp
// In TstateCoordLUT:
bool isLatchPoint;  // True if this t-state is where ULA fetches attr

// In Draw():
if (lut.isLatchPoint)
{
    _latchedPixels = *(zxScreen + lut.screenOffset + lut.symbolX);
    _latchedAttributes = *(zxScreen + lut.attrOffset + lut.symbolX);
    _lastLatchedSymbolX = lut.symbolX;
}

// Use _latchedPixels/_latchedAttributes for rendering instead of live reads
```

### 5.3 Determine latch t-state offset

Based on MiSTer HDL analysis, the attribute latch happens at HC position B
within each 16-HC cell. Translating to t-states:

- Character cell starts at screenLineAreaStart + symbolX * 4 (4 t-states per cell)
- Latch happens at offset +3 t-states into the cell (HC B = cell_start + 3)
- The latch is 1 t-state before the shift register reloads at HC position 0 of
  the next cell

This offset should be added to the LUT generation in `CreateTstateLUT()`.

### 5.4 Interaction with ScreenHQ mode

The `ScreenHQ` feature toggle currently controls whether per-t-state rendering
happens at all. The attribute latching fix should only apply when ScreenHQ is ON
(per-t-state mode). When ScreenHQ is OFF (batch 8-pixel mode), attributes are
already read once per cell, which matches hardware behavior for standard display
but cannot do any multicolor at all.
