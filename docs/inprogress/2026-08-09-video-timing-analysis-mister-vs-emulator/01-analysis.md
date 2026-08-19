# 01 — Comparative Analysis: MiSTer ULA HDL vs Emulator Timing

**Date:** 2026-08-09
**Reference HDL:** `ZX-Spectrum_MISTer/rtl/ula.sv` (327 lines)
**Reference top:** `ZX-Spectrum_MISTer/ZX-Spectrum.sv`
**Emulator screen:** `core/src/emulator/video/screen.h`, `screen.cpp`, `video/zx/screenzx.cpp`

---

## 1. MiSTer: three-model timing architecture

The MiSTer ULA module implements three timing profiles selected by two signals.
These are set from OSD bits `status[9:8]` in the top-level file:

| `status[9:8]` | `mZX` | `m128` | Model     |
|---------------|-------|--------|-----------|
| 0             | 1     | 0      | ZX-48K    |
| 1             | 1     | 1      | ZX-128K   |
| 2+ (default)  | 0     | 0      | Pentagon  |

Top-level assignment (`ZX-Spectrum.sv`, lines 749-756):

```verilog
reg mZX, m128;
always @(posedge clk_sys) begin
    case(status[9:8])
        0: {mZX, m128} <= 2'b10;   // ZX-48K
        1: {mZX, m128} <= 2'b11;   // ZX-128K
        default: {mZX, m128} <= 2'b00; // Pentagon
    endcase
end
```

### 1.1 Horizontal counter (HC) parameters

From `ula.sv` lines 100-111:

```verilog
if (hc==((mZX && m128) ? 455 : 447)) begin
    hc_next = 0;
    if (vc == (!mZX ? 319 : m128 ? 310 : 311)) begin
        vc_next = 0;
        FlashCnt_next = FlashCnt + 1'd1;
    end else begin
        vc_next = vc + 1'd1;
    end
end else begin
    hc_next = hc + 1'd1;
end
```

Derived timing constants:

| Parameter        | Pentagon | ZX-48K | ZX-128K |
|------------------|----------|--------|---------|
| HC per line      | 448      | 448    | 456     |
| T-states/line    | 224      | 224    | 228     |
| Lines/frame (vc) | 320      | 312    | 311     |
| Frame t-states   | 71,680   | 69,888 | 70,908  |

> Note: MiSTer uses a 7MHz-pixel clock (`ce_7mp`/`ce_7mn`), where each t-state
> produces two pixel clocks. HC counts pixel clocks, so t-states = HC / 2.

### 1.2 Interrupt (INT) generation

From `ula.sv` lines 169-173:

```verilog
if( mZX && (vc_next == 248) && (hc_next == (m128 ? 8 : 4))) INT <= 1;
if(!mZX && (vc_next == 239) && (hc_next == 326)) INT <= 1;

if(INT) INTCnt <= ((m128 && INTCnt == 71) || (~m128 && INTCnt == 63)) ? 7'd0 : (INTCnt + 1'd1);
if(INTCnt == 0) INT <= 0;
```

| INT parameter    | Pentagon       | ZX-48K         | ZX-128K        |
|------------------|----------------|----------------|----------------|
| Trigger (vc,hc)  | (239, 326)     | (248, 4)       | (248, 8)       |
| INT duration (HC)| 64             | 64             | 72             |
| INT duration (T) | 32             | 32             | 36             |

### 1.3 HSync / HBlank / VSync / VBlank positions

From `ula.sv` lines 132-159. All values are in HC (pixel clocks):

| Signal   | Pentagon    | ZX-48K      | ZX-128K     |
|----------|-------------|-------------|-------------|
| HBlank on/off  | 312 / 416 | 312 / 420 | 312 / 424 |
| HSync on/off   | 336 / 368 | 338 / 370 | 340 / 372 |
| VSync on/off   | 248 / 256 | 240 / 244 | 240 / 244 |
| VBlank on/off  | 236 / 272 | 236 / 264 | 236 / 264 |

---

## 2. Emulator: raster descriptor and config-driven timing

The emulator uses a `RasterDescriptor` table in `screen.h` (lines 311-317):

