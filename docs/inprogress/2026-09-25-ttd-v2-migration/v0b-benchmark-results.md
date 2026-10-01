# Phase 0, Step 2 results: the TTD benchmark matrix with v1 as the first engine

Date: 2026-09-29. Phase 0, Step 2 of the [migration trajectory](migration-trajectory.md), against [requirements §5](requirements.md) (BR-1 to BR-9).

**Decision: Phase 0, Step 4 (checkpoints inside a frame) is not needed.** On every configuration, the heaviest turbo ones included, reaching any position takes at most 3.8 ms at p99 (3.5 ms over a 10-minute session). The PR-5 limit is 5 ms. What the user waits for beyond that is *drawing the picture* of the new position, which costs another 4–8 ms. Checkpoints inside a frame would not change that cost (§3).

## 1. Glossary

| Term | Meaning |
|---|---|
| Checkpoint | A saved machine state that the TTD (time-travel debugger) can restore. v1 saves one checkpoint at the end of every frame |
| Seek | Jumping to a position in the recording: a frame, plus optionally a T-state inside that frame |
| Restore | Loading the nearest earlier checkpoint back into the machine |
| Replay | Running the machine forward from that checkpoint to the exact target T-state |
| Present | Building the screen picture of the position, so the user sees where the seek landed. v1 does this by running the whole frame again |
| Configuration | A machine model plus its peripherals, for example `ATM3+tsfm+gs512+moon` (ZX-Evo with TurboSound FM, General Sound with 512 KB, MoonSound) |
| Workload | What the machine does while being recorded (idle after boot, a game with scripted keys, a demo, music, disk loading). Every workload is replayable: each run produces the same machine states byte for byte |
| p50 / p99 | Median / the value that 99% of measurements stay under |

## 2. What was built

| Piece | Where |
|---|---|
| Harness: engine interface, configurations, workloads, metrics BM-1 to BM-8 | `core/src/debugger/ttd/bench/ttdbench.{h,cpp}` |
| Timing points inside v1 (capture; restore split by CPU/chipset, devices, memory, screen; replay; present) | `TTDPerfCounters` in `core/src/debugger/ttd/timetravelmanager.h` |
| The matrix as Google Benchmark cases, JSON output | `core/benchmarks/debugger/ttd/ttd_matrix_benchmark.cpp` |
| Compare / summarize / export script | [`tools/verification/ttd-bench/`](../../../tools/verification/ttd-bench/README.md) |
| CI gate, replacing `TTD_Capture_Cost_Gate_Test` | `core/tests/debugger/ttd/bench/ttdbench_test.cpp` |
| Stored baselines (BR-9) | [`testdata/ttd/bench/`](../../../testdata/ttd/bench/README.md) |

The engine is picked by name at run time (BR-1). Today the only engine is `v1`; v2 gets added next to it, and the same cases then compare the two directly.

The matrix comes in three sets:

- `ci`: 4 cases, about 10 s.
- `turbo`: 5 cases, the configurations that decide Phase 0, Step 4.
- `full`: 39 cases:
  - 13 base models;
  - 8 workloads on Pentagon;
  - 9 peripheral sets, each on Pentagon and on ZX-Evo.

Three things the harness had to fix to make the byte counts repeatable:

- **The RTC clock is frozen.** ZX-Evo's BaseConf firmware reads the CMOS clock while it boots, so with the host time the RAM content differed on every run. The harness sets the Ds12887 to a fixed time before the workload starts.
- **Power-on RAM is zeroed.** `Memory` fills RAM pages 5 and 7 with noise from the process-wide `rand()` when a machine is created. A case therefore booted differently depending on which cases had run before it in the same process: ZX-Evo idle wrote 439,023 bytes when run alone and 438,995 after one other case. This is the known hidden input described in [core/tests/README.md](../../../core/tests/README.md), "Power-on RAM is a hidden global input". The harness creates every machine with zeroed RAM (`[MISC] RAMPowerOn=ZERO`, the create option every automation surface now offers as `ram_power_on`). The default for users is unchanged.
- **The configuration comes from the matrix alone.** core-tests installs a config hook that switches off sound cards. The harness used to chain that hook, which gave the gate a different machine than core-benchmarks for the same case. The harness now replaces any hook while it creates a machine.

Exit criterion: a byte comparison of two runs of the same matrix reports 0 differences, for the `ci` set and for the `full` set. The byte metrics were also identical between the high-load and the low-load reruns of the full set. Timings are compared as a change in percent, and only fail a comparison when asked to (`--fail-on-time`).

## 3. The Phase 0, Step 4 question (PR-5)

PR-5 requires a seek to any position to complete in ≤ 5 ms at p99, measured over random positions of a 10-minute session. The table below comes from 10-minute sessions (30,000 frames) on the two heaviest turbo configurations, with 200 random positions, each measured three times with the fastest kept:

| Configuration (10 min) | State reached, p50 | p95 | **p99** | of which replay p99 | restore p99 | + present p99 (what the user sees) |
|---|---|---|---|---|---|---|
| ATM3 (ZX-Evo, 2x turbo) | 1.58 ms | 2.36 ms | **2.51 ms** | 1.59 ms | 0.93 ms | 8.6 ms |
| ATM3 + TSFM + GS512 + MoonSound | 2.03 ms | 3.18 ms | **3.46 ms** | 2.10 ms | 1.32 ms | 11.3 ms |

