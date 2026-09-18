# Evidence and deductions — covox demo frame-budget tail (2026-09-15)

Source: three consecutive `Frame time:` summary lines from unreal-qt running
the covox demo (Pentagon 128, budget 20480 µs), captured at 15:21:10 /
15:21:15 / 15:21:20 (one 256-frame window ≈ 5.24 s each). Produced by the
uncommitted diagnostics in `core/src/emulator/mainloop.cpp` / `mainloop.h`.

## The raw evidence

```
[15:21:10.069.852] Frame time: 0 of 256 frames over budget, max 18323 us, max period 21346 us
  (budget 20480 us); stage max: cpu 17956, video 134, sound 394, other 91 us;
  cpu split max: z80 4558, step-screen 4673, step-io 2491, step-sound 6375 us (12219 steps);
  worst frame #5: wall 18323 us, thread cpu 18233 us (nominal 3.7 GHz);
  histogram <5/5-10/10-15/15-20/>20 ms: 197/32/7/20/0;
  window avg: cpu 4981, step-screen 1354, step-io 670, step-sound 1747 us;
  env: cpu#2 prio 1, 10M-LCG probe 9230 us, clock read 35 ns; previous log write took 508 us

[15:21:15.313.229] Frame time: 0 of 256 frames over budget, max 18585 us, max period 21462 us
  (budget 20480 us); stage max: cpu 18265, video 88, sound 264, other 90 us;
  cpu split max: z80 4387, step-screen 4932, step-io 2661, step-sound 6285 us (13956 steps);
  worst frame #234: wall 18585 us, thread cpu 18385 us (nominal 3.7 GHz);
  histogram <5/5-10/10-15/15-20/>20 ms: 187/45/10/14/0;
  window avg: cpu 5190, step-screen 1421, step-io 732, step-sound 1804 us;
  env: cpu#10 prio 1, 10M-LCG probe 9100 us, clock read 35 ns; previous log write took 639 us

[15:21:20.556.353] Frame time: 1 of 256 frames over budget, max 20899 us, max period 21479 us
  (budget 20480 us); stage max: cpu 20445, video 106, sound 299, other 66 us;
  cpu split max: z80 4566, step-screen 5644, step-io 3278, step-sound 7312 us (17727 steps);
  worst frame #30: wall 20899 us, thread cpu 20754 us (nominal 3.7 GHz);
  histogram <5/5-10/10-15/15-20/>20 ms: 192/39/9/12/4;
  window avg: cpu 5503, step-screen 1504, step-io 854, step-sound 1907 us;
  env: cpu#6 prio 1, 10M-LCG probe 9062 us, clock read 35 ns; previous log write took 610 us
```

## Critical observation: the cache probe is absent from these lines

The current working-tree format string (uncommitted diff, `Run()` in
core/src/emulator/mainloop.cpp) prints, between `window avg:` and `env:`:

```
"cache probe at frame start: worst frame %u us, avg %u us; "
```

The pasted lines go directly from `window avg: ... step-sound 1747 us;` to
`env: cpu#2 ...`. Therefore the binary that produced these lines predates the
cache-probe capture/print (stale build or an older run). Consequences:

- The eviction-vs-in-frame discriminator has produced **no measurements yet**.
- None of the three lines can be read as "probe warm" or "probe cold".
- Runbook R1 (rebuild + verify the segment exists) must precede any
  cache-related conclusion.

## Derived metrics

z80 avg (derived) = window avg cpu − (step-screen + step-io + step-sound avgs).
Inflation = window max / window avg. On-CPU % = worst thread cpu / worst wall.
T-states per instruction = 71680 / steps (Pentagon frame is a fixed T-state
budget; `OnCPUStep` fires once per Z80 instruction, emulator.cpp).