```cpp
const RasterDescriptor rasterDescriptors[M_MAX] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                  // M_NUL
    {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},   // M_ZX48k
    {352, 288, 256, 192, 48, 48, 456, 64, 32, 8, 16},   // M_ZX128 - Not ready!
    {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_PENTAGON128K
    {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16}   // M_PMC - Not Ready!
};
```

Fields: `{fullFrameWidth, fullFrameHeight, screenWidth, screenHeight,
screenOffsetLeft, screenOffsetTop, pixelsPerLine, hSyncPixels,
hBlankPixels, vSyncLines, vBlankLines}`.

Frame/line values come from the per-model INI files (`data/configs/*/unreal.ini`).

### 2.1 INI timing values

**Pentagon 128K** (`data/configs/pentagon128k/unreal.ini`):
```ini
[ULA]
Frame=71680    ; t-states in frame
Line=224       ; t-states in line
intstart=13
intlen=32
4TBorder=0     ; unused in code
```

**Spectrum 48K** (`data/configs/spectrum48/unreal.ini`):
```ini
[ULA]
Frame=70908    ; WRONG — should be 69888
Line=228       ; WRONG — should be 224
intstart=13
intlen=32
```

**Spectrum 128K** (`data/configs/spectrum128/unreal.ini`):
```ini
[ULA]
Frame=70908    ; correct
Line=228       ; correct
intstart=13
intlen=32      ; should be 36 for ZX-128K INT
```

---

## 3. Side-by-side timing comparison

### 3.1 Frame parameters

| Parameter        | MiSTer Pentagon | Emu Pentagon | Match |
|------------------|-----------------|--------------|-------|
| T-states/line    | 224             | 224          | ✅ |
| Lines/frame      | 320             | 320          | ✅ |
| Frame t-states   | 71,680          | 71,680       | ✅ |
| INT start (T)    | 71619           | 13           | ❌ |
| INT length (T)   | 32              | 32           | ✅ |

> **CRITICAL BUG:** The INT start position is completely wrong. MiSTer fires
> Pentagon INT at vc=239, hc=326 — which maps to emulator t-state **71,619**
> (99.9% through the frame). The emulator uses `intstart=13` (0.02% through
> the frame). This is off by 71,606 t-states and breaks every cycle-precise
> program. See [07-int-signal-timings.md](07-int-signal-timings.md) for full
> analysis and [08-int-correction-implementation.md](08-int-correction-implementation.md)
> for the fix.

### 3.2 Visible area parameters

Both MiSTer and the emulator define the visible screen as 256x192 with
48-pixel left/right borders and 48-pixel top border within a 352-wide x 288-tall
visible area. These match for all three models.

---

## 4. Rendering pipeline comparison

### 4.1 MiSTer hardware pipeline

The ULA has a **fetch → latch → shift** pipeline with deterministic timing:

```
HC cycle within 16-HC character cell:
  HC[3:0]=8 or C : set VRAM address (pixel data row)
  HC[3:0]=A or E : set VRAM address (attribute CAS)
  HC[3:0]=9 or D : latch pixel byte from VRAM
  HC[3:0]=B or F : latch attribute byte from VRAM
  HC[2:0]=4      : load shift register from pixel latch
  Every HC cycle  : shift register advances 1 pixel
```

This means:
- **Pixel/attribute bytes are fetched once per 8-pixel cell** (every 4 t-states)
- **Border color is latched every 4 HC** (2 t-states) for all models
- **Pentagon has an additional 1T border override** (see 02-border-timing.md)

### 4.2 Emulator pipeline

The emulator's `ScreenZX::Draw(tstate)` is called once per t-state and does:

```cpp
const TstateCoordLUT& lut = _tstateLUT[tstate];
if (lut.renderType == RT_SCREEN) {
    uint8_t pixels = *(zxScreen + lut.screenOffset + lut.symbolX);      // READ every T
    uint8_t attributes = *(zxScreen + lut.attrOffset + lut.symbolX);    // READ every T
    // ... write 2 framebuffer pixels
} else {
    uint32_t borderColor = _rgbaColors[_borderColor];                   // READ every T
    // ... write 2 border pixels
}
```

The emulator **re-reads RAM at every t-state**. There is no latch or shift
register emulation. See deep-dive documents for consequences.
