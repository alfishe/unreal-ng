# 050 — proposal

**Revised after the native measurement: ship the native `section-timer` analyzer. Shipped.** Steps 1-3 below
landed on master (`a6f99c20d`, `5c2351274`); the native analyzer is exact and costs nothing measurable even on a
dense marker, 4.7x faster than the Lua timer there (measurements, last section). It is now `SectionTimerAnalyzer`
on every automation plane (CLI `sectiontimer`, `POST` / `GET /section-timer` and its WebSocket call, MCP
`analyze_performance` `section_timer`, Lua `section_timer_*`, Python `emu.section_timer_*`), its results readable at
any time, a stop at the exit of a slow pass (`break_if`), and `data/examples/01-section-timing/profile_sections.py
--native` using it (docs: `docs/emulator/design/debugger/breakpoints-and-traces.md` section 10). The Lua timer
(`python/sections.lua`) stays here as the record. The text below is the first proposal, kept for the record.

**First proposal: drop the native `section-timer` analyzer. Ship section timing as a tap-based tool, after fixing
the two defects every tap and hook pays for.**

## Steps, in order

1. **Fix the per-call GC step** in `LuaHookHost::Call` (gap 1): no forced step per call; the hook state on the
   generational collector, or a step per frame end. A test with a hook that keeps state (per-call time flat over
   100k calls). Every stateful frame / sync hook gains (game events, haptics `if:` sources, user hooks).
2. **Allow the HALT idle with exec taps elsewhere** (gap 2): `Z80::IdleStepsInert` refuses the idle only when a tap is
   at the HALT's address, as debug mode already does. A test that an exec tap at the HALT still hears it and one
   elsewhere keeps the idle; an A/B per the performance guidelines. Every exec tap gains (haptic probes, game
   events, analyzers' taps).
3. **Interrupt events on every Z80 machine** (gap 5): `cpu_int` at acceptance with the T-state before the
   acknowledge, and an interrupt return (the first instruction start at the interrupted address with the SP of the
   acceptance), for every model, not only the Next's and the Sprinter's controllers. Exact interrupt accounting for
   any handler, no handler exits needed, contention included. Shares the detection with the profile analyzer's shadow
   stack (POC 046).
4. **Small API additions for tools on hooks** (gaps 6, 7): run a hook-state function on demand while paused (read
   results without a hit), and a hook call that pauses the machine at the current instruction (the `--break-if` of
   the reference).
5. **The tool**: a Lua library for the hook state (the chunk of `python/sections.lua`, tidied) and
   `data/examples/01-section-timing` gaining a `--taps` mode that registers it, runs at full speed and writes the same
   report; the stepping mode stays for machines or cases the taps do not cover. MCP / WebAPI expose it through the
   scripts, as callbacks already are.

Steps 1 and 2 are independent fixes, worth doing even if the tool waits. Before adoption, the A/B of step 2 is
repeated on a quiet host (this POC's host was loaded, see the measurements).

## Not proposed

- Frame or deferred mode for the timer: they lose machine order across callbacks (gap 3); sync is exact and, fixed,
  cheap. A multi-address tap (one callback, one ring for a set of addresses) would remove gap 3 if a frame-mode tool
  is wanted later.
- Lowering the empty-hook cost (gap 8) below 2.4 µs: worth a look when dense markers matter (a reused hit table, the
  register table built on first use), not needed for the timer.
