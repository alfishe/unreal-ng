# 040 — ttd-inplace-replay-profile

Feature: [docs/inprogress/2026-10-28-ttd-analysis-feedback](../../../docs/inprogress/2026-10-28-ttd-analysis-feedback/README.md)
(before planning phase 2). Follows [POC 038](../038-ttd-replay-query-engine/README.md).

## Context

POC 038 measured today's in-place verbs at about 1.3-2.7 ms per replayed frame on the user's machine, while a clone
running the same frames forward from one checkpoint needs about 0.22 ms. Phase 2 is planned around that gap, so its
causes must be known by number, not by guess:

- every frame restores its checkpoint (RAM pages decoded from the piece store) instead of running on from the last;
- a seek runs the frame again to present a picture;
- the frame cache records every instruction through the M1 hook;
- every verb saves and restores the user's live state around the replay.

The recording side matters as well: what a recording costs per frame (capture), with and without the write journal,
how many bytes a frame takes on the heap and in the session file, and how fast a point read (memory-at) is.

## Goals

- **G1** Split the per-frame cost of the in-place replay into its parts: checkpoint restore (CPU and chipset,
  devices, memory, screen), the run to the frame end, the M1 hook, the picture, the live state save and restore.
- **G2** Time the public verbs on the same window: seek (frame start, mid-frame), frame cache, memory-at (CPU view),
  reverse-continue `all`, find `all` and `changes_only` (replay path and write-journal path).
- **G3** Recording: frame time without TTD, with a recording, with the write journal; capture time per frame.
- **G4** Size: session file bytes per frame and the heap breakdown per frame.

## Requirements

- Workloads W1-W3 of POC 038 (Pentagon paging loop, Pentagon 7th Reality, 48K BASIC typing), 3000 frames each.
- Release build, the same machine as POC 038, median of 3 runs.
- Sampling profile (`sample`) of the dominant steps through `--loop`, for inclusive attribution.

## Constraints

- Tool only, not linked into the emulator (`BUILD_TTD_INPLACE_PROFILE_POC`).
- No engine change in this POC; artifacts in `scratch/`.

## Acceptance criteria

- Every step's microseconds per frame, for every workload, with the restore split.
- The parts add up to the measured verb cost within 15 %, or the gap is named.
- A ranked list of causes with the share of each, and what phase 2 should change first.

## Running

`--record-only` prints the recording table only; `--loop verify` checks every frame of the window replays to the
next checkpoint exactly.

```bash
cmake -B cmake-build-agent-release -DBUILD_TTD_INPLACE_PROFILE_POC=ON
tools/build/build.sh ttd-inplace-profile
cmake-build-agent-release/bin/ttd-inplace-profile --workload w1 --frames 3000
```

Run from the repository root (W2 loads `testdata/loaders/sna/7threality.sna`).

## Results (2026-10-28, Release, macOS, Apple Silicon, 3000 frames, median of 5)

The machine was shared with other sessions (load 10-30): absolute times carry about ±20 % noise, the work counts
(pieces, links) are exact. Raw output: `scratch/2026-10-28-poc-039/final-w{1,2,3}.txt`, samples `sample-*.txt`.

Regions per checkpoint (4 KB pieces): Pentagon (W1, W2) `ram` 32, `neogs.ram` 512, `neogs.flash` 128,
`moonsound.wave` 256, devices 23: 951; 48K (W3) `ram` 24, devices 7.

### Recording (µs per frame)

| | W1 | W2 | W3 |
|---|---|---|---|
| no time travel | 354 | 460 | 293 |
| debug mode only (TTD requires it) | 414 | 799 | 352 |
| recording | 540 | 947 | 425 |
| of it capture | 51 | 86 | 36 |
| recording with the write journal | 529 | 988 | 496 |
| session file, B/frame (with journal) | 207 (209) | 954 (1553) | 216 (585) |
| heap, B/frame (with journal) | 2547 (2848) | 4237 (6752) | 2771 (4350) |

Heap per frame: checkpoints 775-823 B, coverage 1059-1308 B, pages 170-845 B (+ slack), ports 8-530 B, the write
journal 563-2777 B.

### Replay (µs per frame)

| Step | W1 | W2 | W3 |
|---|---|---|---|
| continuous run from one seek | 426 | 823 | 805 |
| checkpoint restore | 1325 | 1178 | 63 |
| of it memory | 1206 | 1093 | 28 |
| of it devices | 114 | 85 | 34 |
| engine memory restore, every piece (as today) | 739 | 1088 | 26 |
| engine memory restore, changed pieces only | 6 | 38 | 14 |
| restore + run to the frame end | 1828 | 2069 | 876 |
| the same + M1 hook | 1821 | 2087 | 884 |
| seek to a frame start | 4048 | 3786 | 1501 |
| of it the picture (`PresentPosition`) | 2171 | 2302 | 1024 |
| frame cache | 1588 | 2725 | 1475 |
| memory-at (CPU view), frame start | 1372 | 1526 | 534 |
| memory-at, mid-frame | 1215 | 1941 | 937 |
| reverse-continue `all` | 435 | 2 | 1150 |
| find `all` (replay) / write journal | 1101 / 2.7 | 30 / 8.7 | 101 / 15 |

