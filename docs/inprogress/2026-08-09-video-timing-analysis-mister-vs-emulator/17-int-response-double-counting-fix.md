# 17 — INT Response Double-Counting Fix (Root Cause of 71619→71625+fix1)

**Date:** 2026-08-14  
**Scope:** `Z80::HandleINT()` interrupt response T-state accounting, Pentagon `intstart` calibration  
**Reference Core:** `core/src/emulator/cpu/z80.cpp`, `core/src/emulator/config.cpp`  
**Reference HDL:** `ZX-Spectrum_MISTer/rtl/ula.sv`, `rtl/T80/T80.vhd`  
**Reference Emulator:** `other/unrealspeccy/z80/op_system.h` (`handle_int`)

---

## 1. Problem Statement

The MiSTer-derived Pentagon `intstart` is exactly **71619** (see docs 07/14):

```
paperStartLine = 16 + 16 + 48 = 80
emulatorLine   = (239 + 80) mod 320 = 319
intstart       = 319 * 224 + 326/2 = 71619
```

Yet *Across the Edge* (Demarche) only rendered correctly with the empirical
correction `intstart = 71625` **plus** the `fix_1.trd` variant (+2T), i.e. an
effective +8T offset. Earlier sessions attempted to explain this with
framebuffer HBlank-baseline offsets (+4T → 71623) and LUT compensation
(+6T → 71625) — both were symptoms, not the cause.

**Actual root cause:** `Z80::HandleINT()` double-counted the memory cycles of
the interrupt response sequence.

---

## 2. Root Cause Analysis

### 2.1 The Bug

`HandleINT()` added the *full* interrupt response duration as a lump sum,
then performed the stack pushes (and IM2 vector fetch) through `wd()`/`rd()`,
which each **add +3T again** via `IncrementCPUCyclesCounter(3)`:

```cpp
IncrementCPUCyclesCounter(19);   // IM2: "M1=7T + push PCH=3T + push PCL=3T + VL=3T + VH=3T"
...
interruptHandlerAddress = rd(vectorAddress) + 0x100 * rd(vectorAddress + 1);  // +6T (bug)
...
wd(--sp, cpu.pch);               // +3T (bug)
wd(--sp, cpu.pcl);               // +3T (bug)
```

### 2.2 Resulting ISR Entry Delay

| Mode | Intended | Actually added | Excess | First ISR instruction at (intstart=71619) |
|---|---|---|---|---|
| IM0/IM1 | 13 T | 13 + 2×3 = **19 T** | **+6 T** | 71638+6 = 71644 |
| IM2      | 19 T | 6 + 19 + 6 = **31 T** | **+12 T** | 71638+12 = 71650 |

Hardware (MiSTer T80): INT asserts at 71619, acceptance begins at the next
instruction boundary, IM2 response ≈ 19 T → first ISR instruction at
**71638+δ** (δ = 0..3 T HALT sampling granularity, T80 samples INT on 4 T M1
boundaries while halted).

The empirical +6 T INI correction matches the **IM1 excess** exactly;
*Across the Edge* uses **IM2** (vector `BEFFh`, `I=BEh`), whose +12 T excess
was partially compensated by the INI (+6) and the demo variant (+2), leaving
a residual few-T misalignment and per-variant sensitivity.

### 2.3 Reference Behavior (original Unreal Speccy)

`other/unrealspeccy/z80/op_system.h`:

```cpp
Z80INLINE void handle_int(Z80 *cpu, unsigned char vector)
{
   ...
   cpu->t += (cpu->im < 2) ? 13 : 19;     // full duration, once
   cpu->MemIf->wm(--cpu->sp, cpu->pch);   // raw write - NO t increment
   cpu->MemIf->wm(--cpu->sp, cpu->pcl);
   ...
}
```

The lump sum **includes** the bus cycles; raw `MemIf` calls perform no
additional accounting. Our port inverted that contract: lump sum *plus* timed
accesses.

