# 06 — Consolidated Fix Plan

**Date:** 2026-08-09
**Scope:** Video timing accuracy improvements for Pentagon, ZX-48K, ZX-128K

---

## Implementation phases

Fixes are ordered by dependency and impact. Each phase can be verified
independently before proceeding to the next.

### Phase 0 — INT signal position correction (Critical, fast)

**Estimated effort:** 1 hour
**See:** [07-int-signal-timings.md](07-int-signal-timings.md)
**Files to modify:**

| File | Change |
|---|---|
| `data/configs/pentagon128k/unreal.ini` | `intstart=71619` |
| `data/configs/pentagon512k/unreal.ini` | `intstart=71619` |
| `data/configs/spectrum48/unreal.ini` | `intstart=1794` |
| `data/configs/spectrum128/unreal.ini` | `intstart=2056`, `intlen=36` |
| `data/configs/spectrum3/unreal.ini` | `intstart=2056`, `intlen=36` |

**Why it's Phase 0:** The INT position determines the reference point for ALL
other timing. Border effects, multicolor effects, and cycle-counted code all
measure time relative to INT. Fixing INT position first ensures all subsequent
fixes have the correct baseline.

**Verification:**
- Run a Pentagon demo that uses cycle-counted border effects
- Verify ISR entry occurs near the END of the frame (not at t-state 13)
- Confirm INT wraps across frame boundary correctly

---

### Phase 1 — Frame timing configuration fixes (Low risk, fast)

**Estimated effort:** 30 minutes
**Files to modify:**

| File | Change |
|---|---|
| `data/configs/spectrum48/unreal.ini` | `Frame=69888`, `Line=224` |
| `data/configs/spectrum128/unreal.ini` | `intlen=36` |
| `core/src/emulator/video/screen.h` | M_ZX128 `vBlankLines` → 15 |

**Verification:**
- Run a known ZX-48K timing test (e.g., contention test ROM)
- Verify frame rate is ~50.08 Hz for ZX-48K (69888 / 3.5MHz)
- Check that INT fires at correct position relative to screen content

---

### Phase 2 — Border color update granularity (Medium risk)

**Estimated effort:** 2-4 hours
**Depends on:** Phase 1 (correct frame timing is needed for accurate border position)
**Files to modify:**

| File | Change |
|---|---|
| `core/src/emulator/video/screen.h` | Add `borderUpdateGranularity` to `RasterState` |
| `core/src/emulator/video/screen.cpp` | Set granularity in `SetVideoMode()` |
| `core/src/emulator/video/zx/screenzx.h` | Add `_latchedBorderColor` field |
| `core/src/emulator/video/zx/screenzx.cpp` | Latch border at correct intervals in `Draw()` |

**Design notes:**
- Pentagon (`M_PENTAGON128K`, `M_PMC`): granularity = 1 (every t-state)
- ZX-48K/128K (`M_ZX48`, `M_ZX128`): granularity = 4 (every 4 t-states)
- The existing `config.border_4T` flag can optionally override this, but
  the default should be model-derived

**Verification:**
- Run a border raster bar demo on ZX-48K — should show 8-pixel steps
- Same demo on Pentagon — should show 2-pixel steps
- Compare side-by-side with MiSTer output

---

### Phase 3 — Multicolor attribute latching (High impact, complex)

**Estimated effort:** 4-8 hours
**Depends on:** Phase 2 (shared latching infrastructure)
**Files to modify:**

| File | Change |
|---|---|
| `core/src/emulator/video/zx/screenzx.h` | Add `_latchedPixels`, `_latchedAttributes`, latch-point tracking |
| `core/src/emulator/video/zx/screenzx.cpp` | Implement latch logic in `Draw()` and `CreateTstateLUT()` |

**Design notes:**
- Add `isLatchPoint` flag to `TstateCoordLUT` struct
- Latch fires at HC position B within each 16-HC character cell (see doc 03)
- Latched values are used for all subsequent Draw calls within the same cell
- Only applies when ScreenHQ is ON (per-t-state mode)

**Verification:**
- Run multicolor demos (e.g., "Bustic", "Yoghurt's Tribute")
- Compare attribute transition points with MiSTer
- Verify no intra-cell color splits on hardware-accurate timing

---

### Phase 4 — Memory contention (High impact, most complex)

**Estimated effort:** 8-16 hours
**Depends on:** Phase 1 (correct frame structure)
**Files to modify:**

| File | Change |
|---|---|
| New: `core/src/emulator/contention.h` | Contention model interface |
| New: `core/src/emulator/contention.cpp` | Contention delay tables per model |
| `core/src/emulator/cpu/z80.cpp` | Add contention delay to memory/IO access |
| `core/src/emulator/video/screen.h` | Wire video mode to contention queries |

**Design notes:**
- Pentagon: zero contention (current behavior is correct)
- ZX-48K: classic contention pattern for 0x4000-0x7FFF
- ZX-128K: extended contention including banked screen at 0xC000
- Port 0xFE access also triggers contention
- Contention must be applied **before** Phase 3 verification to ensure
  correct CPU write timing relative to attribute latch points

**Verification:**
- Run contention test ROMs
- Verify CPU instruction timing during active display
- Compare border/multicolor effect positions with hardware
- Check beeper audio timing accuracy

---

## Priority matrix

| Fix | Pentagon benefit | ZX-48/128 benefit | Effort | Priority |
|---|---|---|---|---|
| **Phase 0: INT position** | **Critical** | **Critical** | Low | **Do first** |
| Phase 1: Frame timing | None | High | Low | Do second |
| Phase 4: Contention | None | Critical | High | Do third |
| Phase 2: Border granularity | None | Medium | Medium | Do fourth |
| Phase 3: Attr latching | Minor | High | High | Do last |

## Dependency graph

```
Phase 0 (INT position)  ←─ must be first
    │
    └──> Phase 1 (frame timing)
            │
            ├──> Phase 4 (contention) ──┐
            │                           │
            └──> Phase 2 (border) ──────┤
                                        │
                                        └──> Phase 3 (attr latching)
```

Phase 0 must be done before everything else — it establishes the reference
point for all timing. After that, Phase 1 (frame config) is a quick follow-up.
Phases 2 and 4 are independent and can be done in parallel.
Phase 3 depends on Phase 2 (shared latching pattern) and benefits from
Phase 4 (correct write timing).

## Testing strategy

1. **Unit tests:** Add timing verification tests that check frame t-state counts,
   line counts, and INT positions per model.

2. **Visual comparison:** Capture screenshots of specific test patterns on both
   the emulator and MiSTer for side-by-side comparison.

3. **Demo compatibility:** Run a suite of known timing-sensitive demos:
   - Border raster bars (tests Phase 2)
   - Multicolor effects (tests Phase 3)
   - Contention-sensitive routines (tests Phase 4)

4. **Regression:** Ensure Pentagon accuracy is not broken by any changes.

## Multi-clone architecture consideration

The emulator supports multiple clone models (`MM_PENTAGON`, `MM_SPECTRUM48`,
`MM_SPECTRUM128`, `MM_PLUS3`, etc.). All timing fixes must be model-aware:

- Contention tables differ per model
- Border granularity differs per model
- INT timing differs per model
- Frame/line counts differ per model

The `MEM_MODEL` enum and `CONFIG` struct already provide model differentiation.
The fix should use `config.mem_model` to select the correct timing profile,
similar to how MiSTer uses `mZX`/`m128` signals.
