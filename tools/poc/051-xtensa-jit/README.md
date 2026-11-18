# 051 — xtensa-jit

Feature: [`docs/inprogress/2026-11-15-tsconf-vdac4`](../../../docs/inprogress/2026-11-15-tsconf-vdac4/design.md)
(§7.2 execution engine, §7.3 speed target, §7.5 jobs).

DOOM-VDAC4 drops to about 30 fps on the Steam Deck while shooting. The ESP32-S3 app (the Xtensa interpreter,
`core/src/3rdparty/unreal-xtensa`) is the critical path: on the M-class build host the ESP worker runs about
4.3 ms of jobs per 20 ms frame, the emulation thread about 2.5 ms of its own; the Deck is 3-4x slower per core.
Moving the ESP to a worker thread (design §7.5) gave 7.3 -> 6.1 ms per frame on the host and 28 -> 31 fps on the
Deck. The interpreter is already a predecoded, block-cached, computed-goto one: tuning it gives percent, not
multiples. This POC measures where the ESP's time goes and prototypes a JIT.

## Goals

1. **Where the time goes** (DOOM in game, the shooting scene):
   - guest instructions per emulated ms on each core, and the idle share (WAITI / no ready task);
   - host ns per guest instruction in the interpreter (by itself and with the firmware traps);
   - the split of the ESP's host time: guest code, firmware traps (zm_core_t, malloc, newlib), the LINK / FT812
     path, the scheduler (task switches, LoadState / SaveState);
   - the hottest guest blocks and functions (addresses in DOOM.ELF, which has no symbols) and how concentrated
     they are (the share of the top 10 / 100 blocks);
   - the block shape: instructions per block, block exits (taken branch, fall-through, call, loop end, trap),
     register-window traffic (CALL4/8/12, RETW, overflow / underflow handlers).
2. **Is a JIT worth it**: how much faster than the interpreter can native code for Xtensa blocks run that
   workload, bit-exact, on arm64 (macOS) and x86_64 (Linux, the Deck).
3. **Which design fits the existing CPU**: block selection, register mapping of the window (AR via WindowBase),
   zero-overhead loops, the instruction budget (Run stops at the same instruction as the interpreter),
   self-modifying code / invalidation, traps and memory faults, the memory fast path, W^X.

## Requirements

- Workload: DOOM-VDAC4 in game, deterministic (machine created paused, `_DOOM.SPG` loaded, `run_frames` 6000,
  then the 300-frame shooting scene of the A/B runs), plus a standalone capture of that workload (ESP memory and
  task states, the LINK reads it sees) the POC replays without the Z80 side.
- The answer for both hosts: macOS arm64 (M-class) and Linux x86_64 (the build host .238 class; the Deck for the
  final check only).
- Bit-exactness: after the same budget the JIT and the interpreter leave the same CPU state and memory; in the
  machine, the same DOOM screen hash after 6300 frames.
- Measurements: interleaved A/B rounds, both variants in the same run (`docs/guidelines/performance-guidelines.md`).

## Constraints

- One instruction = one cycle (design D5) stays; CCOUNT, the slice budget and the window handler costs are the
  interpreter's.
- POC code lives here; the emulator's code is touched only for opt-in measurement hooks (off = no cost, checked by
  an A/B) and for the capture. Adopting the JIT means re-implementing it in `unreal-xtensa` with its own tests.
- No third-party JIT or assembler libraries: own emitters for the two backends.
- macOS: `MAP_JIT` + `pthread_jit_write_protect_np` (W^X per thread); Linux: plain `mmap` RW -> RX.
- The interpreter stays the reference and the fallback (any block the JIT does not take runs interpreted).

## Variants

- `a-interp`: the current interpreter (reference).
- `b-jit-blocks`: one native function per predecoded block, AR operands through WindowBase in memory, exits back
  to the dispatcher.
- `c-jit-chained`: `b` plus direct block-to-block jumps (patched exits), the window base kept in a host register
  across a chain, the memory fast path inline.

## Acceptance criteria / expected results

- Measurement (goal 1): the split above with the share of each part; expected: guest code > 70 % of the ESP's
  host time, the top 100 blocks > 60 % of the executed instructions (DOOM's column / span drawers, the FT812
  display-list writer).
- **Go** for adoption: `c` at least 3x `a` in guest instructions per host second on the captured workload on
  both hosts, bit-exact, and the projected Deck frame (the ESP's share divided by the gain) under 20 ms in the
  shooting scene.
- 1.5-3x: go only together with the cheaper levers the measurement points to (FT812 path, traps); below 1.5x:
  no-go, the measurement decides what to do instead (native firmware for hot library routines, the FT path).

## Plan

1. Opt-in profile in the CPU / app host (env, off by default): per-block entry counts and instructions, exit
   kinds, trap counts by entry, idle cycles per core; dump at exit. A/B that the off state costs nothing.
2. Capture / replay harness (`cpp/`): the ESP at frame 6000 and the LINK reads of the next 300 frames; replay
   through `a` and check that it reproduces the capture.
3. `b` on arm64, then x86_64; bit-exact checks against `a` instruction block by block, then over the capture.
4. `c`; measurements, analysis, conclusions, proposal.

Raw data and builds: `scratch/2026-11-17-poc-051/`.