The shorter turbo runs (1,500 frames each) agree:

| Configuration | State reached, p99 | + present p99 |
|---|---|---|
| ATM710 with turbo | 2.1 ms | 7.6 ms |
| Scorpion (Turbo+, on from its firmware) | 1.5 ms | 5.8 ms |
| Pentagon, demo (7th Reality) | 1.9 ms | 6.5 ms |
| ATM3 | 2.5 ms | 8.6 ms |
| ATM3 + TSFM + GS512 + MoonSound | 3.2 ms | 11.1 ms |

(`v1-turbo.json`, run at load average 11–12.)

Across the full matrix (39 cases, run at load average 7–19), state-reached p99 ranges from 0.7 ms (Pentagon without AY) to 3.8 ms (ZX-Evo with MoonSound). Replay p99 stays at 0.4–2.7 ms in every full run, including the runs on a loaded host. An earlier full run on a heavily loaded host (load average 25–50) showed up to 5.5 ms on the Pentagon game. That was host load: at load ~7 the same cases gave 1.2–2.4 ms, and their byte metrics matched exactly.

Why Phase 0, Step 4 would not help:

- Replay, the only part that in-frame checkpoints would shorten, is 1.6–2.1 ms at p99 even at 2x turbo, which is already inside the limit.
- The time above 5 ms is the *present* step. v1 draws the picture of the position by running the whole frame again (`ComposeDisplay` in `PresentPosition`). That costs one frame of emulation no matter where the checkpoint is.

Rendering the full frame after every seek is intended: the picture always shows the complete frame of the position. It is not a task; this document reports the time to reach the machine state and the time with the picture separately.

## 4. Other findings (for Phase 1 and later)

These are v1's numbers. Each finding is an input for the step named in its row, not something Phase 0, Step 2 fixes.

| Finding | Numbers | Requirement / step |
|---|---|---|
| Capture cost follows installed RAM, not change | Capture with no memory written (BM-8): 7 µs on Pentagon without AY, 190 µs on ZX-Evo (4 MB), 300–420 µs with a General Sound card (its RAM is one whole blob). With 64 pieces of 4 KB written: 0.34–1.5 ms | PR-4, Phase 1 |
| Periodic capture spikes | ZX-Evo idle (10 min): p50 240 µs, p99 701 µs (2.9× the median, at the PR-3 limit of 3×); with GS512 + MoonSound: 518 / 1394 µs (2.7×). The cause is the key frame every 50 frames | PR-3, Phase 1 |
| Memory restore dominates the restore time on big machines | Memory (BM-6 p50): 60 µs on Pentagon idle, 260 µs on the Pentagon demo, 765–785 µs on ZX-Evo. Devices: 2–30 µs, 115–230 µs with a General Sound card. CPU/chipset: under 1 µs | Phase 1 |
| Session size | 10 min on ZX-Evo: 0.86 GB resident, 192 MB file (6.4 KB per frame); with GS512 + MoonSound: 0.92 GB / 198 MB | Phase 4 (memory budget), Phase 5 |
| Loading a session | Save 4.6–5 s/GB, load 2.9–3.1 s/GB; first seek after loading a 10-minute file: 0.57–0.63 s | Phase 5 |
| Recording overhead | Extra frame time with the full journal and coverage on (BM-1, median frame): 7–14% on 128K-class machines, 20–30% on ZX-Evo, 42–45% with GS512. Capture alone is 3–26% of a recorded frame, the most with a General Sound card | PR-1, Phase 1 |
| The coverage index has a fixed cost | ~3 MB per session, so it dominates short recordings (53 KB per frame over 60 frames, 3.4 KB per frame over 1,500) | informational |

Numbers are from a shared Mac Studio (Apple silicon), Release build, at load average 7–19 unless stated otherwise. That run was made before the harness zeroed power-on RAM, which changes a few bytes on cold-boot cases and no timings. The JSON files in `testdata/ttd/bench/` carry the commit and host of each run.

About the stored `v1-full.json`: its byte metrics are the reference, and they matched exactly across three full runs made after the RAM fix. Its timings were measured at load average 45–70, because other sessions kept the host busy (a rerun "waiting for a quiet host" reached load 100). Those timings are not a reference; rerun the full set on an idle host before using them for v1-vs-v2 timing comparisons.

## 5. Limits of this round

- Only one engine exists, so the matrix measures v1 against itself. The v1-vs-v2 comparison (acceptance criterion 4) comes with Phase 1.
- The Scorpion firmware switches its Turbo+ flip-flop on by itself while it boots: the standard ROM's service page reads `#7FFD` at `#0419`, ProfROM applies its turbo setting at `#04CE` on the way to the user program (checked 2026-09-30; the emulator follows the firmware, see the Scorpion hardware reference §13). So plain `SCORPION` and `PROFSCORP` run at 7 MHz (`turbo_ratio` = 2). The matrix first had a `SCORPION-turbo` case that recorded the same machine; it is now `SCORPION-3.5MHz`, which switches the turbo off after the boot (`IN #1FFD`).
- The `none` peripheral set appears as `noay` in case names. The name lists only the TurboSound slot; the other devices are off too.
- Timings on a shared host vary by 10–30% between runs. The CI gate therefore checks bytes exactly and timing only as the share of capture in the frame (budget 50%; measured 3–14% on the `ci` cases, up to 26% on the full matrix with GS512).
