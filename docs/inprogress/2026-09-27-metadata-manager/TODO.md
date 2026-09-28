# TODO — Metadata Manager (2026-09-27)

**Status:** requirements + design drafted; no code yet.

## Documents
- [requirements.md](requirements.md) — MD-1..MD-20
- [design.md](design.md) — layers, records, tags, blobs, serialization, identification
- [trigger-integration.md](trigger-integration.md) — timing via triggers, prerequisite chain, pack authoring

## Next (value order)
1. P1: store, coalescing, blobs, binary + JSON/YAML, WebAPI.
2. T0 → T1: expression evaluator, frame-level conditions.
3. T2 → T3: conditional breakpoints Phase 1, trigger engine + metadata additions (TR-7..TR-13).
4. P8/P9: identification, trigger-based packs, authoring tool, Across the Edge pack.

## Related
- [Conditional breakpoints](../2026-08-17-conditional-breakpoints/TODO.md), [expression evaluator](../2026-08-26-expression-evaluator/TODO.md), [roadmap 02 triggers](../2026-09-21-roadmap/02-capability-registry-and-triggers.md)
- Rollout: [../2026-09-27-zxdlss-gigascreen/rollout.md](../2026-09-27-zxdlss-gigascreen/rollout.md)