The parts add up: seek W2 = restore 1178 + picture 2302 = 3480 of 3786 (the rest is the operation's publish,
`GetSessionInfo` + `GetHeapBreakdown`, ~6 % of the samples); the picture = a second restore + a whole frame run +
the live state save and restore.

### Causes, ranked

1. **Every restore decodes all memory.** `RestoreCheckpointForReplay` calls `ForgetMemory()`, so all 951 pieces are
   decoded each time; 896 of them are NeoGS and Moonsound memory that W1/W2 never touch (memset + CRC-32C of 4 KB
   each), and a changed piece walks its difference chain (up to K = 50 links: 171-356 links per restore).
   `Decode` itself (CRC, XOR) is ~60 % of the samples, ZSTD ~30 %. Keeping what memory holds (the engine's
   `_inMemory`, with the dirty tracker's writes as the `written` callback) cuts the memory part from 0.74-1.1 ms
   to 6-38 µs: 30-180x. On a plain 48K the restore is already cheap (63 µs).
2. **A seek presents a picture.** The picture restores the previous checkpoint and runs a whole frame again:
   55-70 % of a seek. Verbs and memory-at do not need it; a UI seek can compose lazily (only when shown).
3. **Restoring per frame instead of running on.** A window replayed frame by frame pays the restore every frame;
   running on from one seek costs only the frame (W2: 823 against 2069).
4. **Input playback services every instruction.** While the input journal plays, `ServiceInput` runs at every
   instruction with three mutex lock/unlocks: W3 replays at 805 µs against 352 in debug mode (2.3x).
   Waking at the next event's time (and an atomic "pending" flag) removes it.
5. **No HALT idle while recording.** Debug mode looked like the main recording cost (W2: 460 → 799 µs), but the
   follow-up below shows it is the HALT idle fast path (`RunHaltedIdle`), refused while TTD records or replays.
6. **Live state save/restore and the publish** around each verb call: ~250-650 µs a call (the frame cache pays it
   per frame).
7. **The M1 hook** costs nothing measurable.

### Conclusions for phase 2

- In place, causes 1-4 fixed: a window replay ≈ the frame's run at debug speed (W2 ~0.8 ms, W1 ~0.4 ms), memory-at
  at a frame start ≈ 40-130 µs without touching the machine (decode the mapped pieces from the store).
- Clones (POC 038) stay the way to answer long windows in parallel; cause 1 also speeds up a clone's seek.
- Recording: the refused HALT idle, not the capture, is the main cost on a game that halts; capture 36-86 µs a
  frame. Heap 2.5-6.8 KB a frame (1 hour = 180k frames: 0.46-1.2 GB), coverage the largest part.

## Follow-up: is it debug mode or the HALT idle? (2026-10-28)

A temporary core patch (not kept; an environment variable `UNREAL_POC040_TTD_LITE`) recorded in three levels:

- **0** today: TTD switches debug mode on;
- **1** TTD without debug mode: the debug memory path (dirty pages, coverage, write journal) stays, the CPU's
  per-instruction debug work (breakpoints, PC history) goes;
- **2** level 1, and a recording may idle through HALT. Two refusals were lifted: `IdleStepsInert` refused while
  TTD coverage is armed (`InstructionStartObserved`), and while TTD records (`IsRecording`, since `39417bffc`, no
  reason given). The patch dropped the HALT's coverage mark of an idled stretch (to fix in the real change).

Thread CPU time (`CLOCK_THREAD_CPUTIME_ID`: the host was loaded 24-70), 3000 frames, 3 interleaved rounds, µs a frame:

| | no TTD | level 0 | level 1 | level 2 |
|---|---|---|---|---|
| W1 (loop, no HALT) | 198-210 | 287-316 | 287-311 | 286-292 |
| W2 (7th Reality, halts) | 447-458 | 930-933 | 926-929 | **568-591** |
| W3 (48K BASIC) | 293-339 | 422-474 | 422-463 | 426-448 |

Exactness: every recording of every level replays (stepped, in debug mode) to each next checkpoint bit for bit,
2998 of 2998 frames on W1-W3 (`--loop verify`). Idling through HALT while recording is exact.

- The CPU's debug work costs nothing measurable: splitting the debug flag gains nothing.
- The HALT idle is the whole gain: W2 recording -39 %, overhead +108 % → +27 %. No HALT, no change (W1, W3).
- Left on W1 / W3 (+45 %): the debug memory path (+41-60 µs) and TTD's own work (capture 24-36 µs, coverage). A
  TTD-only memory path would save part of the 41-60 µs at most: not worth a new mode now.

