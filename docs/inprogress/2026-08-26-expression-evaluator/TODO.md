# TODO — Expression evaluator (2026-08-26)

**Status:** not started (design only). No `ExpressionEvaluator` code exists in
`core/src` (verified 2026-09-16).

## Progress
- [design.md](design.md) complete (324 lines): C-like expression language
  (registers, flags, memory derefs, labels, arithmetic/comparison), tokenizer /
  parser / evaluator split, integration points for watch, conditional
  breakpoints and DeZog conditions.
- Consumer waiting on it: `../2026-08-17-conditional-breakpoints/` Phase 1
  (slices 1a–1h) and the debugger watch/UI surfaces.

## Remaining (value order)
1. **Core evaluator** (`expressionevaluator.{h,cpp}`): parse → evaluate →
   value/error channel, per design; unit tests with the design's grammar
   examples.
2. **Surface #1: conditional breakpoints** — the design's first consumer and
   the reason this ranks in PLAN T2 (see `../PLAN.md` item #6).
3. **Surface #2: watch expressions** in the debugger (evaluate-on-break,
   change highlighting).
4. **Surface #3: DeZog condition expressions** (DZRP parity).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — item #6 (T2).
- Hot-path cost budget already established by the conditional-breakpoints
  performance work — evaluate on break only, cache per-stop.
