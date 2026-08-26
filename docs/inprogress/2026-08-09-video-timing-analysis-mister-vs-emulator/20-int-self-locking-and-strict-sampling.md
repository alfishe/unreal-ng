# 20 — INT Self-Locking (Why Real Pentagon Is Rock Stable) + Strict Acceptance Sampling

**Date:** 2026-08-14  
**Scope:** *Across the Edge* run-to-run instability — "sometimes precise, sometimes 4 px off"  
**Trigger:** Spectrum developers' report: on a correctly adjusted Pentagon 128K the demo effect is stable and never shifts.  
**Files:** `core/src/emulator/cpu/z80.cpp` (`ProcessInterrupts`), `core/tests/emulator/video/int_timing_test.cpp`

---

## 1. The Self-Locking Mechanism (Digital PLL)

The demo's Phase-2 loop (2197 T/iteration) runs **continuously across frames**; once per
frame the INT acceptance inserts `H = H_isr + w(φ)` T into the loop's local time, where

- `H_isr` — total ISR time (INT response + handler body), constant per frame;
- `w(φ)` — **acceptance wait**: distance from the INT trigger (t = intstart) to the next
  loop-instruction boundary — the *staircase* of doc 19, span `0..13 T`
  (longest instruction in the loop).

Loop phase map: `φ' = φ + (frame − H_isr) − w(φ)  (mod 2197)`

**Lock condition** (fixed point exists): `(frame − H_isr) mod 2197 ∈ [0, 13]`
(the staircase span). Then:

- the fixed point is **unique** — `w(φ*)` absorbs exactly the residual mismatch;
- any startup-phase transient is absorbed within a frame or two;
- every frame is bit-identical to the previous one → the effect **never shifts**,
  regardless of when the demo was started.

This is why the effect does *not* need `frame ≡ 0 (mod 2197)`: the quantized
acceptance wait is the phase detector of a digital PLL.

### 1.1 The Demo's Numbers

- Loop L = 2197 T, frame = 71680 T.
- Static ISR total with **our** opcode timings (IM2 response 19 T + BF26 stub
  DI+JP 14 T + frame-counter body 326 T) = **359 T** (excludes `call C003h`).
- Lock requires `C003 ≡ 1004..1017 (mod 2197)` → ≈ **1010 T** or **3207 T** —
  plausible for Demarche's driver routine. On real hardware it evidently holds:
  hence "never shifts".
- Our old audio-capture H = 1635 T (→ C003 ≈ 1276 T) **could not lock**
  (`(71680−1635) mod 2197 = 1938` — pure drift) — but that capture predates the
  doc-17 HandleINT fix and doc-19 port-T2 fix, so it is stale evidence.

## 2. Why Ours Was Bistable ±4 px

After docs 17+19 the user sees *mostly* correct alignment → we are inside the lock
window — but occasionally the effect settles one state off, 2 T (4 px) apart.
Cause: the last known ±1 T acceptance deviation (doc 19 §4 / doc 18 §4):

- ours: **inclusive** sampling — `cpu.t >= int_start` — an instruction boundary
  landing exactly at `int_start` accepts **on the same T**;
- hardware: **strict** — the ULA registers INT one clock *after* the raster compare
  (MiSTer `ula.sv`: `INT <= 1` on the next edge), and the CPU samples INT only at
  end-of-instruction edges → a boundary exactly at `int_start` still sees INT low.

In the PLL picture: a ±1 T error in the staircase placement lets the lock settle on
either of two adjacent states depending on the run's approach direction →
run-to-run ±4 px.

## 3. Fix: Strict Acceptance Sampling

`Z80::ProcessInterrupts`:

```cpp
// was: if (!int_occurred && cpu.t >= int_start)
if (!int_occurred && cpu.t > int_start)
```

- `int_pending` **clear** stays `t >= int_end` (symmetric: at the deassertion edge
  the CPU already sees INT low).
- IM2 response stays **19 T** (real Z80; T80's ≈18 T is the HDL's deviation — the
  demo locks on real silicon, so 19 T is the reference for the lock arithmetic).

## 4. Verification

- [x] `INTTiming_Test.*` 29/29 — one tautology test renamed to
      `Pentagon_INTAcceptanceIsStrictlyAfterStart` and updated to document the
      strict semantics
- [x] `IOContention_Test.* + FloatingBus_Test.* + Z80_Test.*` — 78/78 total
- [x] `ninja core-tests` clean (one pre-existing `nodiscard` warning in the test
      fixture, unrelated)
- [x] *Across the Edge* runtime retest via WebAPI (2026-08-14, release app with
      docs 17+18+19+20, `fix0.sna`, Pentagon instance) — see §4.1

### 4.1 Runtime Verification via WebAPI (port 8090)

Protocol: load `across-the-edge-fix0.sna` → run → pause → `run_frames` to a
fixed index → `capture/screen` (full framebuffer PNG) → compare.

- **10 consecutive live snapshot loads** (load while running, 2.5 s settle,
  pause, +20 frames): **10/10 bit-identical frames** (same MD5). The
  load-to-load variance the user observed on the pre-doc-20 binary does not
  reproduce.
- **Frame periodicity**: stepping in 25-frame increments over 350 frames shows
  7 distinct visual phases (the demo's own cycle), each persisting as
  **pixel-identical consecutive frames** — the digital-PLL fixed point: once
  locked, every frame renders exactly like the previous one.
- Analysis artifacts under `build/sync-analysis/` (scripts + captured frames).

WebAPI quirks found along the way (not timing-related, worth separate fixes):

1. **`lua get_registers()` crashes the app**: SIGSEGV at `sol::state_view`
   copy inside the `get_registers` lambda (`lua_emulator.h:146` — null/dangling
   `lua_State*` when invoked via `/api/v1/lua/exec`). Reproduced: crashed the
   user's running instance this session (crash log `unreal-qt-…-154233.ips`).
2. Paused snapshot-load + `run_frames` yields a blank capture (render path not
   run from that state); live-load + pause + `run_frames` works.
3. Live (unpaused) captures can tear (mid-frame seam at x=175/176 observed) —
   capture reads the framebuffer without frame sync; use pause+step for
   analysis-grade captures.

- [ ] If any residual survives in further use: build the empirical per-frame
      insertion measurement (fresh H with the current build; the remaining ±1 T
      would then be inside an opcode on the ISR path)

## 5. Related

- [19-io-port-write-tstate-placement.md](19-io-port-write-tstate-placement.md) —
  staircase quantization + IO port T2 placement (its §4 "inclusive sampling"
  nuance is resolved by this doc)
- [18-int-to-paper-geometry.md](18-int-to-paper-geometry.md) — intstart = 71635
- [17-int-response-double-counting-fix.md](17-int-response-double-counting-fix.md) —
  ISR response T accounting