| Metric | Window 1 | Window 2 | Window 3 |
|---|---|---|---|
| overruns (>20480 µs) | 0/256 | 0/256 | 1/256 |
| max wall µs | 18323 | 18585 | 20899 |
| worst frame on-CPU % | 99.5 % | 98.9 % | 99.3 % |
| cpu stage avg → max | 4981 → 17956 (**3.61×**) | 5190 → 18265 (**3.52×**) | 5503 → 20445 (**3.71×**) |
| z80 avg → max | ~1210 → 4558 (**3.77×**) | ~1233 → 4387 (**3.56×**) | ~1238 → 4566 (**3.69×**) |
| step-screen avg → max | 1354 → 4673 (**3.45×**) | 1421 → 4932 (**3.47×**) | 1504 → 5644 (**3.75×**) |
| step-io avg → max | 670 → 2491 (**3.72×**) | 732 → 2661 (**3.64×**) | 854 → 3278 (**3.84×**) |
| step-sound avg → max | 1747 → 6375 (**3.65×**) | 1804 → 6285 (**3.48×**) | 1907 → 7312 (**3.83×**) |
| steps (window max) | 12219 | 13956 | 17727 |
| T-states / instruction | 5.87 | 5.14 | 4.04 |
| per-step cost in worst frame (cpu/steps) | 1.47 µs | 1.31 µs | 1.15 µs |
| video / sound / other max (µs) | 134/394/91 | 88/264/90 | 106/299/66 |
| frames in 15–20 ms / >20 ms bucket | 20 / 0 | 14 / 0 | 12 / 4 |
| env core # / LCG probe | #2 / 9230 µs | #10 / 9100 µs | #6 / 9062 µs |

Cross-checks that hold: z80+screen+io+sound maxima sum to the cpu max within
~40 µs (17997 vs 17956; different-frames effect, see caveat C1); 4 frames
≥20000 µs vs 1 overrun >20480 µs in window 3 means three frames landed in
[20000, 20480]; timestamps give a mean period of 5243 ms/256 = **20480 µs
exactly** — pacing holds the mean cadence; window-to-window LCG spread is
1.9 % (3.35–3.42 cycles/iteration at 3.7 GHz — full speed at probe time).

## Deduction chain

- **D1 — It is all inside the cpu stage.** video/sound/other maxima are ≤0.4 ms
  while worst walls are 18–21 ms. Presentation and audio enqueue are not where
  the time goes.
- **D2 — The thread is not preempted or blocked.** `thread cpu ≈ wall`
  (98.9–99.5 %) in all three worst frames. `QueryThreadCycleTime` only
  advances while the thread runs on a core, so the whole frame was spent
  executing (or stalling on memory while still on-CPU). Rules out scheduler
  starvation, blocking log/console I/O inside the frame, and DPC/ISR storms
  severe enough to preempt (those would show wall ≫ cpu).
- **D3 — The slowdown is systemic, not a subsystem.** All four cpu components
  inflate by 3.45–3.84× simultaneously, consistently across all three windows.
  A sound-engine pathology would inflate step-sound only; a screen regression
  step-screen only. A uniform factor means the core itself executed the code
  slower (frequency, SMT contention, cache/TLB pressure) — or the whole frame
  did more of everything.
- **D4 — It is (mostly) not "more work".** Every frame emulates exactly 71680
  T-states; `CPUFrameCycle()` always runs to the frame limit, and the pacing
  re-anchor forbids multi-frame catch-up inside one `RunFrame`. The instruction
  count can only move with instruction-length mix (4.0–5.9 T-states/instr
  observed, a 1.45× spread), yet the slowdown is 3.5×. Per-step cost in the
  worst frames (1.15–1.47 µs) vs the window-average per-step cost
  (~0.34–0.37 µs at plausible avg steps — **not reported**, see P2) implies
  execution at ~1/3.6 speed. Residual uncertainty: avg steps are not printed,
  so "worst frames also happen to be the most instruction-dense frames" cannot
  be fully excluded yet — but it cannot produce a uniform 3.6× either.
- **D5 — No persistent throttling.** LCG probe 9062–9230 µs across windows on
  three different cores; spread under 2 %. Whatever slows the worst frames is
  transient and frame-local, not a sustained machine state.
- **D6 — Every frame starts from a sleep, possibly on a different core.** The
  pacing wait (`TimeHelper::WaitUntilPrecise`, core/src/common/timehelper.cpp)
  is a waitable-timer sleep in ≤4 ms chunks with **no terminal spin**; average
  duty cycle is only ~25 % (5 ms work / 20.48 ms period), so the thread sleeps
  ~15 ms between frames, long enough for the core to idle/park and for L1/L2
  to be repopulated by whatever runs there. The thread ran on cores #2, #10
  and #6 in the three windows. The wake core, its frequency state, its cache
  state and its SMT-sibling load are the uncontrolled variables per frame.
- **D7 — The eviction discriminator is unmeasured.** See the critical
  observation above: no probe data exists in these lines.
- **D8 — Pacing absorbs the tail.** Max period 21.3–21.5 ms (≤1.05× budget)
  despite 18–21 ms frames: the next sleep shortens (re-anchor allows one frame
  of catch-up; DRC absorbs the residual). The direct risk of a >20 ms frame is
  the audio-ring underrun path, not cadence collapse.
