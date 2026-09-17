# TODO — Analysis tools beyond TRDOS analyzer (2026-01-14)

**Status:** partially done — the analyzer framework and the TRDOS analyzer shipped;
the remaining analyzers are design-only.

## Progress
- Analyzer architecture landed (analyzer manager + WebAPI/CLI analysis surface).
- TRDOSAnalyzer shipped and verified — tracked in
  [`../2026-01-21-trdos-analyzer/DONE.md`](../2026-01-21-trdos-analyzer/DONE.md).
- Data-collection extensions largely delivered by the automation program
  (memory-management commands, port tracing, call trace, opcode profiler).

## Remaining (value order)
1. **Interrupt analyzer** — [interrupt-analyzer.md](interrupt-analyzer.md) design
   (699 lines), zero code. Highest remaining value: explains IRQ storms/latency
   in automation triage sessions.
2. **Routine classifiers** — [routine-classifiers.md](routine-classifiers.md)
   design (984 lines): library/SMC/self-modifying classification. No code.
3. **Beam-to-execution correlation** — two designs
   ([beam-to-execution-correlation.md](beam-to-execution-correlation.md),
   ...-alt.md); no code. Pairs naturally with the ULA beam widget that exists.
4. **Block segmentation** — [block-segmentation.md](block-segmentation.md);
   overlaps `../2026-02-23-realtime-monitoring/` (also TODO).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — segmentation/analyzers items (T4).
- Raw session data captured in [session-ses_4416.md](session-ses_4416.md) is the
  natural test corpus for any of the above.
