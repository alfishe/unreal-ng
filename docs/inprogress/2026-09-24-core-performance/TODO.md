# TODO — Core performance and gating review

**Status:** triaged, mostly open. This folder holds one large review document
([unreal-ng-core-perf-and-gating-review.md](unreal-ng-core-perf-and-gating-review.md))
covering ~50 findings across CPU hot path, peripheral gating, sound-device
quiescence, port I/O and feature-gating defects (sections A–I). Priority
order is §I in the review doc itself.

## Done and verified

- **E1 — TurboSound `handleFrameEnd()` called twice per frame.** Fixed in
  `soundmanager.cpp` (removed the redundant second call at the former
  `:652`, kept the first which drains TSFM's word queues before the mix).
  Verified with a new regression test,
  `core/tests/emulator/sound/audio_activity_notification_test.cpp`
  (parameterized AY/FM), confirmed to catch the exact duplicate-post
  regression when the removed call is reintroduced. Full suite green
  (3299/3299). **Not yet committed** — working tree only as of 2026-09-24.

## Everything else in the review

Still open — see the review doc's own executive summary (§0) and suggested
order of work (§I) for the full, prioritized list (re-landing the lost
2026-08-04 perf round in §A, sound-device quiescence in §B, the per-
instruction catch-up sync rework in §C, port I/O path fixes in §D, the
remaining §E per-frame overheads, and the feature-gating defects in §F).
