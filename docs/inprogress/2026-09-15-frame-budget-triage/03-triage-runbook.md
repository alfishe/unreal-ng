# Triage runbook — no source changes required

Ordered by information gained per effort. Each step lists the exact commands
(PowerShell), what to record, and how to read the result. The tail metric to
compare across all configurations: the `histogram ... 15-20/>20 ms` bucket
counts per 256-frame window (and the `cache probe` line once it exists).
Always capture ≥3 windows (~15 s) per configuration; the tail varies ±40 %
window-to-window.

## R1 — Rebuild, verify the probe prints, re-run the demo (first action)

1. Rebuild the unreal-qt target in the configuration normally used.
2. Start unreal-qt, load the covox demo, let it reach the heavy scroller
   section (~30 s in; the staged benchmark warms up 1500 frames for exactly
   this reason).
3. Confirm the summary line now contains
   `cache probe at frame start: worst frame N us, avg N us`. If it does not,
   the binary is stale — stop and fix the build before interpreting anything.

Reading (the two-branch rubric, plus the mixed cases):

| Observation | Meaning | Next |
|---|---|---|
| worst ≈ 60–100 µs, avg ≈ 10 µs | working set evicted between frames (or core exited C-state) | H2/H4 branch: R4, then R6 to split frequency vs eviction |
| worst ≈ avg ≈ 10 µs | frame entered warm; slowdown arises inside the frame | H1/H3 branch: R2, R3b, R6 |
| worst AND avg both high | probe baseline off (size/stride/TLB effects) — treat as no-signal | rely on R5/R6 |
| worst ≈ avg ≈ 10 µs but tail persists across runs | record probe numbers; proceed to R2/R5 regardless | — |

Record: the three lines verbatim, same as before.

## R2 — Headless reproduction with the staged benchmarks

From the repository root (testdata path is relative):

```powershell
cd cmake-build-release-visual-studio   # or the MinGW build dir used
.\core\core-benchmarks.exe --benchmark_filter="BM_CovoxDemoFrame.*" 2>$null
.\core\core-benchmarks.exe --benchmark_filter="BM_CpuSpeedProbe.*" 2>$null
```

(Adjust the path if core-benchmarks.exe lands elsewhere; the target is built
from core/benchmarks.)

- `BM_CovoxDemoFrame` runs the exact demo snapshot back-to-back with no GUI,
  no audio device, no pacing. If its per-frame times show a 15–20 ms tail
  (Google Benchmark prints mean/median/stddev; a bimodal tail shows as a large
  stddev), the cost is core-side (workload H3 or a core pathology) and can be
  profiled directly in this harness. If it is flat at low-ms, the core is
  exonerated and the phenomenon needs the frontend's threading/pacing.
