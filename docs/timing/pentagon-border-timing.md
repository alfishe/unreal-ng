# Pentagon Border Timing

This document explains the border-paper synchronization timing for Pentagon ULA emulation.

## Problem

Demos like "Across The Edge" by Demarche synchronize border color changes with paper content. Without proper timing compensation, border effects appear 2 pixels ahead of paper content.

## Root Cause Analysis

### INT Timing Quantization

The Z80 HALT instruction executes in 4T cycles. When waiting for INT in HALT state:

```
HALT cycle:  [0] [1] [2] [3] [0] [1] [2] [3] ...
             └─────4T─────┘  └─────4T─────┘
```

INT is only serviced at instruction boundaries, so even if INT fires mid-cycle, the CPU won't respond until the current 4T group completes. This means adjusting `intstart` in the INI only produces visible changes at 4T (8 pixel) increments.

### 2-Pixel Offset

The 2-pixel border-ahead-of-paper offset requires 1T precision, which INT timing cannot provide due to HALT quantization.

## Solution

### Two-Part Fix

1. **INT Timing** (`intstart=71635`): Provides the correct 17989T INT-to-paper distance
2. **Border Delay** (`ScreenZX::SetBorderColor`): Adds 1T delay for Pentagon-class ULAs

### INT-to-Paper Distance Calculation

```
Frame duration:     71680 T-states
Paper start:        17944 T-states (line 80, pixel 48)
Target distance:    17989 T-states (real Pentagon calibration)

INT check uses:     cpu.t > int_start (strict greater-than)
INT fires at:       int_start + 1

Calculation:
  (71680 - (int_start + 1)) + 17944 = 17989
  71680 - int_start - 1 + 17944 = 17989
  int_start = 71635
```

### Border Color Delay

In `ScreenZX::SetBorderColor()`, Pentagon-class models render one additional T-state with the OLD border color before applying the new color:

```cpp
// Pentagon-class ULAs: render one additional T-state (2 pixels) with OLD color
if (_mode == M_PENTAGON128K || _mode == M_PMC || _mode == M_P16 ||
    _mode == M_P384 || _mode == M_PHR)
{
    uint32_t nextT = _prevTstate + 1;
    if (nextT < _rasterState.maxFrameTiming)
    {
        Draw(nextT);      // Render T+1 with OLD color
        _prevTstate = nextT;
    }
}
_borderColor = color & 0b0000'0111;  // Now apply new color
```

## Affected Models

All Pentagon variants use `M_PENTAGON128K` video mode:

| RAM Size | Memory Model | Video Mode | Config Folder |
|----------|--------------|------------|---------------|
| 128K | MM_PENTAGON | M_PENTAGON128K | pentagon128k |
| 256K | MM_PENTAGON | M_PENTAGON128K | pentagon128k |
| 512K | MM_PENTAGON | M_PENTAGON128K | pentagon512k |
| 1024K | MM_PENTAGON | M_PENTAGON128K | pentagon512k |

## Border Update Rates

| Model | Update Rate | Implementation |
|-------|-------------|----------------|
| Pentagon | 1T (immediate) | Direct `_borderColor` read in `Draw()` |
| ZX-48K/128K | 4T (latched) | Border latched at 8-HC boundaries |

## Configuration

### INI Settings

```ini
[ULA]
intstart=71635  ; INT fires at 71636 (>71635), 17989T to paper
intlen=32       ; INT pulse duration
```

### Code Locations

- `core/src/emulator/config.cpp`: Default `intstart` values
- `core/src/emulator/video/zx/screenzx.cpp`: `SetBorderColor()` with 1T delay
- `core/src/emulator/cpu/z80.cpp`: INT check logic (`cpu.t > int_start`)

## Testing

Use "Across The Edge" by Demarche to verify border-paper alignment. The colored border stripes should align precisely with paper content edges.

## References

- MiSTer FPGA Pentagon ULA implementation (ula.sv)
- Original Unreal Speccy conf.paper calibration (17989T)
- [ZXMAK2 emulator](https://github.com/zxmak/zxmak2) border timing tests
