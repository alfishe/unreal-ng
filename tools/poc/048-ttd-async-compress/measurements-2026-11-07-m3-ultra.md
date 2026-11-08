# 048 — measurements 2026-11-07, M3 Ultra

## Setup

- Host: the M3 Ultra build host, Release build of
  `core-benchmarks` / `core-tests` from github/master `97a3c9fbc` plus this POC's patch.
- Code: [prototype.patch](prototype.patch) (all three stages, against `97a3c9fbc`; `git apply` in a worktree).
- Counters: `core/src/debugger/ttd/poc048counters.h` of the patch, printed at exit with `UNREAL_POC048=1`. The time in zstd is
  taken around every `codec::Compress` call (all of TTD) and around the piece store's own calls.
- Variants (one binary, `UNREAL_POC048_ASYNC`): 0 compresses on the emulation thread (today); 1 hands each piece to
  one worker thread and wakes it per piece; 2 queues the pieces and wakes the worker once per capture
  (`TTDPieceStore::FlushAsync`). The worker only compresses; the emulation thread applies its answers (the encoding
  rule, the arena, the references), so the store is touched by one thread.
- Runs interleaved: mode 0, 1, 2 one after another in each round.

## Stage 1: the upper bound (mode 0)

Per capture, the frame fixture (1000 frames) and the heavy set (`UNREAL_TTD_BENCH_SET=heavy`, 1500 frames):

| Workload | Recording frame | Capture | `Intern` | zstd, pieces | zstd, all of TTD | zstd share of the frame |
|---|---|---|---|---|---|---|
| `BM_Frame_TTD_Gaming` | 755 µs | 70.7 µs | 64.0 µs (9.1 calls) | 49.8 µs (7.5 calls) | 52.6 µs (41.6 KB) | 7.0 % |
| `BM_Frame_TTD_Development` (journal) | 807 µs | 70.5 µs | 63.9 µs | 49.8 µs | 71.9 µs (57.5 KB) | 8.9 % |
| Pentagon, idle | 252 µs | 11.3 µs | 6.3 µs | 2.2 µs | 2.9 µs | 1.2 % |
| Pentagon + GS512, idle | 244 µs | 9.7 µs | 6.2 µs | 1.9 µs | 2.6 µs | 1.1 % |
| ZX-Evo, idle | 164 µs | 22.3 µs | 12.1 µs | 4.0 µs | 7.2 µs | 4.4 % |
| ZX-Evo + sound cards, idle | 166 µs | 20.5 µs | 11.9 µs | 3.3 µs | 6.5 µs | 3.9 % |
| Sprinter, idle | 1424 µs | 36.0 µs | 17.0 µs | 5.6 µs | 8.4 µs | 0.6 % |
| TS-Conf + VDAC2, idle | 3089 µs | 24.4 µs | 10.0 µs | 4.3 µs | 5.8 µs | 0.2 % |
| Next, idle | 6763 µs | 31.7 µs | 8.7 µs | 3.5 µs | 13.7 µs | 0.2 % |

The recording frame of the heavy set is the median frame without TTD (`bm1_frame_off_us_p50`) times 1 + the median
BM-1 overhead with the journal and the coverage, both from the stage 2 mode 0 runs below. The fixture passes the
charter's 5 % entry condition; no idle heavy configuration does (ZX-Evo comes closest, 3.9-4.4 %).

## Stage 2: the worker

### Frame fixture, unbounded queue, 6 rounds (µs per frame, median; process CPU from `/usr/bin/time`)

| Mode | Gaming | Development | CPU, Gaming run | CPU, Development run |
|---|---|---|---|---|
| 0 (today) | 755 | 810 | 1.79 s | 1.84 s |
| 1 (wake per piece) | 708.5 (−6.2 %) | 755 (−6.8 %) | 1.79 s | 1.84 s |
| 2 (wake per capture) | 706.5 (−6.4 %) | 752 (−7.2 %) | 1.79 s | 1.835 s |

Per capture (Gaming, modes 1 / 2): worker 49.6-50.2 µs of zstd, handoff 1.6-2.0 / 1.3-1.9 µs, applying the answers
0.41-0.50 µs, waits 0.006 / 0.007 per capture (≤ 0.07 µs), 4.41 pieces queued per capture.

Pieces waiting for the worker peaked at 1.43 MB with an unbounded queue (the baseline capture offers the whole
memory at once).

### Frame fixture with the queue bounded, 6 rounds

At most 96 pieces wait for the worker; a piece past the bound is packed on the emulation thread, as today. Modes 0
and 2 interleaved, µs per frame:

| Benchmark | Mode 0, rounds | Mode 2, rounds | Median 0 → 2 |
|---|---|---|---|
| `BM_Frame_TTD_Gaming` | 735 756 768 774 764 781 | 691 718 710 719 714 715 | 766 → 714.5 (−6.7 %) |
| `BM_Frame_TTD_Development` | 773 806 810 809 806 807 | 759 763 763 768 758 767 | 806.5 → 763 (−5.4 %) |

Peak of queued pieces 794 KB. Per capture: worker 50.5-52.6 µs, handoff 1.25-1.80 µs, applying 0.41-0.60 µs, waits
0.005-0.008 (≤ 0.09 µs), 4.33 pieces queued.

### Seeks while recording, 6 rounds

