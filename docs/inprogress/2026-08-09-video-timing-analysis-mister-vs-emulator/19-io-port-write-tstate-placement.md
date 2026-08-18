# 19 — IO Port Write T-State Placement: The Last 2 Pixels

**Date:** 2026-08-14
**Scope:** IO opcode T-accounting — port access position within the IO machine cycle
**Reference Core:** `core/src/emulator/cpu/op_noprefix.cpp` (`op_D3`, `op_DB`), `op_ed.cpp` (`ope_40`..`ope_79`)
**Reference HDL:** MiSTer `ula.sv` line 185 (Pentagon 1T border update), `T80.vhd` line 1373
**Trigger:** After doc 18 (`intstart=71635`), *Across the Edge* `fix_0` shows a constant
**2 px (1T)** residual that `intstart` tuning cannot remove ("71635 is the closest
to required position, but 2 pixel difference anyway").

---

## 1. Why intstart Cannot Fine-Tune the Effect (Staircase Quantization)

The demo's visible effect (Phase 2, the 2197T `LD B,A5h / DJNZ×165 / … / JP NZ`
loop) runs with **IFF1=1** and no HALT. The once-per-frame INT is therefore
accepted **mid-loop**, at the first instruction boundary after the INT line is
asserted (empirically confirmed by the +1635T once-per-frame insertion in the
audio bitstream capture — see the demo README §4).

Consequence for `intstart` tuning:

```
acceptance(intstart) = first loop-instruction end ≥ intstart
```

The instruction lengths of the loop are 7/13/8/4/7/4/11/6/4/4/10 T. Sweeping
`intstart` by 1T changes **nothing** until it crosses an instruction-end boundary,
at which point the acceptance jumps by the **next instruction length (4..13T =
8..26 px)**. The effect position is *piecewise-constant* in `intstart` — it
cannot be moved by 2 px. The best cell (71635) still leaves the 1T residual.

The HALT path is quantized as well: T80 samples INT only at the end of each 4T
halted-M1 refresh cycle (`T80.vhd:1364`, `MCycle = MCycles` per 4T M1). Our
halted CPU happens to reproduce the same 4T grid — `vm1` is never assigned
(defaults false), so a halted CPU re-executes the HALT opcode as a normal 4T
instruction (`z80.cpp:285` fast path is dead code).

**Conclusion:** the last 2 px cannot be calibrated away — they must be a real
semantic offset somewhere in the chain. And there is one: the port-write T.

## 2. Root Cause: Port Access at IO Cycle Entry Instead of the IORQ T

All 18 IO opcode sites called `out()`/`in()` **before** charging the 4T IO cycle:

```cpp
// op_D3 — OUT (n),A  (BEFORE fix)
cpu->out(port + (cpu->a << 8), cpu->a);   // t = opcode(4T) + operand(3T) = +7T
cputact(4);                                // IO cycle charged AFTER the write
```

The comment above it even stated the correct hardware fact — *"Real Z80: IORQ
goes low at T2 of the IO machine cycle"* — while the code placed the access at
T1 (cycle entry). Z80 IO machine cycles assert IORQ/WR **one clock late** (Zilog
UM: the CPU automatically inserts one wait state before IO; T1 carries no IORQ).

Since `Screen::SetBorderColor()` (`screen.cpp:469`) flushes the framebuffer with
the old color **up to the current t** and applies the new color from that same
t onward, the border change was rendered from x = t_entry·2 — **1T (2 px)
before the hardware could even latch the port write**.

On Pentagon this is fully visible: the border mux applies `border_color` every
pixel clock (`ula.sv:185`, "1T update for border in Pentagon mode"), so the
emulator's border changes led real hardware by exactly 2 px — matching the
empirical residual, including its direction (border effect ahead of paper).

## 3. Fix

Charge 1T of the IO cycle **before** the port access, the remaining 3T after:

