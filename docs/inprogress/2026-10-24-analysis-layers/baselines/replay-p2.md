# P2: replay + L1 cost, and the Manic Miner check

Phase P2 adds L1 (code behaviour) of the analysis layers ([design.md](../design.md), module doc
[analysis-layers.md](../../../emulator/analysis/analysis-layers.md) "L1 code behaviour"): a recording replayed on a
muted analysis instance with per-instruction hooks, every instruction folded into `L1Builder`, then the structures
built. This page measures the plan's gate (at least 5x real time for a 5-minute 48K session) and checks L1 against a
hand-made disassembly.

## Cost

Benchmark `BM_AnalysisReplayL1` (`core/benchmarks/debugger/analysis/analysis_replay_benchmark.cpp`): Garfield
(`testdata/loaders/rzx/oracle/garfield-300.z80`, 48K) recorded by the TTD engine for N frames with no history limit,
captured as a `TraceBundle` (untimed); the timed part is `ReplayBuild`: the analysis instance made from the
recording, the replay, `L1Builder::Build`.

```bash
cmake-build-agent-release/bin/core-benchmarks --benchmark_filter=BM_AnalysisReplayL1 --benchmark_repetitions=3
```

Host: Apple M1 Ultra (20 cores), Release + LTO, 2026-10-26. The machine was loaded by other sessions' builds
(load average 29-35 during the run); the cv over the repetitions is below 1 %.

| Frames | Emulated | Replay + build (median) | Real time | Instructions / s | Builder memory | Routines | Loops | Checkpoints compared | Diverged |
|-------:|---------:|------------------------:|----------:|-----------------:|---------------:|---------:|------:|----------:|---------:|
| 1000 | 20 s | 756 ms | 26.4x | 8.06 M | 0.80 MB | 92 | 38 | yes | 0 |
| 15000 | 5 min | 11080 ms | 27.0x | 8.09 M | 1.19 MB | 138 | 78 | yes | 0 |

Gate (plan.md P2): >= 5x real time. Met with a margin of about 5x. The builder keeps no trace: its memory grows with the
code the session touched (nodes, edges, loop stats), not with its length (0.8 MB for 20 s, 1.2 MB for 5 min).

## Manic Miner

Input: `scratch/reaper-mm/run-B/re/orig.z80` (the run B snapshot), a session of mine recorded through MCP on an
offscreen unreal-qt (`analysis start record=true`, about 1500 frames of title and demo caverns, ENTER, four rounds of
walk / jump with O, P and SPACE, then lives lost to game over; 3600 frames), then `analysis build path=...`. Scripts:
the session `scratch/wt-analysis-layers/scratch/p2/mm_session.py`, the comparison `mm_compare.py` next to it
(not checked in: run artifacts).

Replay: 3600 frames, 17.86 M instructions, 144 checkpoints compared, 0 diverged. L1: 29 routines, 263 blocks, 46
loops (3 delay loops).

### Code entries against mm.skool

The reference (skoolkid's `mm.skool`) has 33 `c` entries at or above $8000.

| | Count | Entries |
|---|---:|---|
| L1 routine entry exactly | 21 | |
| executed, but inside another L1 routine | 2 | $8684 STARTGAME (the title code of $85CC runs into it), $927F DRAWWILLY (inside $923A, the reference's only user of it) |
| never executed in the session | 10 | ENDGAM $8944, CRUMBLE $8BBA, LIGHTBEAM $8D73, EUGENE $8DF8, SKYLABS $8E75, VGUARDIANS $8EF1, NXSHEET $9028, INCSCORE $90EA, KONGBEAST $9135, CHKSWITCH $921B: game over, crumbling floor, later caverns, cavern completion, an item collected; the session never got there |

So 21 of the 23 entries the session ran start an L1 routine. L1 also has 8 entries the reference does not start:
$8691 $87A2 (the main loop entered by jumps, which the reference keeps inside StartCavern / the main loop),
$8A8A $8B82 $8B96 $8B9C $8C83 $92D5 (labels inside reference routines that are called or jumped into from
elsewhere).

`score_draft.py` on a ctl draft made from L1 alone (c at the routine entries, b where execution stops):
code entries 21 of 33; code vs non-code bytes 93.2 % agree; every byte L1 calls code is code in the reference
(1236 of 1236); 2227 code bytes stay data because the session never ran them.

### Record walks against manic-miner-records.json

| Record | Expected | What L1 found |
|--------|----------|---------------|
| cavern_buffer $8000, 512 bytes, copies at $B000 + $400n + $200 | STARTGAME (34436) copies 512 bytes | loop $86AB: 512 trips, read and write stride +1, `ld hl` / `ld de` immediates with $8000 used at $86AB |
| guardians $80BE, 7 bytes x 4 | IY walks 7-byte records | routines $8D0F and $8DAA: `IY+0,+1,+4,+5,+6 step 7` and `IY+0..+4 step 7`, bases $80BE, $80C5 (the session's caverns had two horizontal guardians); immediate $80BE at both entries |
| items $8075, 5 bytes x 5 | IY walks 5-byte records | routine $8F63: `IY+0..+3 step 5`, bases $8075, $807A, $807F, ...; immediate $8075 |
| conveyor $806F, 4 bytes | read by the conveyor routine | no walk (one record, absolute loads); routine $9105 (MVCONVEYOR) reads exactly $806F-$8072, its loop of 20 trips writes two pixel rows |
| willy_state $8068, 7 bytes | Willy's fields | immediate $8068 at $8AC9 (114 uses); constant compares on $806B (airborne status) and $806E, $8069 |

Four of the five record walks come out as a base + offset pattern or a copy loop; the fifth (one record read
with absolute addresses) shows in the routine's read set.
