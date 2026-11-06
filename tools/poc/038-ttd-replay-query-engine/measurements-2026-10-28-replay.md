# 038 — measurements 2026-10-28: replay on clones

## Setup

- MacBook Pro, Apple M-series, 20 cores (16 performance). macOS 15 (Darwin 24.6). Release build of the worktree
  `scratch/wt-ttd-query-engine` (master `e851b0f35` + this POC), clang, `UNREAL_NICE=0`.
- Tool: `cpp/replaypoc.cpp` (`-DBUILD_TTD_REPLAY_POC=ON`, `tools/build/build.sh ttd-replay-poc`). The command lines
  are in `scratch/2026-10-28-poc-038/run.sh`, and the raw text and JSON files sit next to it.
- Every run follows the same steps:
  1. The source records the workload in turbo, without the write journal.
  2. The session is serialized in memory (`SerializeSession`).
  3. Today's verbs run on the source (`a-inplace`), paged with their cursors.
  4. N clones are created and load the session on N threads (`DeserializeSession`).
  5. Each clone replays a contiguous stripe of frames on its own thread with an observer set. Each frame runs to its
     end with `RunTStates(frameT - t)`, as the controller's own replay does.
  6. At each stripe end the clone's machine is compared with a seek to that frame: CPU, chipset, every device
     (decoded) and every RAM page.
- Clones run without turbo (`POC038_CLONE_TURBO=0`, see "Turbo on the clone").
- The window starts after the baseline checkpoint (`--skip-baseline`, see "The baseline interval").
- Three repeats per cell, median wall time. The other runs went one after another; no other heavy job was on the
  machine.

## Workloads

| Id | Model | Content | Frames | Session file |
|----|-------|---------|-------:|-------------:|
| W1 | Pentagon | the program of `ttdanalysisqueries_test.cpp` (bank-switched routine, a store every pass, a flag) | 50,000 | 8.8 MB |
| W2 | Pentagon | `7threality.sna`, a demo with AY music and paging (100 frames settled) | 20,000 | 7.3 MB |
| W3 | 48K | the ROM editor at the K prompt, a key every 4.5 frames through the live input path (journaled) | 20,000 | 4.6 MB |

Probe targets:
- W1 and W2: exec #8200, write #8103, read #80F0.
- W3: exec #028E (KEY-SCAN), write and read #5C08 (LAST-K).
- AY on every workload: OUT #FFFD / #BFFD, mask #C002.

## Exactness

| Workload | Stripes checked (N = 1, 2/4, 8, 16) | Machine mismatches | Answers vs oracle | Answers across N |
|----------|-------------------------------------|-------------------:|-------------------|------------------|
| W1 50k | 1 + 2 + 4 + 8 + 16 (x3 observer sets) | 0 | equal | equal |
| W2 20k | 1 + 4 + 8 + 16 (x3) | 0 | equal | equal |
| W3 20k | 1 + 4 + 8 + 16 (x3) | 0 | equal | equal |

The oracles, all on the source:
- exec hits = `ReverseContinue` all mode (frame, t, PC, page);
- writes = `FindAccesses` all (frame, PC, address, page, value; t skew 0);
- changes with old values = `FindAccesses` changes_only;
- PC profile = the frame cache over every frame of the window.

The comparison is limited to the clones' window. The verbs also see the part of the last frame after its
checkpoint.

## Speed (median of 3)

W1, 50,000 frames:

| Variant | O0 replay | O1-O4 taps | O5 PC profile | Setup (create + load) |
|---------|----------:|-----------:|--------------:|----------------------:|
| a-inplace (today, serial) | - | exec 20.8 s + writes 53.0 s + changes 53.6 s; reads, probes, AY: no verb | frame cache 106.8 s (2.14 ms/frame) | - |
| b-clone (N=1) | 11.65 s (4,291 f/s) | 12.24 s | 12.31 s | 21 + 72 ms |
| c-parallel N=2 | 5.90 s (8,481) | 6.19 s | 6.23 s | 44 + 78 ms |
| N=4 | 3.01 s (16,639) | 3.19 s | 3.21 s | 88 + 86 ms |
| N=8 | 1.53 s (32,772) | 1.61 s | 1.61 s | 171 + 96 ms |
| N=16 | 0.80 s (62,580) | 0.84 s | 0.83 s | 349 + 127 ms |