`BM_Frame_TTD_SeekWhileRecording` (in the patch): the Gaming fixture, and every 10th frame a seek 25 frames
back right after the capture, then `ResumeRecordingFrom` the end; 1000 frames, 100 seeks, none failed. µs per frame:

| Mode | Rounds | Median |
|---|---|---|
| 0 | 1247 1254 1254 1249 1262 1262 | 1254 |
| 2 | 1200 1202 1200 1204 1206 1198 | 1201 (−4.2 %) |

Waits rise to 0.074-0.081 per capture (about 0.77 per seek), 0.49-0.62 µs per capture (about 5.5 µs per seek, under
1 % of a seek). The seek reads the history and waits for the last capture's pieces; it costs less than what the
worker took off the frames between seeks.

### Heavy set, modes 0 and 2 (bounded queue; BM-1 overhead of recording over the frame without TTD, median)

3 rounds per configuration, ZX-Evo 10 rounds; points of overhead:

| Configuration | No journal, 0 → 2 | Difference | Journal + coverage, 0 → 2 | Difference |
|---|---|---|---|---|
| Pentagon | 45.7 → 44.0 % | −1.7 | 58.3 → 59.6 % | +1.3 |
| Pentagon + GS512 | 48.9 → 46.1 % | −2.8 | 56.3 → 56.9 % | +0.6 |
| ZX-Evo (10 rounds) | 34.7 → 34.0 % | −0.7 | 53.7 → 54.0 % | +0.3 |
| ZX-Evo + sound cards (10 rounds) | 34.5 → 36.8 % | +2.3 | 52.7 → 54.5 % | +1.8 |
| Sprinter | 40.6 → 39.2 % | −1.4 | 47.6 → 46.7 % | −0.9 |
| TS-Conf + VDAC2 | 14.5 → 14.4 % | −0.1 | 21.8 → 21.6 % | −0.2 |
| Next | 4.7 → 5.3 % | +0.6 | 10.1 → 10.4 % | +0.3 |

Points of overhead are percent of the frame without TTD. Single rounds of one configuration spread over 7-14 points
(ZX-Evo + sound cards, no journal: q1 31.9 against 34.7); every difference is inside that spread, and the most the
worker could take here is the zstd share of stage 1 (0.2-4.4 %). Capture p50 is the same in both modes (an idle
capture compresses one or two pieces). The heavy set got 3 rounds, not the 6 of the charter, except ZX-Evo.

### Correctness

- `TTDPieceStorePoc048_Test.WorkerStoresTheSameVersions`: 16 pieces over 400 frames (one byte, a block, noise,
  zero, a pattern; the previous version released every frame, so ids are freed and reused) through a synchronous
  and a worker store, modes 1 and 2: the same id sequence, and per version the same encoding, depth, base, CRC and
  payload bytes; the same live versions, payload bytes and compress calls.
- The TTD suites (`*TTD*:*Ttd*:*TimeTravel*:*PieceStore*`, 1533 tests) in modes 1 and 2: every replay, seek,
  replay-check and session test passes. Six fail on purpose-free counters only: the CI gate's deterministic work
  counters (`bm2_work_compress_calls_opf`, `_compress_input_bpf`, `_device_blobs_bpf`: counted when a piece is
  applied, a capture later), `TimeTravelEngineHeap_Test.SmallSessionsToo` (the queue's memory is not in
  `HeapBytes`) and `TTDPieceStore_Test.EncodeOnce_FullCompressedOnlyForALargeDifference` (compress calls read
  before the worker answered).
- A first version freed a base later than the synchronous store (the pending piece's reference), so ids were reused
  in another order; fixed by applying the pending pieces before a base whose only other holders they are goes.

## Stage 3: one zstd context reused per thread

`codec::Compress` calls `ZSTD_compress`, which sets up and frees a compression context on every call (the header's
note says thread-local contexts; the code had none). `UNREAL_POC048_CCTX=1` keeps one `ZSTD_CCtx` per thread and
calls `ZSTD_compressCCtx` with the same level (also in the patch). Worker off (mode 0); modes interleaved, 8 rounds; µs per frame and
zstd time per capture (all of TTD):

| Benchmark | Fresh context, rounds | Reused, rounds | Frame median | zstd per capture |
|---|---|---|---|---|
| `BM_Frame_TTD_Gaming` | 763 752 756 753 758 763 756 757 | 755 756 748 749 758 753 755 752 | 756.5 → 754 (−0.3 %) | 53.3 → 47.9 µs (−10 %) |
| `BM_Frame_TTD_Development` | 805 796 798 801 810 811 818 812 | 783 799 789 797 806 798 808 822 | 807.5 → 798.5 (−1.1 %) | 73.7 → 67.4 µs (−9 %) |

The compressed bytes per capture are the same (41612 B and 57517 B); `TTDCompressionPoc048_Test.ReusedContextGivesTheSameBytes`
(300 inputs of 37 B-64 KB, one context reused across them) gives the bytes of a fresh context; the TTD suites
(1549 tests, with the CI gate's byte counters) pass in both modes. The saving, 5.4-6.3 µs per frame, is about 0.7 %
of the frame: real in the counters, below the frame noise.

### Not measured

- Several machines recording at once (one worker each).
- The local Mac: too loaded by other work on 2026-11-07 for frame times.