- `BM_CpuSpeedProbe_LCG10M` gives the machine's reference for the 9.1 ms
  in-app probe (calibrates C4's 3.7 GHz assumption).
- Caveat: the benchmark keeps the core warm (back-to-back), so a flat result
  does not exclude H1/H2 — it excludes core-side cost only.

## R3 — Affinity matrix

Get the SMT topology first (masks per physical core):

```powershell
coreinfo -c    # Sysinternals; shows logical->physical mapping
```

Configurations (each: run demo ≥3 windows, record tail buckets):

- **R3a — whole process to one physical core** (GUI + emu + audio share it):
  `(Get-Process unreal-qt).ProcessorAffinity = 0x11` (example: LPs of one
  physical core). Serializes frontend threads; mainly a stress view.
- **R3b — emulation thread alone to a core whose sibling stays idle** (the H1
  test): use System Informer (Process Hacker) → unreal-qt → Threads → find the
  emulation thread (ABOVE_NORMAL priority, ~25 % CPU) → Set Affinity to a
  single LP whose sibling mask excludes all other process threads and
  typical system load (DWM/audio run system-wide; pick accordingly).
- **R3c — baseline**: no pinning (current state).

Interpretation: tail collapses under R3b → H1 (sibling contention) confirmed
for the collapsed share. Tail persists under both R3b and R4 → suspect
in-frame effects (H3) or mid-frame eviction (P4/R6 territory). Tail collapses
only under pinning (any core) but not under power-plan changes → migration /
wake-target selection (H2 wake-to-cold variant).

## R4 — Power plan / parking matrix

```powershell
powercfg /getactivescheme                                   # record current
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c    # High Performance
# optional: disable core parking under the active scheme
powercfg /setacvalueindex scheme_current sub_processor 0cc5b647-c1df-4637-891a-dec35c318583 100
powercfg /setactive scheme_current
# ... run demo, record, then restore:
powercfg /setactive <recorded-guid>
```

Interpretation: tail collapses on High Performance / parking-off → H2
(frequency/parking on wake). No change → frequency-path of H2 unlikely (before
concluding, confirm with R5 that cycles aren't already telling us this).

## R5 — Calibrate `QueryThreadCycleTime` semantics (cheap, reinterprets all data)

Run the frontend under a forced low frequency and watch one summary window:

```powershell
powercfg /setactive a1841308-3541-4fab-bc81-f71556f20b4a   # Power saver
# run unreal-qt + covox demo ~15 s, note worst frame wall vs thread cpu
powercfg /setactive <recorded-guid>                        # restore
```

- If `thread cpu` stays ≈ `wall` while `wall` inflates → the counter is
  TSC-like (frequency-blind). Existing data then says only "on-CPU the whole
  frame"; frequency remains open (R4/R6 decide).
- If `thread cpu` ≪ `wall` under Power saver → the counter is
  frequency-scaled. Then in the production runs `thread cpu ≈ wall` already
  proves the worst frames ran at ~nominal cycles/µs — **frequency throttling
  is excluded by existing data**; the mechanism is IPC collapse (H1) or
  stalls (H4), and R3b/R6 finish the job.

(Self-validate the throttling actually engaged: the `10M-LCG probe` should
inflate visibly in the same window.)

## R6 — WPR/WPA capture correlated to the worst frames

```powershell
wpr -start CPU -filemode
# run demo ~30 s (several windows)
wpr -stop C:\temp\covox-cpu.etl
```

In WPA (Windows Performance Analyzer), per worst-frame window:

1. **CPU Usage (Precise)**: filter to the unreal-qt emulation thread —
   verify no context switches inside the worst frames (confirms D2); read the
   **Processor** column per switch-interval to see which core each frame ran
   on and whether the sibling LP of that physical core was busy (add a second
   graph filtered to the sibling mask). This is the H1 verdict.
2. **CPU Sampling / Computation Stacks**: which module burns the samples
   during the slow intervals (expect the interpreter; anything else is a find).
3. **DPC/ISR**: storms aligned with slow frames (H6 checkbox).
4. **Processor frequency / P-state** (Microsoft-Windows-Kernel-Processor-Power
   events, if captured): low frequency during exactly those frames = H2.
5. Correlating ETL time to specific worst frames: today only the 5 s summary
   timestamps anchor it (worst frame #N within the window); for exact
   alignment add P3 (worst-frame timestamp) in the next instrumentation pass.
   With hardware counters: `wpr -start CPU -PMC InstructionsRetired:Cycles ...`
   (or xperf `-pmc`) gives IPC during slow vs fast intervals — low IPC at full
   frequency = contention; low frequency = H2.

## R7 — Coarse continuous counters while the demo runs

```powershell
typeperf "\Processor Information(*)\% Processor Performance" -si 1 -o freq.csv
typeperf "\Processor(*)\% DPC Time","\Processor(*)\% Interrupt Time" -si 1 -o dpc.csv
```

Catches sustained patterns (thermal, parking) at 1 s granularity; not
frame-resolved, but free and running in parallel with every other step.

## R8 — Hardware-counter profile of the slow frames (the rubric's step 2)

If R1 says "warm at entry": AMD uProf (Profiles: Clock, IPC, Cache) or Intel
VTune (Hotspots + Threading + frequency timeline) on the running frontend, or
on the headless harness if R2 reproduced the tail. Look for, during slow
windows: effective frequency, IPC, L2/LLC misses, remote-CCX access, SMT
sibling utilization. This is the definitive H1-vs-H2-vs-H4 evidence.

## R9 — EcoQoS / focus matrix (cheap exclusion)

Task Manager → Details → add "Power throttling" column → check unreal-qt while
the demo runs. Then: demo window focused vs occluded vs minimized, one
configuration per 3 windows. Tail appearing only when occluded/minimized → H5.

## Follow-up instrumentation proposals (NOT applied — for the next change)

Ordered; P1–P3 close the attribution gaps found in this analysis:

- **P1 — Worst-frame full latch.** Capture one struct at the new worst frame
  (wall, cpu, all four split components, steps, probe, processor number) and
  print it, replacing the independent per-component window maxima (fixes C1;
  makes the "uniform inflation" argument per-frame-exact).
- **P2 — Window averages for steps (and wall).** avg steps closes D4's
  residual: per-step cost avg vs worst-frame per-step cost directly reads
  "same work slower" vs "more work".
- **P3 — Worst-frame wall-clock timestamp** (µs since epoch) in the summary:
  aligns ETL traces to the exact frame (enables R6 precisely).
- **P4 — End-of-frame second cache probe** (and/or probe the emulator RAM /
  opcode tables instead of the framebuffer): detects mid-frame eviction the
  start-probe cannot see (C3).
- **P5 — Frequency snapshot at frame boundaries** via
  `NtPowerInformation(ProcessorInformation)` (`PROCESSOR_POWER_INFORMATION`:
  CurrentMhz / MhzLimit / MaxMhz) latched for the worst frame: user-mode
  effective-frequency evidence without ETW.
- **P6 — Optional per-frame CSV dump** (frame#, wall, cpu, steps, probe,
  cpu#) behind an env var: scatter of wall vs steps separates bimodal speed
  states (two slopes) from workload noise without any profiler.
- **P7 — Defer the summary print itself** to the GUI thread (same rationale
  as the audio-callback logging fix already in this diff); it costs
  0.5–0.6 ms of console I/O on the emulation thread every window.
- **P8 — After root cause:** compile-time-gate the per-step clock reads
  (~1.5–2 ms/frame uniform overhead, D9) so clean numbers can be re-measured,
  and re-evaluate the hardcoded 3700 divisor (C4).

## What "fixed" looks like

With the tail explained, the success metric is the histogram: the 15–20 ms
bucket should sit at zero (or be explained by content, e.g. loader borders)
across ≥10 consecutive windows with `over budget` = 0, while the audio ring
holds its occupancy sawtooth without hard resyncs.