Serializing the 50,000-frame session took 85 ms.

W2 (20,000 frames; a demo frame is busier):

| Variant | O0 | O1-O4 | O5 |
|---------|---:|------:|---:|
| a-inplace | - | exec 10 ms (no hit, pruned by the coverage index), writes 150 ms | 54.1 s (2.71 ms/frame) |
| N=1 | 15.5 s (1,292 f/s) | 15.8 s | 19.4 s |
| N=4 | 4.44 s (4,501) | 4.70 s | 4.22 s |
| N=8 | 2.02 s (9,923) | 2.07 s | 2.12 s |
| N=16 | 1.05 s (19,137) | 1.08 s | 1.10 s |

W3 (20,000 frames, with recorded input):

| Variant | O0 | O1-O4 | O5 |
|---------|---:|------:|---:|
| a-inplace | - | exec 20.4 s, writes 2.0 s, changes 2.0 s | 42.2 s (2.11 ms/frame) |
| N=1 | 17.2 s (1,162 f/s) | 17.9 s | 18.1 s |
| N=4 | 4.49 s (4,452) | 4.66 s | 4.71 s |
| N=8 | 2.25 s (8,882) | 2.34 s | 2.36 s |
| N=16 | 1.16 s (17,187) | 1.26 s | 1.28 s |

## Clone cost

- Resident memory per clone, measured as the process growth divided by N:
  - W1: 77 MB at N=1, 55-58 MB at N >= 4;
  - W2: 72 MB at N=1, 42-45 MB at N >= 8;
  - W3: 57 MB at N=1, 35-37 MB at N >= 8.
- The live machine's frame time while the clones replay W1: a separate Pentagon instance, unpaced, 500 frames.

| Clones | O0 | O1-O4 | O5 |
|-------:|---:|------:|---:|
| alone | 0.160 ms | | |
| 1 | 0.165 (+3 %) | 0.161 | 0.163 |
| 2 | 0.165 | 0.164 | 0.163 |
| 4 | 0.167 | 0.165 | 0.168 |
| 8 | 0.167 (+4 %) | 0.167 (+4 %) | 0.168 (+5 %) |
| 16 | 0.166 | 0.167 | 0.198 (+24 %) |

## Turbo on the clone

The same W3 run with turbo on the clones (`POC038_CLONE_TURBO=1`) replays at the same speed. Every stripe ends with
the TurboSound FM device (PeripheralId 4, TSFM) different from the checkpoint. The differing bytes are its render
phases: sample phase, decimation phase and the SSG decimator phases. CPU, chipset, RAM and every hit equal the
oracle.
- `EnableTurboMode(true)`, which keeps the audio, is also not exact.
- Only the replaying machine's turbo matters: a source recorded in turbo replays exactly on a clone without turbo.
- Checked every frame (W2, 3,000 frames, `--verify-every 1`, turbo on the clone): all 2,999 frames differ, 47-53
  bytes of the TSFM render state each (offsets 2-4, 18-28, ...). Turbo diverges at the first frame, not at the end.
- Turbo off costs 6-9 % on W3 (N=1: 1,162 vs 1,237 frames/s; N=8: 2.25 vs 2.06 s). On a short W1 run (3,000 frames,
  N=1) it cost 30 % (4,139 vs 5,985 frames/s).

## The baseline interval

The first interval of a session, from the baseline checkpoint (taken inside a frame by `StartRecording`) to the next
checkpoint, is not exact. The difference is three TSFM time fields:
- byte 51: chip 0's FM clock phase;
- byte 637: chip 1's FM clock phase;
- byte 1222: the render cursor offset.

Every later interval is exact, also when it is checked frame by frame: `--verify-every 1` over 300 frames, 299
checks per observer set, 0 mismatches on W1, W2 and W3.

## Side findings

- A public `SeekTo({frame, 0})` replays the previous frame to compose the picture (`ComposeDisplay` with
  `frameTarget`). This costs about one frame of replay per seek; an M1 hook left armed across the seek counted
  5,683 instructions.
- A seek on a 48K maps #4000 to RAM page 5. The configured RAM size (48 KB, three pages) does not cover it: a tool
  must take the page count from the session (`modelRamPages`).
