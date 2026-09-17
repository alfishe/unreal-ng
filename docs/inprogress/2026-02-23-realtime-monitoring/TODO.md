# TODO — Realtime monitoring / segmentation (2026-02-23)

**Status:** not started (design + accuracy analysis only). No segmentation
engine, classifier or widget code exists on master (verified 2026-09-16: no
`GenericTagClassifier` / `InterruptClassifier` / segmentation classes in
`core/` or `unreal-qt/`).

## Progress
- [segmentation_analysis.md](segmentation_analysis.md) — idle-loop ground
  truth (48K/128K), two concrete classifier bugs specified (wrong sysvars
  range `$5B00–$5CBF`, model-blindness) with the exact required fix.
- [segmentation_widget_design.md](segmentation_widget_design.md) — Qt widget
  design (222 lines).

## Remaining (value order)
1. **Segmentation engine** — model-aware `GenericTagClassifier` fix +
   classification pipeline (execute/write/read coverage → CODE/DATA/UNKNOWN
   blocks), per the analysis.
2. **Live segmentation widget** — realtime CODE/DATA overlay in the debugger
  built on the engine; design ready.
3. Feeds from memory tracker / TTD — long-run segmentation over recorded
   sessions (natural follow-on, not designed yet).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — segmentation widget + analyzers
  items (T4).
- Related design: [`../2026-01-14-analyzers/block-segmentation.md`](../2026-01-14-analyzers/block-segmentation.md).
