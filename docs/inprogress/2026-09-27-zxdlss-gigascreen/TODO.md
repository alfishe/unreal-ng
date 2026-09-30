# TODO — ZX DLSS GigaScreen / Temporal Effects (2026-09-27)

**Status:** requirements settled, designs drafted; no code yet.

## Documents
- [requirements.md](requirements.md) — R-1..R-30
- [design-analysis.md](design-analysis.md) — analysis algorithm, case catalog
- [design-mixers.md](design-mixers.md) — mixer store, formula language, calibration
- [temporal-effects-manager.md](temporal-effects-manager.md) — one core manager, modes off/blend/adaptive/dlss
- [test-plan.md](test-plan.md) — inputs, generators, gates, goldens, benchmarks
- [prior-art.md](prior-art.md) — other emulators' de-flicker
- [optimization-ideas.md](optimization-ideas.md) — backlog + SIMD candidates
- [rollout.md](rollout.md) — phases P0–P10, T0–T4, flags, exit criteria
- [reference-across-the-edge.md](reference-across-the-edge.md) — recording, clip, first-pass effect map, golden clip candidates

## Next (value order, per rollout.md)
1. ~~Record the full Across the Edge TTD; first-pass effect map~~ (done 2026-09-28, prototype extractor in scratch). P0b: clip format + extractor in the tree; synthetic generator (periods 3–5 needed: the demo is period-2 material).
2. P0a: plane B capture + raw frame copy + `capture/screen/raw`, `capture/planeb`.
3. P0e: Temporal Effects Manager skeleton; `blend` mode ported with parity test; `adaptive` baseline.
4. P1 → P2: Metadata Manager core, then GigaScreen analyzer in observe-only mode; whole-demo statistics.

## Related
- [Look-Ahead Manager](../2026-09-27-lookahead-manager/TODO.md)
- [Metadata Manager](../2026-09-27-metadata-manager/TODO.md)
- Roadmap: [04 §3.1](../2026-09-21-roadmap/04-zxdlss-semantic-layer-and-multiplayer.md)
