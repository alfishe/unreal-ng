# 09 — Master Reference: All Findings and Change Plan

**Date:** 2026-08-09
**Scope:** Complete findings from MiSTer HDL vs emulator timing analysis
**Purpose:** Single-page reference consolidating all 5 findings, their impact,
exact file locations, and fix values.

---

## Finding matrix

| # | Finding | Severity | Pentagon | ZX-48K | ZX-128K | Doc |
|---|---------|----------|----------|--------|---------|-----|
| 0 | INT signal position wrong | **Critical** | Off by 71,606 T | Off by 1,781 T | Off by 2,043 T | [07](07-int-signal-timings.md) |
| 1 | Border update granularity | Medium | Correct ✅ | Too precise | Too precise | [02](02-border-timing.md) |
| 2 | Attribute latching missing | High | Over-precise | Over-precise | Over-precise | [03](03-multicolor-latching.md) |
| 3 | Memory contention missing | High | Accidentally OK | Major inaccuracy | Major inaccuracy | [04](04-memory-contention.md) |
| 4 | Frame/line config errors | Low–High | N/A | Wrong frame size | Minor issues | [05](05-frame-timing-fixes.md) |

---

## MiSTer HDL reference values

Source: `ZX-Spectrum_MISTer/rtl/ula.sv` lines 96-173.

### Frame structure

| Parameter | Pentagon | ZX-48K | ZX-128K |
|---|---|---|---|
| HC per line | 448 | 448 | 456 |
| T-states/line | 224 | 224 | 228 |
| Lines/frame | 320 | 312 | 311 |
| Frame t-states | 71,680 | 69,888 | 70,908 |

### INT generation

| Parameter | Pentagon | ZX-48K | ZX-128K |
|---|---|---|---|
| INT trigger (vc, hc) | (239, 326) | (248, 4) | (248, 8) |
| INT duration (HC) | 64 | 64 | 72 |
| INT duration (T-states) | 32 | 32 | 36 |
| Mid-line? | **Yes** (73% through) | No (1%) | No (2%) |
| Frame-wrapping? | **Yes** | No | No |

### Sync positions (in HC)

| Signal | Pentagon | ZX-48K | ZX-128K |
|---|---|---|---|
| HBlank on/off | 312/416 | 312/420 | 312/424 |
| HSync on/off | 336/368 | 338/370 | 340/372 |
| VSync on/off | 248/256 | 240/244 | 240/244 |
| VBlank on/off | 236/272 | 236/264 | 236/264 |

### Border update rate

| Model | Rate | Source |
|---|---|---|
| Pentagon | Every t-state (1T) | `ula.sv` line 185 |
| ZX-48K/128K | Every 4 t-states (4T) | `ula.sv` lines 175-178 |

### Attribute latching

| Event | HC position within 16-HC cell |
|---|---|
| Set VRAM address | 8 or C |
| Latch pixel byte | 9 or D |
| Latch attribute byte | B or F |
| Load shift register | next cell, HC[2:0]=4 |

---

## Current emulator values vs correct values

### INI configuration (`data/configs/*/unreal.ini`)

| Model | Setting | Current | Correct | Fix |
|---|---|---|---|---|
| Pentagon128K | `Frame` | 71680 | 71680 | ✅ no change |
| Pentagon128K | `Line` | 224 | 224 | ✅ no change |
| Pentagon128K | `intstart` | **13** | **71619** | ❌ change |
| Pentagon128K | `intlen` | 32 | 32 | ✅ no change |
| Pentagon512K | `intstart` | **13** | **71619** | ❌ change |
| ZX-48K | `Frame` | **70908** | **69888** | ❌ change |
| ZX-48K | `Line` | **228** | **224** | ❌ change |
| ZX-48K | `intstart` | **13** | **1794** | ❌ change |
| ZX-48K | `intlen` | 32 | 32 | ✅ no change |
| ZX-128K | `Frame` | 70908 | 70908 | ✅ no change |
| ZX-128K | `Line` | 228 | 228 | ✅ no change |
| ZX-128K | `intstart` | **13** | **2056** | ❌ change |
| ZX-128K | `intlen` | **32** | **36** | ❌ change |
| ZX+3 | `intstart` | **13** | **2056** | ❌ change |
| ZX+3 | `intlen` | **32** | **36** | ❌ change |

### Code changes

