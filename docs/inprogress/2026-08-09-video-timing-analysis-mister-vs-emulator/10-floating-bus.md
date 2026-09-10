# 10 — Floating Bus: ULA Ferranti vs Discrete Logic

**Date:** 2026-08-09 (updated)
**Emulator source:** `core/src/emulator/video/ulacontention.cpp`, `ulacontention.h`
**Test source:** `core/tests/emulator/video/io_contention_test.cpp`

---

## 1. What is the floating bus?

When the Z80 reads a port that no peripheral responds to, no device drives the
data bus. The CPU sees whatever byte the video controller just fetched from
VRAM — the "floating bus".

Demos exploit this for cycle-precise raster sync: reading port `0xFF` returns
the video byte currently being fetched, allowing code to determine its exact
vertical position with T-state accuracy.

**Contention and floating bus are independent features:**
- **Contention** = "the video controller halts the CPU" (clock stretching)
- **Floating bus** = "video data appears on the shared bus"

Pentagon has NO contention but DOES have a floating bus.

---

## 2. Two video controller architectures

The implementation models two fundamentally different video controller designs:

### 2.1 ULA_FERRANTI — ZX-Spectrum 48K, 128K, +2, +3

The Ferranti ULA chip has an internal **8-T-state state machine**. Per cycle:

```
 8T cycle: [Fetch][Fetch][Shift][Shift][Shift][Shift][Shift][Shift]
              │       │
              │       └─ Attribute byte on bus (phase 3 or 5)
              └─ Pixel byte on bus (phase 2 or 4)
```

Only **4 out of 8 T-states** have VRAM data on the bus. During the other 4
(shift phases), the bus is idle and reads as `0xFF`.

```
 phase8 = tInPaper % 8

 phase8   Bus content          cellIndex
 ──────   ──────────────────   ───────────
   0      0xFF (shift)            —
   1      0xFF (shift)            —
   2      Pixel byte (bitmap)   tInPaper/4
   3      Attribute byte        tInPaper/4
   4      Pixel byte (bitmap)   tInPaper/4
   5      Attribute byte        tInPaper/4
   6      0xFF (shift)            —
   7      0xFF (shift)            —
```

**Reference:** ZXMAK2 `SpectrumRenderer.cs`, `CalcTableItem()` lines 430-481:
```
scrPix % 8 == 0 → Shift1AndFetchB2  (pixel on bus)
scrPix % 8 == 1 → Shift1AndFetchA2  (attribute on bus)
scrPix % 8 == 2 → Shift1            (idle, 0xFF)
scrPix % 8 == 3 → Shift1Last        (idle, 0xFF)
scrPix % 8 == 4,5 → Shift2          (idle, 0xFF)
scrPix % 8 == 6 → Shift2AndFetchB1  (pixel on bus)
scrPix % 8 == 7 → Shift2AndFetchA1  (attribute on bus)
```

### 2.2 ULA_DISCRETE_LOGIC — Pentagon, Scorpion, Profi

Soviet clones use **discrete TTL logic** (counters + multiplexers) instead of
a custom ULA chip. The video address counter runs **continuously** — there are
NO shift/dead cycles. Every T-state during the paper area has VRAM data on the
bus.

```
 phase4 = tInPaper % 4

 phase4   Bus content          cellIndex
 ──────   ──────────────────   ───────────
   0      Pixel byte (bitmap)   tInPaper/4
   1      Pixel byte (bitmap)   tInPaper/4
   2      Attribute byte        tInPaper/4
   3      Attribute byte        tInPaper/4
```

Key difference: discrete logic **NEVER returns 0xFF during paper**. The bus
always carries VRAM data (pixel or attribute byte).

---

## 3. Why this matters for demoscene sync

On the Ferranti ULA, an `IN A,(C)` sync instruction can land on a shift phase
(0xFF) or a fetch phase (data). This creates a natural "window" for attribute
detection: only phases 3 and 5 return the attribute byte.

