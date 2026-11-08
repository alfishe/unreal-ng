# TODO — Breakpoint enhancements (2026-08-26)

**Status:** ranges and hit counters done (2026-10-04, branch `bp-matching`, through the hot-path
matching design of
[../2026-08-17-conditional-breakpoints/hotpath-matching-design.md](../2026-08-17-conditional-breakpoints/hotpath-matching-design.md));
conditions and IRQ breakpoints open. [design.md](design.md) covers all four.

## Progress
- Design complete (315 lines): data model, evaluation cost analysis, UI plan.
- Prerequisite hot-path work landed (Phase 0 of the conditional-breakpoints
  track, 12–18× miss-path speedup) so range/counter checks have a cheap place
  to hook.

## Remaining (value order)
1. ~~Address-range breakpoints~~ — done 2026-10-04 (`address_end`, CLI `bp A-B`).
2. ~~Hit counters~~ — done 2026-10-04 (`hit_count`, policies equal / at_least / multiple).
3. **Conditions** — implemented via the expression evaluator track
   (`../2026-08-26-expression-evaluator/` + `../2026-08-17-conditional-breakpoints/`).
4. **IRQ breakpoints** — break on interrupt entry (needs IM1/IM2 vector
   resolution; pairs with debugger parity IFF1/IFF2 work).
5. **Triggers and actions in conditional debugging** (owner, 2026-11-08) — check what exists, implement what is
   missing. The debugger should make it trivial to script a sequence such as "wait N frames, press 1, press
   Enter": triggers beyond a PC / memory / port hit (a frame count, a T-state, a condition becoming true, the
   previous step done) and actions beyond a stop (press / release a key, type text, resume, run N frames, fire
   the next step). Survey first: the condition engine, breakpoint taps and their callbacks
   (`../2026-10-26-breakpoint-taps/`), `type_input` / `run_frames` on the planes, what other debuggers offer;
   then a design, the same on every plane.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — item #6 (T2) covers the
  condition slice; ranges/counters are T4.
