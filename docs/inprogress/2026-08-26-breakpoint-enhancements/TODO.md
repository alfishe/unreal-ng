# TODO — Breakpoint enhancements (2026-08-26)

**Status:** not started (design only). [design.md](design.md) covers address
ranges, hit counters, conditions, IRQ breakpoints; no corresponding code
exists in `core/` (verified 2026-09-16 — only the condition design overlaps
with `../2026-08-17-conditional-breakpoints/`).

## Progress
- Design complete (315 lines): data model, evaluation cost analysis, UI plan.
- Prerequisite hot-path work landed (Phase 0 of the conditional-breakpoints
  track, 12–18× miss-path speedup) so range/counter checks have a cheap place
  to hook.

## Remaining (value order)
1. **Address-range breakpoints** — one range instead of N points; highest
   user value (ROM/RAM bank watch).
2. **Hit counters** — break after N hits; trivial once ranges exist.
3. **Conditions** — implemented via the expression evaluator track
   (`../2026-08-26-expression-evaluator/` + `../2026-08-17-conditional-breakpoints/`).
4. **IRQ breakpoints** — break on interrupt entry (needs IM1/IM2 vector
   resolution; pairs with debugger parity IFF1/IFF2 work).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — item #6 (T2) covers the
  condition slice; ranges/counters are T4.
