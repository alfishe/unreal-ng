# 050 — conclusions

1. **G1, exact on taps: yes.** Exec taps on the markers plus one Lua chunk in sync mode give the reference's passes
   exactly, pass for pass, on a 48K and a Pentagon: gross, nesting, interrupts and net time, with the machine never
   stopped. Frame and deferred modes are exact only if the timer sorts the hits itself (gap 3). The interrupt return is
   computed from the handler's return instruction, exact in uncontended memory and given the handler's exits (gap 5).
2. **G2, cost.** 57-63x faster than the reference (0.1 s against 6.3 s for 200 frames). On master the taps cost
   1.7x on the demo whatever the hit count (no HALT idle) and 15-50 µs a hook call (a GC step per call over the
   hook's growing state). With both fixed (temporary patches): no measurable cost on the demo's markers, 2.4 µs a hit
   on a dense marker, 1.3 ms a frame at 558 hits a frame. A native timer would gain only that per-hit Lua call.
3. **G3, gaps.** Eight, listed in [analysis.md](analysis.md): two defects that cost every tap or hook (the per-call GC
   step, no HALT idle with exec taps), no machine order across callbacks outside sync mode, deferred mode too slow for
   dense markers, no interrupt-return event on most machines, no way to read a hook's results without a hit, no pause
   from a hook, a per-call base cost above POC 039's estimate. None needs a section-timer analyzer; the interrupt
   events would be needed by one too.

**Decision rule** (README): `b-hook-sync` is exact, and with the two defects fixed its per-hit cost keeps a dense
marker at a few percent of a real-time frame. The criterion for the tap-based tool is met, subject to the fixes.

## After the native analyzer (same day)

4. **The native analyzer, measured.** `section-timer` (exec taps of its own sink, the CPU's interrupt events)
   matches the reference pass for pass on both models, with no handler exits given, and costs nothing measurable
   even at 558 hits a frame; the Lua timer, with both fixes, costs nothing on routine-level markers and 3.7x on the
   dense one. The native one also has none of Lua's limits: no instruction budget or suspension policy, no hook
   state to read through `shared_set`, no per-call cost to watch.
5. **Revised recommendation.** The interrupt events and the two fixes are in master and serve both. The section
   timer is worth shipping natively (an analyzer on every plane), with the Lua way kept for ad-hoc timers a user
   writes. See [proposal.md](proposal.md).