- **D9 — Known fixed diagnostic costs, all absorbed by pacing:** per-step
  instrumentation (4 × `steady_clock::now()` per instruction, ~35 ns each,
  ~12–18 k steps) ≈ 1.5–2 ms/frame uniformly added to every cpu-stage number
  (averages and maxima alike — ratios D3/D4 are unaffected, absolute values
  are inflated); the 10M-LCG probe (9.1 ms) and the summary log write
  (0.5–0.6 ms) once per 256-frame window.

## Field semantics (from the uncommitted diff)

| Field | Meaning | Source |
|---|---|---|
| `max N us` | longest `RunFrame()` wall time in window (`duration1`) | `measure_us` around RunFrame |
| `max period` | longest start-to-start gap (skips >1 s pause artefacts) | `betweenIterations` |
| `stage max: cpu` | `ExecuteCPUFrameCycle()` = Z80 + per-step screen/io/sound hooks, wall clock | `_frameCpuUs` |
| `stage max: video` | batch render + `LatchFramebuffer()` | `_frameVideoUs` |
| `stage max: sound` | `SoundManager::handleFrameEnd` | `_frameSoundUs` |
| `stage max: other` | RunFrame remainder (frame start/end, HUD, recording) | duration1 − timed stages |
| `cpu split max: z80` | cpu stage − the three measured hooks (per-component maxima are **independent window maxima**, caveat C1) | derived |
| `(N steps)` | window-max instruction count (`OnCPUStep` = per Z80 instruction) | `_stepCount` |
| `worst frame wall/thread cpu` | captured together at the same (worst-wall) frame; cpu = `QueryThreadCycleTime` / 3700 (hardcoded nominal 3.7 GHz) | frame-scoped latch |
| `histogram` | wall-time buckets 5 ms wide, bucket 5 = ≥20 ms | `duration1 / 5000` |
| `window avg` | per-frame averages (sum/256) of the cpu stage and hooks; **no avg wall, no avg steps** | `_frameStatSum*` |
| `env: cpu#N` | `GetCurrentProcessorNumber()` at report time (window boundary — not the worst frame's core) | |
| `env: prio %d` | `GetThreadPriority` = +1 = ABOVE_NORMAL — this **is** the intended Windows "realtime" (`ThreadHelper::setRealtimePriority`, threadhelper.cpp: deliberately below MMCSS audio) | |
| `10M-LCG probe` | 10M dependent LCG iterations run at window boundary; ~3.4 cycles/iter ≈ full speed; reference: `BM_CpuSpeedProbe_LCG10M` | |
| `previous log write` | cost of the previous summary print (console I/O on the emulation thread) | |

## Instrumentation caveats

- **C1 — Window maxima are independent.** `z80`, `step-screen`, `step-io`,
  `step-sound` and `steps` maxima may come from five different frames; only
  `worst wall`/`worst cpu` are latched as a pair. The uniform-inflation
  argument survives this (the ratios agree within ±10 % in all three windows),
  but the worst frame's true composition is unknown. Fix: P1.
- **C2 — `QueryThreadCycleTime` semantics are vendor-dependent.** If the
  counter is frequency-scaled (APERF-like), then `cpu ≈ wall` in the worst
  frames proves ~nominal frequency during them — and frequency throttling is
  **already excluded** by the existing data, leaving IPC collapse (SMT sibling,
  cache/TLB) as the mechanism. If it is TSC-like (invariant), `cpu ≈ wall`
  only proves on-CPU ≈ wall and frequency remains open. Calibration: R5.
- **C3 — The cache probe (when it eventually prints) has blind spots.** It
  reads only the first 64 KB of the framebuffer, only at frame start; it
  cannot see mid-frame eviction (the GUI frame-source lambda copies the full
  framebuffer on the GUI thread at display cadence, which can land inside a
  frame), and it does not probe the z80 interpreter's actual working set
  (emulator RAM, opcode tables) whose eviction would matter more for a uniform
  slowdown. A "warm" probe narrows, but does not close, the eviction question.
- **C4 — Hardcoded 3700 divisor / "nominal 3.7 GHz".** Verify the machine's
  actual base clock (`Get-CimInstance Win32_Processor | select MaxClockSpeed`);
  if it is not 3700 MHz, all `thread cpu` numbers scale linearly.
- **C5 — `stage cpu` and `worst thread cpu` are different scopes.** The former
  is the cpu stage only (steady_clock, wall); the latter covers all of
  RunFrame (cycles/3700). Do not compare them directly.
