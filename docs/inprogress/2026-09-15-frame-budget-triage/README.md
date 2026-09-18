# Frame budget triage — covox demo, 15–20 ms tail on a fast CPU

Status: analysis of uncommitted diagnostics (see `01-evidence-and-deductions.md`),
hypothesis ranking (`02-hypothesis-matrix.md`), and a no-code-change runbook
(`03-triage-runbook.md`). No source files were modified for this triage.

## Problem statement

Running the covox scroller demo (`testdata/sound/covox/scroller_by_demarche.sna`)
on Pentagon 128 (frame budget 20480 µs = 71680 T-states @ 3.5 MHz), the Qt
frontend reports a recurring tail of 15–20 ms frames (~5–8 % of every 256-frame
window) with occasional >20 ms overruns, on a machine whose sustained speed is
fine (10M-LCG probe stable at ~9.1 ms). Question: where precisely does the time
leak during those frames?

## TL;DR of the current evidence

1. **The pasted three log lines do NOT contain the new cache probe.** The
   working-tree format string (core/src/emulator/mainloop.cpp, `Run()` summary
   print) includes `cache probe at frame start: worst frame %u us, avg %u us`
   between `window avg:` and `env:`. The pasted lines jump straight from
   `window avg: ... ;` to `env: cpu#...`. They were produced by a pre-probe
   binary (stale build or an older paste). The "eviction vs in-frame" decision
   the probe was built for has **no data yet** — first action is a verified
   rebuild + re-run (runbook R1).
2. In the worst frames the thread is **on-CPU for ~99 % of the wall time**
   (`thread cpu` ≈ `wall`: 18323/18233, 18585/18385, 20899/20754). The thread
   is not preempted and not blocked. The time is spent executing.
3. **All four cpu-stage components inflate by the same ~3.5–3.8× factor
   simultaneously** in the worst frames (z80, step-screen, step-io, step-sound
   vs their window averages). That is a systemic, core-level slowdown, not a
   subsystem regression.
4. Per-frame emulated work is **fixed** (71680 T-states); the instruction count
   between worst frames varies only 1.45× (12219 → 17727) while their wall time
   varies 1.14×. The worst frames execute roughly the same workload at
   ~1/3.6 of the average per-instruction speed.
5. Leading hypotheses: **SMT-sibling contention during the whole frame** and
   **wake-onto-cold/parked/low-frequency core** (the thread sleeps ~15 ms per
   frame on a waitable timer and migrates cores between windows: #2 → #10 →
   #6). Distinguishing them from cache eviction and from each other is exactly
   what the runbook does.
6. One cheap experiment (R5) reinterprets all existing data: calibrate whether
   `QueryThreadCycleTime` is frequency-scaled or TSC-like. If it is
   frequency-scaled, `cpu ≈ wall` in the worst frames **already excludes
   frequency throttling** and points at IPC collapse (SMT/cache contention).

## Immediate next steps (ordered)

| # | Action | Status | Answers |
|---|--------|--------|---------|
| R1 | Rebuild, confirm the `cache probe at frame start` segment actually prints, re-run covox demo, capture 3+ windows | In Progress (unreal-qt restarted) | Eviction-at-entry vs warm-entry |
| R2 | Run `BM_CovoxDemoFrame` / `BM_CpuSpeedProbe` (staged benchmarks, no GUI/audio/pacing) | **COMPLETED** (see `04-benchmark-evidence.md`) | Core takes **2.97 ms/frame** (14.5% budget); core exonerated |
| R5 | Power-saver calibration of `QueryThreadCycleTime` semantics | Pending | Is frequency already excluded? |
| R3/R4 | Affinity pinning matrix; power-plan matrix | Pending | Wake/migration vs sibling contention |
| R6 | WPR/WPA CPU trace correlated to the worst frames | Pending | Ground truth: core, frequency, sibling, DPC |

## Files

- `01-evidence-and-deductions.md` — the three log lines parsed field-by-field,
  derived metrics, the deduction chain, and instrumentation caveats.
- `02-hypothesis-matrix.md` — ranked hypotheses with signatures and the
  discriminating test for each.
- `03-triage-runbook.md` — concrete no-code-change experiments (R1–R9) and the
  proposed follow-up instrumentation (P1–P8, explicitly *not* applied).
- `04-benchmark-evidence.md` — empirical execution of R2: headless vs in-app
  timing, core overheads, and machine baseline probes.
- `05-optimization-roadmap.md` — concrete optimization roadmap: hybrid pacing,
  SMT isolation, per-step clock gating, and presentation decoupling.
- `06-overrun-root-cause-analysis.md` — comprehensive deep dive: HALT step
  explosion (17.9k steps), the async peripheral synchronization invariant (FDC,
  AY, TSFM, MoonSound, GS, Tape, Screen), 50/60 Hz beat-frequency collisions, and
  why adaptive pacing already works.

## Scope note

The uncommitted diff also contains unrelated staged work (filter decimator,
turbosound benchmarks/tests) and two frontend fixes already relevant here:
audio-callback logging deferred to the GUI thread
(unreal-qt/src/emulator/soundmanager.cpp) and enabling
`SUBMODULE_CORE_MAINLOOP` logging (unreal-qt/src/mainwindow.cpp). This triage
touches none of it.
