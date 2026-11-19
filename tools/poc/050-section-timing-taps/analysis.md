# 050 — analysis

Numbers: [measurements-2026-11-10-demo.md](measurements-2026-11-10-demo.md).

## Against the expectations

| Question | Expected | Measured | |
|:--|:--|:--|:--|
| `b-hook-sync` passes against `a-webapi-steps` | the same list | 814 / 814 (48K), 832 / 832 (Pentagon) | met |
| `c-hook-frame`, `d-deferred` | gross exact, interrupt return not exact | both exact once the hits are sorted; wrong without (the rings of different taps are drained one after another) | different reason, see below |
| run speed, demo, `b` | within 5 % of no taps | master: 1.7-2x slower; with the two gap patches as fast as no taps | met only patched |
| one hit, `b` | ≤ 2 µs | master: 14.7 µs (quiet), 50 µs (loaded); patched: 2.4 µs a call (loaded) | met only patched, close |
| one hit, `e` | ≤ 50 ns | not measurable this way: the count-only run had debug mode on | not answered; `sync-empty` patched is the floor (≈ 0 on the demo) |
| run speed, `a` | 2-8 s per 100 frames | 3.2 s per 100 frames, 3.2-3.4 ms a stop | met |

## What the taps get right

- **The clock.** An exec tap is told at the start of the instruction at its address, before it runs, with the
  frame and the T-state of the frame: the same reading the reference takes after `POST /steps` stops at a
  breakpoint. Gross times, nesting and the interrupts' acknowledge come out the same with no stop.
- **The order (sync).** A sync hook runs inside the hit, so the hits of all taps arrive in machine order and the timer
  is a plain state machine, as in the reference.
- **Speed.** 57-63x the reference at the same exactness, with a timer that can stay on while the program runs.

## What they get wrong today

1. **A GC step on every hook call** (`luahookhost.cpp`, `lua_gc(L, LUA_GCSTEP, 0)`). Harmless for a hook without
   state; a hook that keeps any (every timer, counter or log does) pays a step proportional to its heap on each call:
   44 µs a call after 4700 kept tables, and a dense marker never finished. This is a defect of the hook host, not of
   the timer: every stateful frame or sync hook has it. Without the step the allocator's own incremental GC runs
   (5 µs a call here); with the generational collector, 2.4 µs, the same as an empty hook.
2. **No HALT idle while any exec tap is armed** (`Z80::IdleStepsInert`). A program that waits in HALT (most games and
   demos) then runs the HALT instruction by instruction. It costs every exec tap, also the haptic probes, the game
   events and the analyzers' taps: about 190 µs a frame on this demo, 1.7x. Debug mode already makes the right
   check (`ExecIsInertAt(pc)`: idle unless a breakpoint is at the HALT); exec taps can use the same.
3. **Hits out of order outside sync mode.** One callback is one tap, with its own ring. A frame callback runs at the
   frame end callback after callback, a deferred one when the script thread drains its ring: the entry of one marker
   comes after the exit of another. A timer on frame or deferred mode must keep the hits and sort them by global T;
   that is only safe after the last hit of a frame is known, which neither mode tells.
4. **Deferred cannot carry dense markers.** One script thread takes a few hundred thousand hits a second at most; a
   dense marker at full speed is over a million. The budget policy then suspends the callback (and its hits are
   lost), which is the right behaviour for the policy and the wrong one for a timer.
5. **The return from an interrupt is not an event.** The timer adds the return instruction's length to its start
   (`RETI` 14 T, `RET` 10 T). That is exact only when the handler's return and its stack are in uncontended memory,
   and it needs the handler's exits as addresses (a label in the demo; the ROM's `EI; RET` for an IM 1 program, found
   by disassembly). The reference needs neither: it sets a breakpoint on the interrupted address at run time, which a
   hook cannot do. `cpu_int` / `cpu_reti` machine events exist on the Next and the Sprinter only (their interrupt
   controllers).
6. **Results out of the hook state.** A hook publishes through `shared_set` (nil, boolean, number, string). The POC
   writes its passes as a JSON string and needs a one-off sync hook, run by a marker hit, to do it. A tool would
   want to read its statistics while the machine is paused, with no hit.
7. **A pause on a slow pass.** The reference's `--break-if` pauses the machine at the exit of the first slow pass.
   A hook has no call that stops the machine.
8. **Per-call base cost.** An empty sync hook costs 2.3-3.6 µs. POC 039 expected ≤ 0.5 µs for a trivial body in an
   own state; the difference is what each call builds (the hit table with a register sub-table, the count hook set
   and cleared) and, on master, the GC step.

## A native analyzer, for comparison

A native `section-timer` would sit on the same per-instruction check the taps use (a bit per address,
`MemoryBit(BRK_KIND_EXEC, pc)`), so it gains nothing on the instructions without a marker. On a marker hit it saves
the Lua call: tens of nanoseconds against 2.4 µs. That matters only for dense markers: 558 hits a frame are 1.3 ms a
frame on taps (6.5 % of a 50 Hz frame's 20 ms) and well under 0.1 ms natively. For markers around routines (the
demo: 9 a frame) the difference is 20 µs a frame.

What the native analyzer would get that the taps lack is gap 5: the CPU knows when it accepts an interrupt and can
see the return (the profile analyzer of POC 046 already follows interrupt entries and returns in its shadow stack).
That is better given to every tool as two machine events than built into one analyzer.

Closing gaps 1 and 2 is a few lines each and helps every tap and hook; 3, 6 and 7 are small API additions; 5 is the
one real piece of CPU work, and a native timer would need it too.