On Pentagon's discrete logic, the attribute byte is on the bus for phases 2-3
of every cell. Since instructions have varying lengths, the sync cycle can
"wander" within the 4T window. Without the phase distinction (pixel vs
attribute), code that checks for a specific attribute value might read the
pixel byte instead, causing sync drift.

This was the root cause of border effect jitter in "Across the Edge" on
Pentagon — the old implementation returned attribute for all 4 T-states per
cell, allowing the sync to wander.

---

## 4. VRAM address calculation

Both architectures use the same ZX Spectrum interleaved VRAM layout.

### 4.1 Attribute address (0x5800-0x5AFF)

```
attrAddr = 0x5800 | (block << 8) | (char_row << 5) | cellIndex

  block    = (y >> 6) & 3   — screen third (top/mid/bottom)
  char_row = (y >> 3) & 7   — character row within third
  cellIndex                  — character column 0-31
```

Verified against ZXMAK2 `CalcTableAddrAt()` (SpectrumRenderer.cs:525-530).

### 4.2 Pixel address (0x4000-0x57FF)

```
pixelAddr = 0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8)
                   | ((y & 0x38) << 2) | cellIndex

  block     = (y >> 6) & 3   — screen third
  pixel_row = y & 7           — scan line within character cell
  char_row  = (y >> 3) & 7   — character row within third
  cellIndex                   — character column 0-31
```

Verified against ZXMAK2 `CalcTableAddrBw()` (SpectrumRenderer.cs:513-518).

---

## 5. Pipeline offset

Both architectures fetch VRAM data **4 T-states ahead** of the electron beam.
The shift register / attribute latch must be loaded before pixels are drawn.

```
fetchAreaStart = screenLineAreaStart - 4
fetchAreaEnd   = screenLineAreaEnd   - 4
```

This means `cellIndex` derived from `tInPaper` is the cell being **fetched**
(ahead of the beam), not the cell currently being **drawn**.

---

## 6. Model comparison matrix

| Feature | ZX-48K | ZX-128K | Pentagon |
|---|---|---|---|
| Video controller | Ferranti ULA | Ferranti ULA | Discrete TTL |
| Fetch architecture | `ULA_FERRANTI` | `ULA_FERRANTI` | `ULA_DISCRETE_LOGIC` |
| Memory contention | Yes ({6,5,4,3,2,1,0,0}) | Yes (+1T even ports) | **No** |
| Floating bus | **Yes** | **Yes** | **Yes** |
| Fetch cycle | 8T (4 fetch + 4 shift) | 8T | 4T continuous |
| Shift/dead phases | Yes (phases 0,1,6,7 = 0xFF) | Yes | **None** |
| Pixel byte on bus | Phases 2,4 | Phases 2,4 | Phases 0,1 |
| Attribute byte on bus | Phases 3,5 | Phases 3,5 | Phases 2,3 |
| Returns 0xFF during paper | Yes (shift phases) | Yes | **Never** |

---

## 7. Reference emulator comparison

| Emulator | Pentagon model | Pixel bytes? | Shift gaps? |
|---|---|---|---|
| **This emulator** | Discrete 4T continuous | Yes | No (Pentagon) / Yes (ZX) |
| ZXMAK2 | Same 8T as ZX (no override) | Yes | Yes (all models) |
| UnrealSpeccy | Simplified (attr only, all 4T) | No | No |
| Xpeccy | Not implemented | N/A | N/A |

ZXMAK2's `UlaPentagon` class does NOT override `ReadFreeBus()` — it uses the
same 8T Ferranti pipeline for Pentagon. This is a known simplification.
Real Pentagon hardware uses discrete logic without shift gaps.

UnrealSpeccy (io.cpp:953-958) returns attribute for ALL 4 T-states per cell
(no pixel/attribute phase distinction, no pixel bytes). Pentagon floating bus
is OFF by default (`conf.portff = 0`).

