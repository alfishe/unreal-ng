# 038 — conclusions

| Goal | Answer |
|------|--------|
| G1 exact on a clone | **Yes, with two conditions.** No turbo on the replaying machine, and the baseline interval excluded or fixed. Under those conditions: 0 mismatches over every stripe of three workloads (Pentagon program, AY demo, 48K with recorded keyboard input), and every answer equals today's verbs and the frame cache. |
| G2 speed | One clone replays 4-12x faster than today's in-place replay (0.23-0.8 ms vs 2.1-2.7 ms a frame). N clones scale linearly to the performance cores: 31,000 frames/s at 8 clones on W1, 60,000 at 16. The agent's W1 question over 50,000 frames takes 1.96 s including setup, against more than 127 s of verbs today (65x). |
| G3 observers | Breakpoint taps on the clone give the probe semantics (pages, ranges, port masks) for exec, read, write and port probes at +3-6 %. The M1 hook gives a PC profile at +1-6 %. The old value of a write comes from a per-page snapshot of the range at the stripe start, kept up to date by each write. Every observer of a question runs in one pass. |
| G4 cost | 35-77 MB per clone; create + load 0.1-0.3 s for 8 clones. The live machine slows by 4-5 % with 8 clones and by 24 % with 16 on a 16-performance-core Mac. |

**Criteria:** all met. The memory criterion is borderline on one cell (W2 at N=1: 72.0 MB against 71.3 MB).

**Verdict: go.** Build the replay query engine on clones, with the turbo and baseline fixes as prerequisites.

Engine findings:

1. **The baseline interval was not exact.** Replaying from the baseline checkpoint (taken inside a frame) gave
   three different TSFM time fields at the next checkpoint: both FM clock phases and the render cursor. Fixed
   2026-10-28 (the pending clock adoption is stored).
2. **Host settings made a replay inexact.** The sound off, LQ and turbo change the TSFM render loop, whose position
   is TTD state. Fixed 2026-10-28: the render mode is pinned to the recording's while the machine is bound to a
   timeline. After the fix all 15 combinations (W1-W3 x replaying with the recorded settings, sound off, LQ,
   turbo, turbo with audio) have 0 mismatches, the baseline interval included.
3. **A public seek costs a frame.** A public `SeekTo` to a frame start replays the previous frame for the picture.
   An engine that seeks internally must skip the presentation.

Both fixes: [TTD analysis feedback](../../../docs/inprogress/2026-10-28-ttd-analysis-feedback/README.md#engine-fix-turbosound-fm-replays-on-any-host-poc-038-findings-f1-f2).
