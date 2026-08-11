# 02 — Border Color Update Granularity Analysis

**Date:** 2026-08-09
**HDL reference:** `ula.sv` lines 175-185
**Emulator reference:** `screenzx.cpp` lines 672-677, `screen.h` line 416, `config.cpp` line 283

---

## 1. How real hardware handles border color

The border color is written by the Z80 via `OUT (#FE), A` (bit 0-2 = border color).
The ULA then displays this color in the border region. But the ULA does **not**
re-read the border register every pixel — it has a latch with specific update timing.

## 2. MiSTer HDL border update logic

### 2.1 Default latching (all models)

```verilog
// ula.sv lines 175-178
if(hc_next[2:0] == 4) begin
    SRegister <= VidEN ? bits : 8'd0;
    hiSRegister <= VidEN ? {bits, attr} : 16'd0;
    AttrOut <= tmx_hi ? hiattr : VidEN ? attr : {2'b00,border_color,border_color};
end
```

The `AttrOut` register is loaded when `hc_next[2:0] == 4`, which happens every
8 HC cycles (4 t-states). This is the **4T border update** for ZX-48K/128K.

### 2.2 Pentagon 1T border override

```verilog
// ula.sv line 185
//1T update for border in Pentagon mode
if(!mZX & ((hc_next<12) | (hc_next>267) | (vc>=192)))
    AttrOut <= tmx_hi ? hiattr : {2'b00,border_color,border_color};
```

When `mZX=0` (Pentagon), the border color is **re-read every single HC cycle**
(every t-state) as long as the current pixel position is in the border region:
- `hc_next < 12` (left border + hblank area)
- `hc_next > 267` (right border + hsync area)
- `vc >= 192` (top/bottom border lines)

This means Pentagon border changes take effect **immediately** at the next pixel.
This is why Pentagon demos can do pixel-precise border effects ("raster bars")
that are impossible on genuine ZX hardware.

### 2.3 Summary of MiSTer border update rates

| Model    | Border update rate | Granularity |
|----------|--------------------|-------------|
| Pentagon | Every t-state      | 1T (2 px)   |
| ZX-48K   | Every 4 t-states   | 4T (8 px)   |
| ZX-128K  | Every 4 t-states   | 4T (8 px)   |

## 3. Emulator border update logic

### 3.1 Current implementation

The emulator's `Draw()` method is called once per t-state. For border pixels:

```cpp
// screenzx.cpp lines 672-677
else
{
    // Render border (2 pixels)
    uint32_t borderColor = _rgbaColors[_borderColor];
    framebufferARGB[framebufferOffset] = borderColor;
    framebufferARGB[framebufferOffset + 1] = borderColor;
}
```

`_borderColor` is set by the port decoder on every `OUT (#FE)`:

```cpp
// portdecoder.cpp line 318
_screen->SetBorderColor(borderColor);

// screen.cpp lines 427-430
void Screen::SetBorderColor(uint8_t color)
{
    _borderColor = color & 0b0000'0111;
}
```

This means the emulator reads the **current** border color at every t-state for
**all models**. There is no model-dependent latching delay.

### 3.2 The dead `border_4T` config flag

The emulator has a config parameter `border_4T` that is loaded from INI:

```cpp
// config.cpp line 283
config.border_4T = (unsigned)inimanager.GetLongValue(ula, "4TBorder", 0);

// platform.h line 416
uint8_t even_M1, border_4T;
```

This flag is **never read** by any rendering code. A grep for `border_4T` across
the entire `core/src` directory returns only the declaration and config load —
the field is dead code.

## 4. Impact on visual accuracy

### 4.1 Pentagon — correct

The emulator's per-t-state border reading matches the Pentagon's 1T hardware
behavior. Border effects (raster bars, color cycling) will render correctly. ✅

### 4.2 ZX-48K / ZX-128K — over-precise

The emulator allows border color changes to take effect at 1T granularity on
models where hardware only updates at 4T boundaries. This means:

- A border color change via `OUT (#FE)` that occurs between HC boundaries will
  show up immediately in the emulator, but would be delayed until the next 4T
  boundary on real hardware.
- Demos that do precise timing-based border effects may look slightly different.
- The difference is most visible when the CPU changes border color rapidly during
  a scanline (e.g., in the HBlank area).

## 5. Required fix

### 5.1 Add border update granularity to RasterState

```cpp
// screen.h - add to RasterState struct
uint8_t borderUpdateGranularity;  // 1 for Pentagon, 4 for ZX-48K/128K
```

### 5.2 Set it per video mode

In `Screen::SetVideoMode()`, after raster state calculation:

```cpp
// Pentagon modes: 1T granularity
// ZX-48K/128K modes: 4T granularity
switch (mode) {
    case M_PENTAGON128K:
    case M_PMC:
        _rasterState.borderUpdateGranularity = 1;
        break;
    case M_ZX48:
    case M_ZX128:
    default:
        _rasterState.borderUpdateGranularity = 4;
        break;
}
```

### 5.3 Latch border color in Draw()

Add a `_latchedBorderColor` field to `ScreenZX`. In `Draw()`, for border pixels,
only update the latched color at the correct boundary:

```cpp
// Only re-read border register at 4T boundaries for ZX models
uint32_t tInLine = tstate % _rasterState.tstatesPerLine;
if (_rasterState.borderUpdateGranularity == 1 ||
    (tInLine % _rasterState.borderUpdateGranularity) == 0)
{
    _latchedBorderColor = _rgbaColors[_borderColor];
}
uint32_t borderColor = _latchedBorderColor;
```

Alternatively, wire the existing `config.border_4T` flag to toggle this behavior,
defaulting it based on model.