---

## 3. The Fix

### 3.1 `core/src/emulator/cpu/z80.cpp` — `HandleINT()`

Vector fetch and stack pushes now use the raw memory interface (the same
`MemIf->MemoryRead/MemoryWrite` calls that `rd()`/`wd()` dispatch to, minus
contention checks and T-state increments):

```cpp
// IM2 vector fetch - no T-state accounting (timing is in interruptDuration)
interruptHandlerAddress = (_memory->*MemIf->MemoryRead)(vectorAddress, false) +
                          0x100 * (_memory->*MemIf->MemoryRead)(vectorAddress + 1, false);
// Stack push - no T-state accounting
(_memory->*MemIf->MemoryWrite)(--sp, cpu.pch);
(_memory->*MemIf->MemoryWrite)(--sp, cpu.pcl);
```

Notes:
- Raw interface keeps ROM write protection (`Memory::_bank_write[]`) and
  debug-mode tracking (write breakpoints / memory tracker) intact.
- INT acknowledge bus cycles are not contended on real ULA hardware; pushes
  into contended memory during the response are not modeled by any of the
  three reference emulators, matching this fix.
- `interruptDuration` stays **13 T (IM0/IM1) / 19 T (IM2)** — unchanged,
  now the *only* T accounting for the response.

### 3.2 `core/src/emulator/config.cpp` — `ApplyModelTimingDefaults()`

Pentagon default reverted to the HDL-exact value (removes the +6 T
compensation that masked the bug):

```
MM_PENTAGON: intstart 71625 → 71619
```

### 3.3 Configuration files

| File | Change |
|---|---|
| `data/configs/pentagon128k/unreal.ini` | `intstart` 71625 → **71619** |
| `data/configs/pentagon512k/unreal.ini` | `intstart` 71625 → **71619** |
| `data/configs/spectrum3/unreal.ini` | `intstart` 71619 → **2056**, `intlen` 32 → **36** (copy-paste of Pentagon values introduced by commit `2e37b34f`; +3 shares ZX-128K ULA timing) |

> [!WARNING]
> `spectrum3/unreal.ini` still carries `Frame=71680` / `Line=224` (Pentagon
> frame geometry) — pre-dating `2e37b34f`. ZX +3 hardware is 311×228 T =
> 70908 T (descriptor `M_ZX128`). Left unchanged pending a separate
> verification pass over the +3 raster path.

### 3.4 Tests

`core/tests/emulator/video/int_timing_test.cpp`: Pentagon expectations
updated 71623 → **71619** (they had drifted: assertions still checked the
intermediate 71623 value and were failing before this fix).

---

## 4. Expected Observable Effects

1. **ISR entry alignment**: with `intstart=71619`, first ISR instruction now
   at 71638+δ (was 71650+δ in IM2) — within 0–4 T of MiSTer.
2. **Across the Edge**: retest required. The previously working combination
   (`71625` + `fix_1`) over-delays the ISR by ~8 T; the expected correct
   combination is now **`71619` + `fix_0`** (possibly `fix_1` due to the
   T80's 4 T HALT sampling granularity — accept the variant that renders
   stable and prefer the earliest).
3. **IM1 software** (most games/48K): 6 T earlier ISR entry per frame;
   interrupt-driven frame counters unchanged in behavior.
4. All models benefit — the bug was model-independent.

## 5. Verification

- [x] `ninja core` — builds clean under `-Werror`
- [x] `INTTiming_Test.*` — 29/29 pass (incl. corrected 71619 assertions)
- [x] Full `core-tests` run: 113/118; the 5 failures verified **pre-existing
      on clean master** via stash/rebuild (TRDOS ×1, KeyboardInjection ×2,
      stale-71623 assertions ×2 — the latter two fixed here)
- [ ] *Across the Edge* manual retest with `fix_0`..`fix_3` (user)
