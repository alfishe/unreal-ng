# 048 — ttd-async-compress

Does moving the TTD piece store's zstd compression off the emulation thread pay for itself, or does the handoff
(copy, queue, wake-up, waiting on a version not yet packed) eat the gain? Feature: TTD recording cost
(`docs/inprogress/2026-09-25-ttd-v2-migration`, the recording-cost item after `7bcd0d0bc`).

## Background

A checkpoint capture runs on the emulation thread at each frame end (`TimeTravelEngine::Capture` ->
`TTDPieceStore::Intern`). For a changed 4 KB piece `Intern` XORs it with the previous version, checks for zero, takes
the CRC, stores a few changed runs as they are, and otherwise compresses the difference (and the whole piece when the
difference packs to more than 128 B) with zstd. In the profile of the frame benchmark (`BM_Frame_TTD_Gaming`, M3
Ultra, after `7bcd0d0bc`) `CaptureNow` takes about 614 samples, of them `codec::Compress` about 400; the frame costs
730 µs recording against 487 µs without TTD.

## Goals

1. **Upper bound.** How much of the emulation thread's recording time is compression, per workload: the time in
   `Compress` per frame, calls per frame, input bytes per frame.
2. **Net gain.** With compression on one worker thread per machine, what does the emulation thread save net of the
   handoff: copying the difference into a queue, the queue itself, waking the worker, and every wait for a version
   the worker has not packed yet (a seek, a restore, a save, the ring dropping a segment, the chain limit reading a
   base).
3. **Side effects.** What the second thread costs the machine as a whole: total CPU of both threads, memory held by
   pieces waiting to be packed, interference with the emulation thread (shared caches, memory bandwidth) and with
   other machines running at once.
4. **Variant choice**, if goal 2 shows a gain: which handoff (a single-producer ring of fixed slots, a vector swapped
   per frame, a batch per capture) and which waiting rule (block, pack on the spot, read the unpacked bytes) cost the
   least.

## Requirements

- Workloads: the frame fixture (`FrameBenchmarkFixture/BM_Frame_TTD_Gaming`, `_Development`) and the heavy matrix
  (`UNREAL_TTD_BENCH_SET=heavy`: Pentagon with and without GS512, ZX-Evo with and without its sound cards, Sprinter,
  TS-Conf with VDAC2, Next), with the app's defaults (coverage index on, write journal off) and with the journal on.
- Machines: the M3 Ultra build host (.20) for the numbers, the local Mac for a check that the sign holds.
- Headless benchmarks only; no GUI.
- A seek-heavy case: seeks into the last second of a recording while it records (the case that waits on unpacked
  versions most).

## Constraints

- Nothing lands on master from this POC but its documents; the prototype is kept as [prototype.patch](prototype.patch).
- The stored bytes stay the same: every packed payload and CRC equal to today's synchronous store (checked per
  piece), so a session file is byte for byte what it would have been.
- TTD off costs nothing new: no thread exists while no recording runs.
- At most one worker per machine; the memory of pieces waiting to be packed is bounded (a full queue packs on the
  emulation thread, as today).
- Benchmarks are run A/B interleaved, at once, with enough rounds for the noise
  ([performance-guidelines.md](../../../docs/guidelines/performance-guidelines.md)).

## Acceptance criteria

- **No-go at once** if goal 1 gives compression under 5 % of the recording frame on the frame fixture and under 5 % on
  every heavy configuration: there is not enough to win.
- **Go** if, on the median of at least 6 interleaved rounds:
  - `BM_Frame_TTD_Gaming` on the emulation thread is at least 5 % faster than synchronous compression;
  - no heavy-matrix configuration is more than 2 % slower;
  - the seek-heavy case is not slower by more than 5 %;
  - both threads together use no more than 10 % more CPU than today;
  - pieces waiting to be packed hold at most 1 MB per machine at the worst moment measured;
  - the stored payloads are byte-identical to today's.
- **No-go** otherwise, in particular if the handoff and the waits take more than half of the upper bound.

## Expectations (stated before measuring)

- Compression is 40-60 % of a capture and 5-10 % of a recording frame on the frame fixture; on idle heavy
  configurations (few changed pieces) under 3 %.
- The handoff costs about a 4 KB copy plus an atomic per packed piece (well under 1 µs), so most of the bound is kept
  on the fixture; the gain on idle configurations is in the noise.
- Seeks while recording wait rarely: the worker keeps up within a frame.