---

## 8. Implementation

### 8.1 Architecture selection (screen.cpp)

```cpp
switch (mode)
{
    case M_PENTAGON128K:
    case M_PMC:
        _rasterState.fetchType = ULA_DISCRETE_LOGIC;
        break;
    case M_ZX48:
    case M_ZX128:
    default:
        _rasterState.fetchType = ULA_FERRANTI;
        break;
}
_context->pUlaContention->SetFetchType(fetchType);
```

### 8.2 Phase branching (ulacontention.cpp)

```cpp
if (_fetchType == ULA_FERRANTI)
{
    // 8T: only phases 2-5 have data; phases 0-1, 6-7 = 0xFF
    uint32_t phase8 = tInPaper % 8;
    if (phase8 < 2 || phase8 > 5)
        return 0xFF;
    isAttribute = (phase8 & 1) != 0;
}
else  // ULA_DISCRETE_LOGIC
{
    // 4T: ALL phases have data, no shift gaps
    uint32_t phase4 = tInPaper % 4;
    isAttribute = (phase4 >= 2);
}
```

---

## 9. Test coverage

File: `core/tests/emulator/video/io_contention_test.cpp`

### Ferranti ULA (ZX-48K/ZX-128K) — 8T pipeline

| Test | Description |
|---|---|
| `ZX48k_FloatingBus8TPipelineAllPhases` | All 8 phases: 0xFF at 0,1,6,7; pixel at 2,4; attr at 3,5 |
| `ZX48k_FloatingBusPixelByteInPaper` | Pixel byte returned at phase 4 |
| `ZX48k_FloatingBusAttributeByteInPaper` | Attribute byte returned at phase 5 |
| `ZX48k_FloatingBusReturnsDifferentAttributePerColumn` | Different cells across line |
| `ZX48k_FloatingBusCorrectForLine64` | Interleaved pixel addr for y=64 |
| `ZX48k_FloatingBusCorrectForLine64Attribute` | Interleaved attr addr for y=64 |
| `ZX48k_FloatingBusReturnsFFInBlank` | 0xFF before screen area |
| `ZX48k_FloatingBusReturnsFFInBorder` | 0xFF in border |
| `ZX48k_FloatingBusReturnsFFAfterLine191` | 0xFF below visible area |
| `ZX128k_FloatingBusWorks` | 128K uses same 8T pipeline |

### Discrete logic (Pentagon) — 4T continuous

| Test | Description |
|---|---|
| `Pentagon_FloatingBusWorksDespiteNoContention` | Float bus works without contention |
| `Pentagon_FloatingBus4TDiscretePipelineAllPhases` | All phases: pixel at 0,1; attr at 2,3 |
| `Pentagon_FloatingBusNeverReturnsFFDuringPaper` | No 0xFF during paper (no shift gaps) |

---

## 10. Historical bugs

### Bug: `_contentionEnabled` guard on floating bus (Pentagon)

Early implementation guarded `GetFloatingBus()` with
`if (!_contentionEnabled) return 0xFF;`. Since Pentagon has
`_contentionEnabled = false`, floating bus was disabled entirely.
Fix: Remove the guard — floating bus is independent of contention.

### Bug: `result == 0xFF` trigger for floating bus (TRDOS)

Floating bus override was triggered by `if (result == 0xFF && (port & 1))`.
WD1793 data ports return 0xFF for empty disk sectors, which got replaced with
video attribute bytes.
Fix: Use `_lastPortDecoded` flag instead of checking result value.

### Bug: Attribute returned for all 4 T-states per cell (Pentagon jitter)

Old code returned the attribute byte for all phases of each 4T cell without
distinguishing pixel phases from attribute phases. This caused sync drift in
"Across the Edge" demo because the `IN A,(C)` sync instruction could read the
pixel byte as if it were an attribute.
Fix: Implement per-phase pixel/attribute distinction for both architectures.