```cpp
// op_D3 — OUT (n),A  (AFTER fix)
cputact(1);                                // T1 of IO cycle (no IORQ yet)
cpu->out(port + (cpu->a << 8), cpu->a);    // IORQ/WR T — port write lands here
cputact(3);                                // T3..T4 of IO cycle
```

Total instruction timing is unchanged (11T for `OUT (n),A` / `IN A,(n)`,
12T for the ED forms). Only the **position of the port access within the IO
cycle** shifts by +1T, which also fixes:

- **Border color changes**: now flush at the IORQ T-state (pixel-exact on 1T-border models).
- **Beeper/AY port writes**: sound transition timestamps shift +1T (hardware-correct).
- **Floating-bus sampling** (`Z80::in()` → `GetFloatingBus()`): now samples the
  bus at the T where the ULA actually drives it, not one T early.
- **IO contention lookup** (`GetIOContentionDelay()` computes from absolute t):
  now evaluated at the IORQ T — consistent with the contended-I/O model.

### Sites changed (18)

| File | Opcodes |
|---|---|
| `op_noprefix.cpp` | `op_D3` (OUT (n),A), `op_DB` (IN A,(n)) |
| `op_ed.cpp` | `ope_40/48/50/58/60/68/70/78` (IN r,(C) + undocumented IN (C)), `ope_41/49/51/59/61/69/71/79` (OUT (C),r + undocumented OUT (C),0) |

## 4. Nuance Resolved by Doc 20: Inclusive INT Acceptance Sampling

> [!IMPORTANT]
> **Fixed 2026-08-14 in [doc 20](20-int-self-locking-and-strict-sampling.md):**
> `ProcessInterrupts()` now samples **strictly** (`t > int_start`). Original
> analysis kept below for the record.

Original note (pre-doc-20):

`ProcessInterrupts()` (`z80.cpp:653`) accepts the INT at the first instruction
boundary with `t >= int_start` (**inclusive**). Hardware samples /INT at the
rising edge of the last T-state of the instruction (Zilog UM), and in MiSTer
`ula.sv:170` registers the INT line on the same clock domain that `T80.vhd:1373`
uses to sample it — an INT asserted **exactly** at the sampling edge is seen
only at the **next** boundary (**strict** `t > int_start`).

Impact when a boundary coincides with `int_start`: ours accepts one boundary
early (halted grid: 4T; mid-loop: one instruction length). Zero otherwise.

## 5. Verification

- [x] `ninja core` clean under `-Werror`
- [x] `INTTiming_Test.*` 29/29 (re-verified after final rebuild)
- [x] Full `core-tests` instability is **pre-existing in committed master**:
      isolated in a pristine-HEAD worktree (35544eb9, zero working-tree changes)
      the suite SEGV'd mid-run, with a flaky FAILED set that changes between
      runs (observed there: TRDOS jump, portInBreakpoint, 2 keyboard-injection,
      2 atomic-stepping; Write_Track SEGV excluded via filter). The abort in
      this branch's tree lands at varying tests (BasicEncoder, then
      DebugKeyboardManager on rerun) — a moving crash location cannot come from
      this deterministic +1T port-access shift. Both individual tests pass in
      isolation. Suspect for the root cause: committed 35544eb9 "Enable tape
      audio generation in main emulation loop" (audio thread in the main loop)
      and/or the uncommitted frame-pacing WIP in the working tree — needs a
      separate investigation, out of scope here.
- [ ] *Across the Edge* `fix_0` retest with `intstart=71635` — expected:
      border effect pixel-aligned with paper (residual 2 px → 0)

## 6. Related

- [18-int-to-paper-geometry.md](18-int-to-paper-geometry.md) — why 71635 (its §4
  "absorbable via INI override" claim is superseded by this doc's staircase analysis)
- [17-int-response-double-counting-fix.md](17-int-response-double-counting-fix.md) — INT response T accounting
- [16-hc-tstate-timing-model.md](16-hc-tstate-timing-model.md) — 1T border update rate on Pentagon
