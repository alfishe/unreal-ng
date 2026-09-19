# Architectural Technical Design: Pentagon 1024K 16-Color Screen Mode Extension

**Target Model:** Pentagon 1024K (`MM_PENTAGON`, `PortDecoder_Pentagon1024`)  
**Feature:** Alone Coder 16-Color Screen Mode v1.1 (`16col` / `Pentagon 16C` / `M_P16`)  
**Date:** 2026-09-18  
**Status:** In Progress / Proposal  
**Author / Specs:** Based on Alone Coder (30.10.2005, *Info Guide #08 / #09*) & Unreal-NG Core Architecture  

---

## 1. Executive Summary & Historical Background

The **Pentagon 16-Color Screen Mode** (popularly known as `16col` or `Pentagon 16C`) is a hardware modification for Pentagon computers designed by Alone Coder in October 2005 (*Info Guide #08*, with minor video playback corrections in *Info Guide #09*). Unlike standard ZX Spectrum attribute-bound graphics (2 colors per 8x8 block), this mode provides **individual 16-color palette index selection for every single pixel** at 256×192 resolution, giving true per-pixel RGBI graphics.

### Key Characteristics
- **Control Register:** Enabled via Bit 0 of Port `#EFF7` (`eff7bit0 = 1`).
- **Resolution:** 256×192 pixels, 16 colors per pixel (4 bpp, EGA/ATM color bit encoding `%IiGRBgrb`).
- **Memory Footprint:** 24,576 bytes per screen (4 planes of 6,144 bytes each).
- **Buffer Distribution:**
  - **Screen 0 (Main):** RAM Pages 4 and 5 mapped across 4 virtual planes at `#C000`, `#4000`, `#E000`, and `#6000`.
  - **Screen 1 (Shadow):** RAM Pages 6 and 7 mapped across `#C000`, `#4000`, `#E000`, and `#6000`.
- **v1.1 Defect Fix:** Incorporates the D10 TM2 border gate circuit to eliminate CPU bus noise on the left 4 pixels and horizontal address offset on the right 8 pixels, yielding a clean 248×192 active display area (or 256×192 framebuffer view).

While `ScreenZX::DrawAlcoMode` in `core/src/emulator/video/zx/screenzx.cpp` already contains low-level pixel fetch logic for `M_P16`, full formal integration into the **Pentagon 1024K** machine definition requires completing port decoding, state reporting, video mode detection, automation surface parity (WebAPI / MCP / CLI / Lua / Python), and snapshot / TTD persistence.

---

## 2. Hardware Schematics & Signal Analysis (v1.1 Specification)

The modification builds upon hardware multicolor timing circuits and multiplexes RAM address / attribute strobes using a single additional IC (1533KP11 / 74ALS157).

### 2.1 Signal Circuits Breakdown

| Circuit # | Signal Name | Hardware Origin / Logic Gate | Function |
|:---|:---|:---|:---|
| **Circuit 1** | `/BUSRQ` | Wired-OR (`D10/8` OR `/eff7b0`) to Z80 `/BUSRQ` | Synchronizes video controller DMA requests during pixel fetch cycles. |
| **Circuit 2** | `A13V` (D17/11) | Wired-AND of `eff7b0` AND 7/8 MHz clock (`D3/2`) | Multiplexer video address line 13. Toggles plane addresses between `#0000` and `#2000`. |
| **Circuit 3** | `A14V` / `P0V` (D17/14) | Wired-OR of `/eff7b0` AND 7/4 MHz clock (`D3/3`) | Multiplexer video address line 14. Selects odd RAM page pairs (e.g. Page 4 vs Page 5). |
| **Circuit 4** | Flashed Mask (D6/11) | Commutated via 74ALS157 to 3.5 MHz (`D1/8`) on `eff7b0` | Disables standard Spectrum FLASH blinking logic and shifts pixel clocking to 3.5 MHz. |
| **Circuit 5** | 2nd Bright (D47/11) | Commutated via 74ALS157 to Attribute Bit 7 (`D7/12`) | Routes bit 7 of the attribute fetch to drive the secondary pixel brightness signal (`Yr`). |
| **Circuit 6** | Attribute Read Strobe | Primary buffer (D37/11): 3.5 MHz (`D45/2`); Secondary buffer (D40/11): 3.5 MHz shifted 90° (`D1/9`) | Strobes primary and secondary pixel buffers on alternating clock phases. |
| **Circuit 7** | Mode Addressing Switch | Kept identical to "Attribute per byte" schematic | Switches address decoder between ATTR and MASK modes via `eff7bit0`. |

### 2.2 v1.0 Defect Correction (v1.1 Circuit Modification)

In the initial v1.0 circuit, two hardware defects were observed on genuine Pentagon hardware:
1. **Right Edge Shift:** The rightmost 8 pixels of every scanline were fetched from the beginning of the line + 8 bytes.
2. **Left Edge Noise:** The leftmost 4 pixels were corrupted by Z80 CPU bus activity.

**Alone Coder's v1.1 Fix (30.10.2005):**
Uses a D-flip-flop (D10 TM2 pin 12/8) gated with `/eff7b0` into Circuit 1 (`/BUSRQ`):

```
                     D10 (TM2)
                  ┌─────────┐
Border Address 12 │         │
 ─────────┬───────┤ D     _ │   Graphic Border
          │       │       Q o──────────┬── x ────┬─────
          │       └─────────┘          │   ┌──┐  │
          │   ┌──┐                ┌──┐ └───┤1 │  │
          └───┤1 o──┬─────────────┤& ├─────┤  ├──┘
              └──┘  │  eff7b0 ────┤  │     └──┘
                    │             └──┘
                    │
                    └─> To Circuit 1 (/BUSRQ instead of D10/8)
```

**Result:** Clips 3 pixels on the left margin and 5 pixels on the right margin. The remaining **248×192** central graphics area is completely free of artifacts. In emulation, the complete 256×192 plane is decoded cleanly without bus contention, while preserving true Pentagon 71,680 T-state timing.

---

## 3. Screen Memory Layout & Pixel Bit Packing

### 3.1 Pixel Bit Structure (`%IiGRBgrb`)

Each byte in memory specifies the 4-bit palette indices for **two horizontal adjacent pixels** (left pixel and right pixel), using the ATM Turbo EGA bit packing standard:

| Bit | Symbol | Description |
|:---:|:---:|:---|
| **D7** | `Yr` | Right Pixel Intensity / Brightness (`Bright`) |
| **D6** | `Yl` | Left Pixel Intensity / Brightness (`Bright`) |
| **D5** | `Gr` | Right Pixel Green |
| **D4** | `Rr` | Right Pixel Red |
| **D3** | `Br` | Right Pixel Blue |
| **D2** | `Gl` | Left Pixel Green |
| **D1** | `Rl` | Left Pixel Red |
| **D0** | `Bl` | Left Pixel Blue |

#### Pixel 4-Bit Palette Index Extraction
- **Left Pixel Index (`I_left`):** `((byte & 0x40) >> 3) | (byte & 0x07)` -> `{Yl, Gl, Rl, Bl}`
- **Right Pixel Index (`I_right`):** `((byte & 0x80) >> 4) | ((byte & 0x38) >> 3)` -> `{Yr, Gr, Rr, Br}`

### 3.2 Four-Plane Memory Addressing

A single scanline of 256 pixels consists of 32 character blocks (8 pixels wide each). Each 8-pixel horizontal segment is divided into 4 pixel pairs across 4 memory planes:

```
Pixel Column Index:  [ 0  1 ]  [ 2  3 ]  [ 4  5 ]  [ 6  7 ]
Plane Location:       Plane 0    Plane 1    Plane 2    Plane 3
Buffer Address:       #C000+    #4000+     #E000+     #6000+
```

#### Screen 0 (Main Screen — Port `#7FFD` Bit 3 = 0)
- **Plane 0 (Pixels 0,1):** RAM Page 4 at CPU virtual address `#C000` (Offset `#0000..#17FF`)
- **Plane 1 (Pixels 2,3):** RAM Page 5 at CPU virtual address `#4000` (Offset `#0000..#17FF`)
- **Plane 2 (Pixels 4,5):** RAM Page 4 at CPU virtual address `#E000` (Offset `#2000..#37FF`)
- **Plane 3 (Pixels 6,7):** RAM Page 5 at CPU virtual address `#6000` (Offset `#2000..#37FF`)

#### Screen 1 (Shadow Screen — Port `#7FFD` Bit 3 = 1)
- **Plane 0 (Pixels 0,1):** RAM Page 6 at CPU virtual address `#C000` (Offset `#0000..#17FF`)
- **Plane 1 (Pixels 7,3):** RAM Page 7 at CPU virtual address `#4000` (Offset `#0000..#17FF`)
- **Plane 2 (Pixels 4,5):** RAM Page 6 at CPU virtual address `#E000` (Offset `#2000..#37FF`)
- **Plane 3 (Pixels 6,7):** RAM Page 7 at CPU virtual address `#6000` (Offset `#2000..#37FF`)

#### Interleaving Within a Character Block
For character block 0 (leftmost 8x8 block on scanline 0):
- Pixels (0,1): Byte at RAM Page 4 offset `#0000`
- Pixels (2,3): Byte at RAM Page 5 offset `#0000`
- Pixels (4,5): Byte at RAM Page 4 offset `#2000`
- Pixels (6,7): Byte at RAM Page 5 offset `#2000`

### 3.3 Cross-Reference: Other Emulator Implementations

Implementation verified against Unreal Speccy, ZXMAK2, and Xpeccy-Plus (2026-09-18).

#### Pixel Bit Extraction (All Three Agree)

| Emulator | Left Pixel Formula | Right Pixel Formula |
|:---|:---|:---|
| **Unreal-NG** (§3.1) | `((byte & 0x40) >> 3) \| (byte & 0x07)` | `((byte & 0x80) >> 4) \| ((byte & 0x38) >> 3)` |
| **ZXMAK2** `EvoA16Renderer.cs:229-230` | `(at & 7) \| ((at & 0x40) >> 3)` | `((at >> 3) & 7) \| ((at & 0x80) >> 4)` |
| **Xpeccy-Plus** `video.c:912,927` | `(scrbyte & 7) \| ((scrbyte & 0x40) >> 3)` | `((scrbyte & 0x38)>>3) \| ((scrbyte & 0x80)>>4)` |

All three emulators use identical bit extraction logic.

#### Plane Addressing (Screen 0 = Pages 4 & 5)

| Pixel X & 7 | Xpeccy-Plus | ZXMAK2 | Unreal Speccy | Unreal-NG |
|:---:|:---|:---|:---|:---|
| 0,1 | `vidPage ^ 1` @ `+0x0000` | Page0 @ `+0x0000` | `t.alco[y][x].s` | Page 4 @ `+0x0000` |
| 2,3 | `vidPage` @ `+0x0000` | Page1 @ `+0x0000` | `t.alco[y][x].a` | Page 5 @ `+0x0000` |
| 4,5 | `vidPage ^ 1` @ `+0x2000` | Page0 @ `+0x2000` | offset +0x2000 | Page 4 @ `+0x2000` |
| 6,7 | `vidPage` @ `+0x2000` | Page1 @ `+0x2000` | offset +0x2000 | Page 5 @ `+0x2000` |

**Xpeccy-Plus convention:** `vidPage = 5` (screen 0) → `vidPage ^ 1 = 4`. Matches Unreal-NG's Page 4/5 assignment.

#### Screen Selection via #7FFD Bit 3

| Emulator | Screen 0 (bit 3 = 0) | Screen 1 (bit 3 = 1) |
|:---|:---|:---|
| **Unreal Speccy** | `ofs = 0` | `ofs = (8 << 12) = 0x8000` (pages 6/7) |
| **Xpeccy-Plus** | `vidPage = 5` | `vidPage = 7` |
| **ZXMAK2** | `MemoryPage0/1` set via `UpdateVideoPage()` | Swaps to pages 6/7 |
| **Unreal-NG** (§5.2) | `videoPage = 5`, pages 4 & 5 | `videoPage = 7`, pages 6 & 7 |

All emulators agree on screen buffer selection.

#### Source Files Reference

| Emulator | 16col Rendering | Port EFF7 Handler |
|:---|:---|:---|
| **Unreal Speccy** | `draw_384.cpp:draw_alco_256()` | `emulkeys.cpp` (inline toggle) |
| **ZXMAK2** | `Atm/EvoA16Renderer.cs` | `Pentagon/MemoryPentagon1024.cs:149` |
| **Xpeccy-Plus** | `video/video.c:vidDrawAlco()` | `hardware/pent1024.c:p1mOutEFF7()` |

#### Port `#EFF7` Decoding Mask Variations

> [!NOTE]
> Emulators use slightly different port decoding masks. All respond to `OUT #EFF7,n` but differ on partial decodes:

| Emulator | Mask | Match | Partial Decode |
|:---|:---:|:---:|:---|
| **Unreal-NG** | `0x00FF` | `0x00F7` | Any `xxxx'xxxx'1111'0111` (low byte = 0xF7, A3=0) |
| **ZXMAK2** | `0xF008` | `0xE000` | `1110'xxxx'xxxx'x000` |
| **Xpeccy-Plus** | `0xF008` | `0xEFF7` | Full `0xEFF7` address |

ZXMAK2 and Xpeccy-Plus use the same mask but different match values. Unreal-NG's looser mask is consistent with real Pentagon hardware where only low address lines are fully decoded. No known software depends on the exact partial decode behavior for `#EFF7`.

---

## 4. Port `#EFF7` Decoder & State Management

In [`PortDecoder_Pentagon1024`](../../../core/src/emulator/ports/models/portdecoder_pentagon1024.cpp), Port `#EFF7` controls extended machine features:

### Port `#EFF7` Bit Allocations

| Bit | Flag | Description |
|:---:|:---|:---|
| **D0** | `EFF7_4BPP` (`0x01`) | **16-Color Mode Enable** (`1` = 16col active, `0` = standard mode) |
| **D1** | `EFF7_512` (`0x02`) | 512×192 High-Resolution Mode |
| **D2** | `EFF7_EXTMEM` (`0x04`) | Extended RAM Lock (`0` = 1MB enabled, `1` = locked to 128KB) |
| **D3** | Reserved | Unused |
| **D4** | `EFF7_GIGASCR` (`0x10`) | GigaScreen Hardware Interlace Mode |
| **D5** | `EFF7_HWMC` (`0x20`) | Hardware Multicolor Mode |
| **D6** | Reserved | Unused |
| **D7** | `EFF7_GLUK` (`0x80`) | Gluk CMOS RAM / Service Port Enable |

### Decoding Logic Updates

When `Port_EFF7_Out` receives a write:
1. `_context->emulatorState.pEFF7` is updated.
2. If `EFF7_4BPP` (bit 0) toggles:
   - Call `_screen->OnPortWriteEFF7()` to notify `Screen` of state change.
   - Log state transition via `MLOGDEBUG`.

#### Implementation (portdecoder_pentagon1024.cpp:94-122)

Following the ATM3 pattern (`PortDecoder_ATM3::Port_EFF7_Out`, lines 497-500), the port decoder calls `InitRaster()` directly when video mode bits change:

```cpp
void PortDecoder_Pentagon1024::Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = _context->emulatorState;
    uint8_t prevValue = state.pEFF7;
    state.pEFF7 = value;

    // Bit 2 controls extended memory access - remap if it changed
    bool extendedMemoryChanged = ((prevValue ^ value) & 0x04) != 0;
    if (extendedMemoryChanged && _memory)
    {
        switchRAMPage(state.p7FFD);
        _memory->UpdateZ80Banks();
    }

    // Video mode bits (0, 1, 5, 6) - trigger mode re-detection via InitRaster
    // Pattern follows ATM3's Port_EFF7_Out (portdecoder_atm3.cpp:497-500)
    constexpr uint8_t VIDEO_MODE_BITS = EFF7_4BPP | EFF7_512 | EFF7_HWMC | EFF7_384;
    bool videoModeChanged = ((prevValue ^ value) & VIDEO_MODE_BITS) != 0;
    if (videoModeChanged && _context->pScreen)
    {
        _context->pScreen->InitRaster();
        MLOGDEBUG("Port_EFF7_Out: video bits changed (0x%02X -> 0x%02X), InitRaster() called",
                  prevValue, value);
    }

    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0xEFF7, port, value, pc, Dump_EFF7_value(value).c_str()));
    }
}
```

> [!NOTE]
> The `InitRaster()` approach is preferred over a new `OnPortWriteEFF7()` method because:
> 1. It follows the established ATM3 pattern (proven, testable via `atm_video_modes_suite_test.cpp:486-510`)
> 2. `InitRaster()` already handles mode re-detection from `pEFF7` state via `DetectModePentagon()`
> 3. No new API surface to maintain

---

## 5. Emulator Video Engine Integration (`Screen` & `ScreenZX`)

### 5.1 Video Mode Detection (`Screen::DetectModePentagon`)

[`Screen::DetectModePentagon`](../../../core/src/emulator/video/screen.cpp#L289-L318) evaluates `state.pEFF7`:

```cpp
Screen::ModeSelection Screen::DetectModePentagon(const EmulatorState& state) const
{
    VideoModeEnum mode = M_PENTAGON128K;
    RasterModeEnum rasterMode = R_256_192;

    if (_overscanForced)
    {
        mode = M_P384;
        rasterMode = R_384_304;
    }

    const uint8_t alco = state.pEFF7 & (EFF7_4BPP | EFF7_512 | EFF7_384 | EFF7_HWMC);
    if (alco != 0)
    {
        switch (alco)
        {
            case EFF7_4BPP: mode = M_P16; break;  // Alone Coder 16col
            case EFF7_HWMC: mode = M_PMC; break;  // Hardware Multicolor
            case EFF7_512:  mode = M_PHR; break;  // 512x192
            case EFF7_384:  mode = M_P384; rasterMode = R_384_304; break;
            default:        mode = M_NUL;  break;
        }
    }

    return { mode, rasterMode };
}
```

### 5.2 Pixel Rendering Pipeline (`ScreenZX::DrawAlcoMode`)

In [`ScreenZX::DrawAlcoMode`](../../../core/src/emulator/video/zx/screenzx.cpp#L1260-L1318):
- When `_mode == M_P16`:
  - `videoPage = (state.p7FFD & 0x08) ? 7 : 5`
  - RAM Page A address = `RAMPageAddress(videoPage ^ 1)` (Page 4 for screen 0, Page 6 for screen 1)
  - RAM Page B address = `RAMPageAddress(videoPage)` (Page 5 for screen 0, Page 7 for screen 1)
  - `q = (zxX >> 1) & 3` (plane index 0..3)
  - `plane = (q & 1) ? pageB : pageA`
  - Byte address offset = `((q >> 1) << 13) + screenOffset + (zxX >> 3)`
  - Palette lookup converts indices to 32-bit ARGB values.

### 5.3 Batch Rendering Consideration (`ScreenHQ=OFF`)

As documented in `ScreenZX::RenderFrameBatch()` ([screenzx.cpp:1334](../../../core/src/emulator/video/zx/screenzx.cpp#L1334)):
```cpp
if (_mode == M_ATM16 || _mode == M_ATMHR || _mode == M_ATMTX || _mode == M_ATMTL ||
    _mode == M_P16 || _mode == M_PMC)
{
    // Extended modes bypass standard ZX batch fetch; per-t-state renderer handles the frame
    RenderFramePerTstate();
}
```
This ensures fallback to scanline/t-state accuracy when running with `ScreenHQ=OFF`.

---

## 6. Automation, Telemetry & WebAPI / MCP Surface Parity

To ensure full compliance with Unreal-NG automation standards, Pentagon 16-color mode state must be fully introspectable across all control interfaces.

### 6.1 State API Extensions

#### `GET /api/v1/emulator/{id}/state/screen/mode`
Response JSON format:
```json
{
  "mode": "M_P16",
  "name": "Pentagon 16c",
  "resolution": "256x192",
  "bpp": 4,
  "colors": 16,
  "eff7_active": true,
  "active_screen": 0
}
```

#### `GET /api/v1/emulator/{id}/state/paging`
Response JSON format inclusion:
```json
{
  "model": "PENTAGON1024",
  "port_7ffd": "0x10",
  "port_eff7": "0x01",
  "eff7_flags": {
    "16col_enabled": true,
    "512_enabled": false,
    "extmem_locked": false,
    "gigascreen_enabled": false,
    "hwmc_enabled": false
  }
}
```

### 6.2 MCP & CLI Tool Integration
- **MCP Tool `inspect_state` (`video` aspect):** Reports `video_mode: "M_P16"`, `eff7_16col: true`.
- **CLI Command `state screen`:** Displays `Screen Mode: M_P16 (Pentagon 16c 256x192 16col)`.

### 6.3 Implementation Files (Completed)

| File | Change |
|:---|:---|
| `core/automation/webapi/src/api/state_screen_api.cpp` | `getStateScreenMode()` reports actual video mode name, resolution, bpp, colors, eff7 state |
| `core/automation/webapi/src/api/state_memory_api.cpp` | `eff7_flags` object added to paging response for Pentagon models |
| `core/automation/mcp/src/mcp-tools.cpp` | `video` aspect calls `/state/screen/mode` endpoint |
| `core/automation/cli/src/commands/cli-processor-state.cpp` | `HandleStateScreenMode()` shows video mode details with EFF7 breakdown |
| `core/automation/lua/src/emulator/lua_emulator.h` | `screen_video_state()` returns table with video_mode, bpp, colors, eff7 state |
| `core/automation/python/src/emulator/python_emulator.h` | `screen_video_state()` returns dict with same fields |

---

## 7. Snapshot & Time-Travel Debugging (TTD) Persistence

### 7.1 Snapshot Formats & Persistence Analysis

> [!WARNING]
> Neither official `.sna` (standard 31-byte header), official `.z80` (v1–v3 86-byte header), nor official `.szx` format specifications include an `#EFF7` field. Any persistence of `#EFF7` outside of Unreal-NG's native TTD engine relies on non-standard snapshot extensions.

1. **Unreal-NG Native Engine Persistence (Verified Ground Truth):**
   - [`TimeTravelManager`](../../../core/src/debugger/ttd/timetravelmanager.cpp#L1189) captures `state.pEFF7` on every frame boundary in the internal delta state structure (`EmulatorState`). Replaying frames or seeking in TTD restores `pEFF7` with 100% fidelity across 16col mode transitions.
2. **External Snapshot Interchange Limitations:**
   - **Standard `.sna` / `.z80`:** Loading standard `.sna` or `.z80` snapshots resets `#EFF7` to `0x00` (default mode).
   - **Legacy UnrealSpeccy Extensions:** Legacy UnrealSpeccy appended trailing un-spec'd bytes after the RAM dump in `.sna` / `.z80` to persist `pEFF7`.
   - **SZX Custom Extension Block:** If `.szx` snapshot saving is implemented for Pentagon 1024, a custom chunk `EFF7` (4 bytes: `pEFF7`, `p7FFD`, `pDFFD`, reserved) is required.

### 7.2 TTD Frame Recording
`TimeTravelManager` captures `pEFF7` on every frame boundary in the delta journal ([timetravelmanager.cpp:1189](../../../core/src/debugger/ttd/timetravelmanager.cpp#L1189)). Replaying frames restores `state.pEFF7` seamlessly, ensuring reverse debugging across 16col mode transitions.

---

## 8. Verification & Test Plan

### 8.1 Unit Tests (`core/tests/emulator/ports/`)

Extend existing `portdecoder_pentagon1024_test.cpp`:

```cpp
/// Verify EFF7 bit 0 enables 16-color mode
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_Bit0_Enables16ColorMode)
{
    _decoder = new PortDecoder_Pentagon1024(_context);
    _decoder->Reset();

    // Write 0x01 to #EFF7
    _decoder->WritePort(0xEFF7, 0x01, 0x0000);
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x01);
    EXPECT_TRUE(_context->emulatorState.pEFF7 & EFF7_4BPP);
}

/// Verify EFF7 cleared on reset
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_ClearedOnReset)
{
    _decoder = new PortDecoder_Pentagon1024(_context);
    _context->emulatorState.pEFF7 = 0x01;
    _decoder->Reset();
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x00);
}

/// Verify screen notification called on video mode bit change
TEST_F(PortDecoder_Pentagon1024_Test, EFF7_VideoModeChange_NotifiesScreen)
{
    // Requires mock Screen to verify OnPortWriteEFF7() is called
    MockScreen mockScreen;
    _decoder = new PortDecoder_Pentagon1024(_context);
    _decoder->SetScreen(&mockScreen);

    EXPECT_CALL(mockScreen, OnPortWriteEFF7(0x01)).Times(1);
    _decoder->WritePort(0xEFF7, 0x01, 0x0000);
}
```

### 8.2 Video Mode Detection Tests (`core/tests/emulator/video/`)

New file: `pentagon_16col_mode_test.cpp`

```cpp
/// Verify DetectModePentagon returns M_P16 when EFF7 bit 0 set
TEST_F(Pentagon_16col_VideoMode_Test, DetectMode_Returns_M_P16)
{
    _context->emulatorState.pEFF7 = EFF7_4BPP;
    auto selection = _screen->DetectModePentagon(_context->emulatorState);
    EXPECT_EQ(selection.mode, M_P16);
    EXPECT_EQ(selection.rasterMode, R_256_192);
}

/// Verify mode priority: 16col takes precedence over standard
TEST_F(Pentagon_16col_VideoMode_Test, Mode_16col_Overrides_Standard)
{
    _context->emulatorState.pEFF7 = 0x00;
    auto sel1 = _screen->DetectModePentagon(_context->emulatorState);
    EXPECT_EQ(sel1.mode, M_PENTAGON128K);

    _context->emulatorState.pEFF7 = EFF7_4BPP;
    auto sel2 = _screen->DetectModePentagon(_context->emulatorState);
    EXPECT_EQ(sel2.mode, M_P16);
}
```

### 8.3 Rendering Accuracy Verification

New file: `pentagon_16col_render_test.cpp`

```cpp
/// Verify pixel extraction from %IiGRBgrb byte encoding
TEST_F(Pentagon_16col_Render_Test, PixelExtraction_IiGRBgrb)
{
    // Byte 0b11'101'010 = I=1 i=1 Gr=1 Rr=0 Br=1 Gl=0 Rl=1 Bl=0
    uint8_t testByte = 0xEA;
    
    // Left pixel: bits 6,2,1,0 -> Yl=1, Gl=0, Rl=1, Bl=0 -> index 0b1010 = 10
    uint8_t leftIdx = ((testByte & 0x40) >> 3) | (testByte & 0x07);
    EXPECT_EQ(leftIdx, 0x0A);  // Bright green (idx 10)

    // Right pixel: bits 7,5,4,3 -> Yr=1, Gr=1, Rr=0, Br=1 -> index 0b1101 = 13
    uint8_t rightIdx = ((testByte & 0x80) >> 4) | ((testByte & 0x38) >> 3);
    EXPECT_EQ(rightIdx, 0x0D);  // Bright magenta (idx 13)
}

/// Verify plane addressing for character block
TEST_F(Pentagon_16col_Render_Test, PlaneAddressing_Screen0)
{
    // Screen 0: Pages 4 & 5
    // Plane 0 (pixels 0,1): Page 4 @ 0x0000
    // Plane 1 (pixels 2,3): Page 5 @ 0x0000
    // Plane 2 (pixels 4,5): Page 4 @ 0x2000
    // Plane 3 (pixels 6,7): Page 5 @ 0x2000

    for (int q = 0; q < 4; q++)
    {
        uint8_t expectedPage = (q & 1) ? 5 : 4;
        uint16_t expectedOffset = (q >> 1) << 13;  // 0x0000 or 0x2000
        
        // Verify against DrawAlcoMode addressing
        EXPECT_EQ(GetPlaneAddress(0, q), RAMPageAddress(expectedPage) + expectedOffset);
    }
}
```

### 8.4 Integration Tests

```cpp
/// End-to-end: write test pattern, capture frame, verify colors
TEST_F(Pentagon_16col_Integration_Test, RenderTestPattern_VerifyCapture)
{
    // 1. Configure Pentagon 1024 with 16col mode
    _context->config.mem_model = MM_PENTAGON;
    _context->config.ramsize = 1024;
    _decoder->WritePort(0xEFF7, EFF7_4BPP, 0);

    // 2. Write test pattern to all 4 planes (Pages 4 & 5)
    WriteTestPattern16col(_context->pMemory);

    // 3. Render one frame
    _emulator->RunFrame();

    // 4. Capture and verify
    auto capture = ScreenCapture::CaptureARGB(_screen);
    VerifyTestPatternColors(capture, ATM_PALETTE_RGBI);
}
```

### 8.5 Quality Checklist
- [ ] Zero compiler warnings (`-Werror`).
- [ ] Build passes via `ninja -C cmake-build-release`.
- [ ] Tests pass via `./cmake-build-release/bin/core-tests --gtest_filter=*Pentagon_16col*`.
- [ ] All file cross-references valid and formatted with markdown file links.
- [ ] Screen notification wired (§4.1).
- [x] API endpoints report 16col state (§6.3) — WebAPI, MCP, CLI, Lua, Python complete.
