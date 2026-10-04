# TODO — Conditional breakpoints (2026-08-17)

**Status:** partially done — Phase 0 (hot-path retrofit) shipped; the condition
engine itself (Phase 1) is not implemented.

## Progress
- **Phase 0 complete (2026-08-19):** breakpoint miss-path cost cut 12–18× via
  the hot-path walkthrough/retrofit ([hotpath-walkthrough.md](hotpath-walkthrough.md),
  [performance.md](performance.md)); regressions green.
- Design for the full feature complete: [design.md](design.md),
  [implementation-plan.md](implementation-plan.md) (slices 1a–1h), DeZog
  condition protocol mapped ([dezog-integration.md](dezog-integration.md),
  [research/dezog-dzrp.md](research/dezog-dzrp.md)), prior-art survey
  (SpecEmu/Spectaculator/Unreal/zx-m8xxx) in [research/](research/).
- No `bpcondition.*` / condition code exists in `core/src` (verified 2026-09-16).

- **Hot-path matching design (2026-10-03):** [hotpath-matching-design.md](hotpath-matching-design.md) -
  exact addresses, ranges, physical / slot-bound pages, port masks, hit counts on painted tables and an L1
  filter; measured in [PoC 022](../../../tools/poc/022-breakpoint-matching/README.md) (2026-10-04: CPU
  addresses and ranges 0.42-0.54 ns above unarmed, flat to 10 000; today 1.6-3.7; filter `globalbits`).
  Next: integrate into `BreakpointManager` with the inline call-site check, tests, A/B on the emulator. The `MapZ80AddressToPhysicalPage` cache-page prerequisite is fixed
  (master 7eca03436).

## Remaining (value order)
1. **Phase 1 slices 1a–1h** — expression-conditioned breakpoints: core engine
   (`bpcondition.{h,cpp}`), WebAPI + MCP + CLI + Lua/Python surfaces, debugger
   UI, tests per plan. Gated on the expression evaluator
   (`../2026-08-26-expression-evaluator/`).
2. DeZog `conditionBreakpoints` parity once the engine exists.
3. Hit counters / address ranges — split out to
   `../2026-08-26-breakpoint-enhancements/`.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — expression evaluator +
  conditional breakpoints (T2, item #5).
