# TODO — Shared-memory coherency research (2026-08-27)

**Status:** research complete, implementation deliberately deferred.

## Progress
- [coherency-research.md](coherency-research.md) (213 lines): analysis of the
  shared-memory communication model between the emulator thread and
  consumers (WebAPI snapshots, screen capture, TTD), the coherency windows
  that exist today and where they can tear.
- Findings folded into practice where cheap: snapshot/publish discipline in
  the screen pipeline and TTD present-queue (`CopyPresentedFramebuffer`)
  follow the researched patterns.

## Remaining
1. **Decision: formalize or stay opportunistic** — implement a documented
   coherency contract (seqlock/versioned snapshots) for cross-thread readers,
   or record the current opportunistic rules as the contract. No consumer is
   currently blocked on this.
2. If a shared-memory automation bridge (out-of-process screen/state feed) is
   ever built, this research is its prerequisite — tracked as PLAN T4.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — shared-memory bridge (T4).
