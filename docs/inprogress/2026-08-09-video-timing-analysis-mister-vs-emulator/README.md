# Video Timing Analysis: MiSTer HDL vs unreal-ng Emulator

**Date:** 2026-08-09
**Scope:** Pentagon / ZX-Spectrum 48K / ZX-Spectrum 128K video timing comparison
**Reference HDL:** `/Volumes/TB4-4Tb/Projects/mister/cores/ZX-Spectrum_MISTer`
**Reference core:** `/Volumes/TB4-4Tb/Projects/Test/unreal-ng/core`

---

## Documents in this set

| Document | Description |
|---|---|
| [01-analysis.md](01-analysis.md) | Full comparative analysis: MiSTer ULA HDL vs emulator timing model |
| [02-border-timing.md](02-border-timing.md) | Deep-dive: border color update granularity per model |
| [03-multicolor-latching.md](03-multicolor-latching.md) | Deep-dive: attribute byte latching and "racing the beam" effects |
| [04-memory-contention.md](04-memory-contention.md) | Deep-dive: ULA memory contention — the missing feature |
| [05-frame-timing-fixes.md](05-frame-timing-fixes.md) | Frame/line configuration corrections for ZX-48K and ZX-128K |
| [06-fix-plan.md](06-fix-plan.md) | Consolidated fix plan with file references and priorities |
| [07-int-signal-timings.md](07-int-signal-timings.md) | Pentagon mid-line INT, per-model INT positions, `intstart` correction |
| [08-int-correction-implementation.md](08-int-correction-implementation.md) | Exact code locations, two approaches, calculation formula, test plan |
| [09-master-reference.md](09-master-reference.md) | Single-page summary: all findings, values, file locations, testing checklist |
| [16-hc-tstate-timing-model.md](16-hc-tstate-timing-model.md) | HC (pixel clock) to T-state mapping, attribute prefetch pipeline timing |
| [17-int-response-double-counting-fix.md](17-int-response-double-counting-fix.md) | HandleINT() rd/wd double-counting fix — root cause of the historical 71623/71625 offsets |
| [18-int-to-paper-geometry.md](18-int-to-paper-geometry.md) | Raster window geometry: why real-Pentagon 17989T INT-to-paper requires `intstart=71635` |
| [19-io-port-write-tstate-placement.md](19-io-port-write-tstate-placement.md) | IO port write T placement (IORQ at T2): the last 2 px — why `intstart` cannot fine-tune (staircase quantization) |
| [20-int-self-locking-and-strict-sampling.md](20-int-self-locking-and-strict-sampling.md) | INT self-locking (digital PLL) — why real Pentagon is rock stable; strict acceptance sampling fix (`t > int_start`) for run-to-run ±4 px |

---

## Executive summary

A comparison of the MiSTer ZX-Spectrum core's `ULA` Verilog module against the
emulator's software renderer revealed **five categories of timing discrepancy**.
The INT signal position is the most impactful — it affects every cycle-precise
program, not just visual effects.

| # | Issue | Pentagon impact | ZX-48/128 impact | Priority |
|---|---|---|---|---|
| 0 | **INT signal position (`intstart`)** | **Massive** — off by 71K T | **Major** — off by ~1.8K T | **Critical** |
| 1 | Border color update granularity | Correct (1T) | Wrong (too precise) | Medium |
| 2 | Attribute byte latching per 8px cell | Over-precise | Over-precise | High |
| 3 | Memory contention not implemented | Accidentally OK | Major inaccuracy | High |
| 4 | Frame/line config mismatch (48K) | N/A | Wrong frame size | Low |

### Why it matters

The emulator currently renders **every model with Pentagon-like timing semantics**:
1-t-state border granularity, no contention, and per-t-state attribute reads. This
makes Pentagon emulation visually accurate but leaves ZX-48K and ZX-128K with
incorrect border precision, wrong CPU timing during screen rendering, and broken
multicolor effect replication.

### Key file references

- MiSTer ULA: `ZX-Spectrum_MISTer/rtl/ula.sv`
- MiSTer top-level model select: `ZX-Spectrum_MISTer/ZX-Spectrum.sv`
- Emulator screen base: `core/src/emulator/video/screen.h`, `screen.cpp`
- Emulator ZX renderer: `core/src/emulator/video/zx/screenzx.h`, `screenzx.cpp`
- Emulator config loader: `core/src/emulator/config.cpp`
- Emulator per-model INI: `data/configs/{model}/unreal.ini`
- Emulator CPU frame loop: `core/src/emulator/cpu/z80.cpp`

---

## How to read these documents

- Start with **01-analysis.md** for the full picture.
- Read **07-int-signal-timings.md** next — the INT position issue is the
  highest-priority finding and affects all timing-sensitive code.
- Then read the deep-dives (02-05) for specific subsystem details.
- Use **06-fix-plan.md** as the implementation checklist.
- Use **09-master-reference.md** as a quick lookup for exact values and file paths.
