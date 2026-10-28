# 038 — ttd-replay-query-engine

Feature: [docs/inprogress/2026-10-28-ttd-analysis-feedback](../../../docs/inprogress/2026-10-28-ttd-analysis-feedback/README.md)
(phase 2). Phase 1 (one-call queries: coverage-scan lists, reverse-continue `all`, find `changes_only`, memory-at
`cpu`) is on master since `c35d12fd8`.

## Context

A reverse-engineering agent asks TTD questions about a window of a recording, not about one point:

- in which frames did a profile probe fire? An exec, read, write, port-in or port-out probe (`HapticProbe`) over a
  range, on a page or on the CPU address, with a port mask. Owner decision 2026-10-28: TTD answers this as a query;
  replays never drive haptics, sound or the deck;
- every hit of a PC, every write with its old value, every read of a font or table range with the PC that read it;
- the port timeline: AY register writes frame by frame (music);
- an instruction trace or a PC profile of the window (logic, decompressors, self-modifying code).

Today each verb replays on the user's own machine, one frame at a time (~1.3 ms a frame, `SaveLiveState` /
`RestoreLiveState` around it). A verb with a coverage-index prune skips frames that cannot hold the hit, but a
question with no index (reads by PC, probes, port values, traces) replays the whole window. Replaying 50,000 frames
(about 17 minutes of play) at 1.3 ms a frame takes about 65 s, and the user's emulator is held for that time.

The idea: one engine. Replay a window once with a set of observers and collect every observer's answer. The window
is split into checkpoint intervals, and each interval is independent: it starts from its checkpoint and ends at the
next one. The intervals can therefore run on clones in parallel, and the user's machine is not touched.

## Goals

- **G1** Is a replay on a clone exact? A clone is a muted, headless instance of the same model that loads the
  serialized session. Running a stripe of frames forward from a checkpoint must reach the next checkpoint's
  machine state bit for bit, on every workload.
- **G2** How fast is a replay, with and without observers? Measure frames/s for each of these:
  - (a) the live machine, serial (today);
  - (b) one clone;
  - (c) N clones in parallel, N = 1, 2, 4, 8, 16.

  Add the setup cost: serializing the session, then creating and loading each clone.
- **G3** Which observer mechanism gives each answer exactly and cheaply?
  - the breakpoint taps on the clone, for the probe semantics with pages and port masks;
  - the Z80 M1 trace hook;
  - the frame cache, for full instruction records;
  - the TTD access probe.
- **G4** What does a clone cost? Memory per clone, and the user's frame time while clones replay on other cores.

## Variants

Every file uses the same variant names:

- `a-inplace`: today's verbs and frame cache on the source instance, serial.
- `b-clone`: one clone loaded from the serialized session; the stripe is the whole window.
- `c-parallel`: N clones; checkpoint stripes are dealt round robin by contiguous blocks; results are merged in time
  order.

Observers, the same on every variant where the variant can run them:

| Id | Answer | Mechanism |
|----|--------|-----------|
| O0 | none (pure replay speed) | - |
| O1 | probe fired: per probe, the frames, hits per frame, and the first hit's time and PC | breakpoint taps (a POC sink) |
| O2 | every PC hit of an address (= reverse-continue `all`) and every write with its old value (= find `all`) | taps; checked against the phase 1 verbs |
| O3 | reads of a range with their PC (fonts, tables) | read taps |
| O4 | AY register writes per frame (port #FFFD / #BFFD pairs) | port-out taps |
| O5 | PC profile: instructions per PC over the window | M1 trace hook |

## Requirements

- Workloads:
  - **W1:** the synthetic Pentagon program of `ttdanalysisqueries_test.cpp`, whose truth is known. Bank-switched
    routine, a store every pass, a flag that flips. 3,000 and 50,000 frames.
  - **W2:** `testdata/loaders/sna/7threality.sna` on Pentagon 128K, a demo with AY music and paging. 20,000 frames.
  - **W3:** a 48K game with the input journal played back (an RZX or a scripted run), so recorded input is on the
    path.
- Every answer is compared with an oracle:
  - W1: the program's truth;
  - all workloads: today's verbs on the source (`a-inplace`) and the other variants. They must match exactly.
- Determinism (G1): at the end of every stripe, the clone's machine-state hash (`ttd::HashSnapshot`, with the
  peripheral registry) must equal the hash after a seek to that frame (the recorded checkpoint).
- macOS (M-series, 20 cores) for the numbers. Linux gcc build of the POC tool (the zero-warning rule).
- Release builds, `UNREAL_NICE=0`; interleaved repeats, medians.

## Constraints

- The recording hot path is not changed. The POC uses only the public core API. Its code lives in `cpp/` here and is
  not linked into core or the apps (POC guidelines §4).
- A clone never reaches host audio, a host file, the haptics feed, the deck or the activity log. Taps on a clone go
  to the POC sink only.
- No v1 `TimeTravelManager` work. Raw data in `scratch/2026-10-28-poc-038/`.

## Acceptance criteria / expected results

- **Exact (gate):** 0 hash mismatches over every stripe of W1-W3, and every answer equal to its oracle. A model that
  is not exact on a clone is a no-go for `c-parallel` on that model; it keeps `a-inplace`.
- **Speed:** `c-parallel` at N = 8 answers O1-O4 over 50,000 W1 frames in at most 5 s, including the setup. That is
  at least 10,000 frames/s and at least 8x `a-inplace` on the same question. Expected: a clone replays at turbo
  speed, ~0.4-0.7 ms a frame, and scales near-linearly to the performance cores.
- **Observer cost:** O1-O4 taps add at most 30 % to O0, and O5 at most 50 %.
- **Clone cost:** each clone needs at most the session size + 64 MB. With 8 clones replaying, the source's frame
  time rises by at most 5 %.
- **Setup:** serializing the session and loading 8 clones takes at most 1 s for a 50,000-frame session.

Expectation stated before measuring: the taps and the M1 hook are exact, because a clone replays as a Detached
forward run. The cost is dominated by the replay itself. The risk is G1 on clones: devices whose state lives outside
the checkpoint, and media reads.