| File | Location | Issue | Fix |
|---|---|---|---|
| `screen.h:314` | M_ZX128 raster | `vBlankLines=16` → 312 total lines | Change to 15 (311 total) |
| `screen.h:316` | M_PMC raster | Same as M_PENTAGON (copy) | Verify after Pentagon fix |
| `config.cpp:283` | `border_4T` loaded but unused | Dead code | Wire to border latching |
| `config.cpp:284` | `even_M1` loaded but unused | Dead code | Wire to M1 alignment |
| `screenzx.cpp:649-669` | Draw() reads RAM every T | No attribute latching | Add latch logic |
| `screenzx.cpp:672-677` | Draw() reads border every T | No 4T border latching | Add border latch |
| `z80.cpp` (all) | No contention delay | Missing feature | Add contention model |

---

## Implementation phases

```
Phase 0: INT position        ← Critical, 1 hour, config-only
    │
    └──> Phase 1: Frame timing  ← 30 min, config + raster fix
            │
            ├──> Phase 4: Contention    ← 8-16 hours, new code
            │                           │
            └──> Phase 2: Border 4T     ← 2-4 hours, code change
                                        │
                                        └──> Phase 3: Attr latch  ← 4-8 hours
```

---

## INT position calculation formula

```
INPUT: MiSTer (vc_trigger, hc_trigger)
       raster descriptor: vSyncLines, vBlankLines, screenOffsetTop
       config: t_line (t-states per line)

    paperStartLine = vSyncLines + vBlankLines + screenOffsetTop
    emulatorLine   = (vc_trigger + paperStartLine) mod totalLines
    intstart       = emulatorLine * t_line + (hc_trigger / 2)

VERIFICATION (Pentagon):
    paperStartLine = 16 + 16 + 48 = 80
    emulatorLine   = (239 + 80) mod 320 = 319
    intstart       = 319 * 224 + 163 = 71619 ✅

VERIFICATION (ZX-48K):
    paperStartLine = 8 + 16 + 48 = 72
    emulatorLine   = (248 + 72) mod 312 = 320 mod 312 = 8
    intstart       = 8 * 224 + 2 = 1794 ✅

VERIFICATION (ZX-128K):
    paperStartLine = 8 + 16 + 48 = 72
    emulatorLine   = (248 + 72) mod 311 = 320 mod 311 = 9
    intstart       = 9 * 228 + 4 = 2056 ✅
```

---

## All affected files

### Config files (INI)

```
data/configs/pentagon128k/unreal.ini     → intstart=71619
data/configs/pentagon512k/unreal.ini     → intstart=71619
data/configs/spectrum48/unreal.ini       → Frame=69888, Line=224, intstart=1794
data/configs/spectrum128/unreal.ini      → intstart=2056, intlen=36
data/configs/spectrum3/unreal.ini        → intstart=2056, intlen=36
data/configs/zx-diagnostics/unreal.ini   → depends on model
data/configs/ts-conf/unreal.ini          → no change (TSConf has own INT logic)
```

### Source files (C++)

```
core/src/emulator/video/screen.h         → M_ZX128 vBlankLines fix (line 314)
                                          → add borderUpdateGranularity to RasterState
core/src/emulator/video/screen.cpp       → set granularity in SetVideoMode()
core/src/emulator/video/zx/screenzx.h    → add _latchedBorderColor, _latchedPixels,
                                            _latchedAttributes, latch point tracking
core/src/emulator/video/zx/screenzx.cpp  → border latch in Draw() (line 672)
                                          → attribute latch in Draw() (line 649)
                                          → isLatchPoint in CreateTstateLUT() (line 139)
core/src/emulator/cpu/z80.cpp            → add contention delay (new, after mem_read)
core/src/emulator/config.cpp             → add ApplyModelTimingDefaults() (after line 313)
NEW: core/src/emulator/contention.h       → contention delay tables per model
NEW: core/src/emulator/contention.cpp     → contention implementation
```

### MiSTer HDL reference files

```
ZX-Spectrum_MISTer/rtl/ula.sv            → primary reference for all timing
ZX-Spectrum_MISTer/ZX-Spectrum.sv        → model select (mZX, m128)
```

---

## Testing checklist

- [ ] Pentagon INT fires at t-state 71619 (debug log in ProcessInterrupts)
- [ ] ZX-48K INT fires at t-state 1794
- [ ] ZX-128K INT fires at t-state 2056
- [ ] ZX-128K INT duration is 36 t-states
- [ ] ZX-48K frame rate ≈ 50.08 Hz (69888 / 3.5MHz)
- [ ] Pentagon raster bar demo aligns correctly
- [ ] ZX-48K border changes show 8-pixel steps (4T granularity)
- [ ] Pentagon border changes show 2-pixel steps (1T granularity)
- [ ] Multicolor demo: no intra-cell color splits
- [ ] Contention test ROM: CPU timing matches hardware
- [ ] Pentagon regression: no visual changes from current behavior
