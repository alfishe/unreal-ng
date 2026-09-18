# Hypothesis matrix — where the 15–20 ms tail comes from

Each hypothesis states: mechanism, fit with deductions D1–D9
(see `01-evidence-and-deductions.md`), the signature it would leave in the
data, and the cheapest discriminating experiment. Hypotheses are not mutually
exclusive; H2 and H4 both predict a slow frame-start cache probe and differ
only in effective frequency.

## H1 — SMT-sibling contention for the whole frame (rank 1)

**Mechanism.** The emulation thread wakes from its ~15 ms pacing sleep onto a
logical core whose SMT sibling is busy for the rest of the frame. Steady
periodic candidates in-process and around it: the miniaudio/WASAPI device
thread (MMCSS "Audio", ~10 ms pulls), the Qt GUI thread running the
frame-source copy of the full framebuffer at display cadence, DWM composition
(~16.7 ms), screen-viewer / videowall consumers. SMT does not preempt the
emulation thread (fits D2) but shares L1/L2, LSU, store buffers and decoder,
collapsing IPC for branchy interpreter code (fits D3's uniform 3.5–3.8×), for
the whole 18–21 ms duration (frames are ~20 ms; sibling periods 10–17 ms — a
collision plausibly spans an entire frame, then clears). ~5–8 % incidence
matches collision probability of two independent ~50–60 Hz duty patterns. The
LCG probe at the window boundary lands wherever the thread happens to be, so
it usually samples a quiet moment (fits D5).

**Signature.** Cache probe warm at worst-frame start; `thread cpu ≈ wall` with
full effective frequency (if R5 shows the cycle counter is frequency-scaled);
WPA shows the sibling logical core busy during exactly those 20 ms; tail
collapses when the emulation thread is pinned to a core whose sibling stays
idle.

**Discriminating test.** R3b (per-thread affinity to a sibling-idle core) —
expect the 15–20 ms bucket to empty. Ground truth: R6 (WPA, both logical
processors of the physical core).

## H2 — Wake onto a cold / parked / low-frequency core (rank 2)

**Mechanism.** The thread sleeps ~15 ms per frame (D6). If the wake lands on a
core that went to a deep idle state (L1/L2 flushed, min P-state, unparking
lag), the frame starts cold and — if frequency ramping lags or parking
heuristics hold the core down — runs uniformly slow for the whole frame. Fits
D3 (uniform), D2 (no preemption), D5 (probe time is warm), the observed
core-to-core migration (#2 → #10 → #6), and the 3.6× magnitude if the core
stays near its minimum frequency for most of the frame.

**Signature.** Cache probe slow at worst-frame start (60–100 µs vs ~10 µs —
this is exactly the branch the probe was built to detect); effective frequency
low during those frames (WPA Processor Frequency / `% Processor Performance`);
tail collapses under High Performance plan + disabled core parking (R4) or
under pinning to a core that stays busy (R3).

**Discriminating test.** R1 (probe data) + R4 (power plan matrix). If R5 shows
`QueryThreadCycleTime` is frequency-scaled, existing `cpu ≈ wall` data already
refutes the frequency part of H2, leaving only the cold-cache part (→ H4).

## H3 — Per-frame workload spikes (rank 3 — partially pre-refuted)

**Mechanism.** The demo occasionally executes a costlier instruction mix (OUT
bursts into covox, shorter instructions → more steps → more per-step hooks and
more instrumentation overhead). The window maxima of steps do rise together
with the worst walls (12219/13956/17727 steps alongside 18.3/18.6/20.9 ms),
and step-sound is the single largest component — consistent with a covox
demo's sound-engine bursts.

**Why it cannot be the whole story.** The frame is a fixed 71680-T-state budget
(D4); instruction count can move ~1.45× at most between these worst frames,
not 3.6×; and all components including the z80 remainder inflate by the same
factor, whereas a workload shift changes the mix. It may contribute the
1.45×-class variation and set which frame becomes the worst within a window,
while the 3.6× execution-speed factor is environmental.

**Signature.** Headless `BM_CovoxDemoFrame` (back-to-back, warm, no GUI/audio
device) also shows 15–20 ms frames; steps of the worst frame ≫ window average;
per-step cost in worst frames equal to the average per-step cost.

**Discriminating test.** R2 (headless benchmark distribution) + P2 (print avg
steps). If headless is flat at low-ms and per-step cost in the worst frames is
~3.6× the average, H3 is out as the tail driver.

## H4 — Cross-thread cache/TLB eviction between frames (rank 4)

**Mechanism.** Between frames (while the emulation thread sleeps ~15 ms) the
GUI thread's frame-source copy reads the full framebuffer
(unreal-qt/src/mainwindow.cpp `setFrameSource`), plus texture upload and DWM —
evicting the emulator's working set (framebuffer, 128 KB RAM, opcode tables)
from L1/L2/L3. The next frame then runs slower on a cold working set. This is
the hypothesis the new frame-start cache probe was designed to catch.

**Why rank 4.** Pure cache eviction usually adds a bounded refetch cost (tens
of µs for a few hundred KB), not a uniform 3.6× across a 5 ms compute-bound
frame — unless it coincides with SMT sharing (H1) or frequency effects (H2).
It fits D3/D5/D6 equally well but explains the magnitude worst.

**Signature.** Cache probe slow at worst-frame start **with normal effective
frequency** (distinguishes from H2); mid-frame eviction invisible to the probe
(see caveat C3); headless benchmark unaffected.

**Discriminating test.** R1 (probe) combined with R5/R6 frequency evidence.

## H5 — EcoQoS / power throttling of the process (rank 5)

**Mechanism.** Windows applies power throttling to background/occluded
windows, capping threads well below nominal. Sustained (not frame-local)
though — D5's stable LCG argues against it unless the probe windows happened
to be unthrottled. Cheap to exclude.

**Signature.** Task Manager Details → "Power throttling" column = Enabled for
unreal-qt while the demo runs; tail changes with window focus/visibility.

**Discriminating test.** R9 (focus/occlusion matrix + throttling column).

## H6 — DPC/ISR storms, timer coalescing artifacts (rank 6 — mostly pre-refuted)

Preempting DPCs/ISRs would show `wall ≫ cpu` (they preempt the thread); D2
shows 99 % on-CPU. Timer coalescing affects period, not cpu-stage time, and
max period is ≤1.05× budget (D8). Keep only as a checkbox in the WPA pass (R6:
DPC/ISR view) since a storm confined *between* the measured clock reads of a
stage could still smear into wall numbers.

## Decision tree after R1 (probe data exists)

```
worst-frame probe >> avg (~60–100 vs ~10 µs)
├── + low effective frequency (R5/R6)      → H2: cold/parked/low-freq core
└── + normal frequency                     → H4: eviction between frames
                                              (then: who copies? GUI frame source)
worst-frame probe ≈ avg
├── + cpu≈wall AND frequency-scaled cycles → H1: IPC collapse (SMT sibling)
│                                            (R3b pinning to confirm)
├── + cpu≈wall AND TSC-like cycles         → frequency unknown → R5/R6 first
└── + headless BM_CovoxDemoFrame slow too  → H3/…: profile the core itself
```

Note the probe's blind spot (C3): a warm probe narrows the frame-entry
question only. Mid-frame eviction still needs the end-of-frame probe (P4) or
hardware counters (R6/R8).
