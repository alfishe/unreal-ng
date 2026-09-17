# TODO — Debugger enhancements / xpeccy parity (2026-08-26)

**Status:** partially done — the analysis/proposal phase is complete; almost
none of the parity feature list itself is implemented (verified 2026-09-16: no
address history, marked addresses, port watch or heatmap code in
`unreal-qt/src/debugger/`).

## Progress
- Full analysis set: [proposal.md](proposal.md) (phased plan),
  [features-parity.md](features-parity.md), [xpeccy-comparison.md](xpeccy-comparison.md),
  [ui-mockups.md](ui-mockups.md).
- Adjacent work landed since the proposal, covering some Phase 4 ground by
  other routes: ULA beam widget (`ulabeamwidget.*`), memory-page map widgets
  (`memorypageswidget.*`, `memorypagesviswidget.*`), FDC status widget.
- Expression evaluator / breakpoints split into their own folders
  (`../2026-08-26-expression-evaluator/`, `../2026-08-26-breakpoint-enhancements/`).

## Remaining (value order)
1. **Phase 1 disassembly navigation** — address history back/forward, go-to-PC
   hotkey, follow-operand on Enter, marked addresses (highest daily-use value).
2. **Phase 3 flags/interrupts** — clickable flag checkboxes, IFF1/IFF2 + ISR
   address display (feeds IRQ breakpoints later).
3. **Phase 2 stack widget** — 9 entries, single-click jump, return-address
   hints.
4. **Phase 5 port watch** — PortRegistry list with value-change highlighting
   (note: `/ports` automation surface with tags already exists — build on it).
5. Signal indicators (DOS/ROM/INT), PreferenceManager abstraction.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — debugger parity (T4).
