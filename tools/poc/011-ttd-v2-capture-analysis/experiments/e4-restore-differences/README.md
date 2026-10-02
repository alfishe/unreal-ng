# E4 — Restore only the pieces that differ

Part of the [TTD v2 Phase 1 experiments](../README.md). Design under test: [Phase 1 TDD §4.7](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md) (Step 6).

## Goal

A v1 seek writes every piece of memory back into live RAM (`RestoreRamPages`). Phase 0 measured that as the largest part of a seek on big machines: 776–789 µs on ZX-Evo. Phase 1 proposes restoring only the pieces whose stored version differs from what live memory holds, plus the pieces a replay wrote since. Questions:
1. How many pieces differ between the live position and the seek target, for typical seeks?
2. What does that save, in decode time, against a full restore?
3. Where does a full restore spend its time?

## Method

- **Input:** the real change history of every region of all 17 input sessions ([data set](../README.md#data)).
- **Seek pairs**, 400 per session and scenario:

  | Scenario | Seek |
  |---|---|
  | step | one frame back |
  | scrub | 1–10 frames back, as when dragging the timeline |
  | jump | a random position to a random position |

  Each scenario is also evaluated from a position inside the next frame, where live memory also holds what the replay into that frame wrote.
- **Differing pieces:** the pieces that changed between the two positions, plus the replay's writes.
- **Time model:**
  - per non-zero piece: one Full decode plus one XOR link per chain step. Per-call times come from [E2](../e2-encode-once/README.md) (`timing.json`: 3.94 µs per Full, 1.2 µs per link). Chain depths are simulated under the Phase 1 rule (per-piece limit K = 50, v1 encoder);
  - per all-zero piece: writing it. v1's baseline stores every piece, zero ones as Zero slots, so a full restore writes all of installed memory.
- **Calibration against the Phase 0 measurement** (BM-6 memory restore p50, `testdata/ttd/bench/`), full restore, jump:

  | Configuration | Zero pieces | Model, decode only | Model + 0.55 µs per zero piece | Measured |
  |---|---|---|---|---|
  | ZX-Evo idle | 1,003 | 210 | 762 | 776–789 |
  | Pentagon 1024 idle | 251 | 50 | 188 | 189 |
  | Scorpion idle | 55 | 65 | 95 | 85–87 |
  | ATM710 turbo idle | 243 | 236 | 370 | 259–277 |
  | Pentagon demo | 0 | 348 | 348 | 269–290 |

  The 0.55 µs per zero piece is fitted on the large machines. Decode times from E2 run high by up to 25% on busy small machines (they are minimums taken under heavy load, against the benchmark's medians), so absolute figures carry about ±25%.

## Run

```bash
../common/record-datasets.sh     # once
(cd ../e2-encode-once && python3 run.py)   # timing.json
python3 run.py                   # writes results.md
```

## Results

Full tables: [results.md](results.md). p50 / p99 of memory-restore time per seek, µs:

| Session | Scenario | Full restore | Differing only | Pieces decoded (full → differing) |
|---|---|---|---|---|
| ZX-Evo idle | step | 764 / 869 | 145 / 251 | 21 + 1,003 zero → 4 |
| ZX-Evo idle | jump | 761 / 869 | 143 / 251 | 21 + 1,003 zero → 4 |
| ATM710 turbo idle | step | 368 / 480 | 85 / 289 | 13 + 243 zero → 2 |
| Pentagon 1024 idle | jump | 188 / 216 | 34 / 63 | 5 + 251 zero → 1 |
| Pentagon demo 2 | step | 308 / 459 | 198 / 354 | 32 → 7 |
| Pentagon demo 2 | jump | 294 / 459 | 207 / 373 | 32 → 10 |

Worst p99 over all sessions: full restore 869 µs; differing only 354 µs for step and scrub, 490 µs for jump.

## Analysis

- **Most of a full restore on a big machine writes zeros.** ZX-Evo at the BASIC prompt has 21 non-zero pieces out of 1,024. The other 1,003 are rewritten with zeros on every seek, about 550 of its 760 µs. Restoring only what differs removes that entirely: 5.3× faster on ZX-Evo, 5.5× on Pentagon 1024.
- **What differs is exactly what is expensive.** The pieces that change between two positions are the busy ones (stack, screen, variables), and those have the longest chains. On a fully used 128K machine (Pentagon demo 2) restoring only differing pieces saves about a third, not more, because most of the cost is in those busy pieces.
- **The direction of the seek hardly matters.** Step, scrub and jump differ little, because a few hot pieces change in almost every frame. Starting from inside a frame adds the replay's writes, which are those same pieces.
- **What is left is chain decoding,** about 60 µs for a busy piece at depth 49. This is where K acts after Step 6: a smaller K shortens it, at the cost in bytes measured by [E1](../e1-chain-limit/README.md).
- **Limits:**
  - modeled, not measured, with about ±25% on absolute values;
  - a piece is counted as differing if it changed anywhere between the two positions, even when it changed back, which matches comparing slot ids;
  - only RAM and classic GS RAM (the regions in v1 files).

## Conclusions

1. **Step 6 is worth more than the TDD assumed.** On large-memory machines it removes the largest part of a seek, the write of untouched zero memory: an estimated 5× faster memory restore on ZX-Evo, with p99 under 0.5 ms on every input.
2. **Keep K = 50.** With Step 6 in place, the remaining seek cost is the chains of a few busy pieces. That is well inside PR-5 (5 ms), so the bytes K = 50 saves (E1) matter more than a shorter chain.
3. **Add to the TDD:** the full restore's main cost is writing zero pieces. Even before Step 6, a restore that skips Zero pieces already zero in live memory would recover most of the ZX-Evo gain.
4. **Measure it after implementation:** the BM-6 memory figure of the benchmark matrix is the check. Expected: ZX-Evo memory restore p50 from about 780 µs to about 150 µs.
