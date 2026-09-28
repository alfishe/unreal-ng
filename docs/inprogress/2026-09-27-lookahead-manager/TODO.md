# TODO — Look-Ahead Manager (2026-09-27)

**Status:** requirements + design drafted; no code yet.

## Documents
- [requirements.md](requirements.md) — LA-1..LA-17
- [design.md](design.md) — shadow instance, resync, divergence monitor, consumers
- [prior-art.md](prior-art.md) — xpeccy-plus, RetroArch, Mesen2, Spectral, BizHawk

## Next (value order)
1. P0c: `MachineSnapshot` raw state copy + run-restore-run completeness tests per model.
2. P0d: context role with per-context sinks; emulated-time RTC/CMOS clock.
3. P5: manager with shadow worker, resync policy, divergence monitor.
4. P6/P7: GigaScreen two-sided window; Game Mode run-ahead display.

## Related
- Game Mode §7: [../2026-09-24-core-performance/unreal-ng-input-latency-and-game-mode.md](../2026-09-24-core-performance/unreal-ng-input-latency-and-game-mode.md)
- Rollout: [../2026-09-27-zxdlss-gigascreen/rollout.md](../2026-09-27-zxdlss-gigascreen/rollout.md)
