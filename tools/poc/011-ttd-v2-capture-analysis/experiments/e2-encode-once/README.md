# E2 — Encode a changed piece once

Part of the [TTD v2 Phase 1 experiments](../README.md). Design under test: [Phase 1 TDD §4.8](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md) (Step 7).

## Goal

v1 compresses every changed 4 KB piece twice, as the XOR difference from its previous version and as the full piece, and keeps the smaller (`InternXor`, `ttdcodecpagestore.cpp`). Phase 1 proposes compressing the full piece only when the XOR result is large. Questions:
1. How often does the full piece win at all?
2. Which XOR-size threshold T keeps stored bytes (almost) unchanged while saving most of the encode time?
3. What do the zstd calls cost on real pieces with the emulator's own zstd? [E4](../e4-restore-differences/README.md) also uses the decode times.

## Method

- **Sizes:** every real content change in all 17 input sessions ([data set](../README.md#data)), 70,283 changes with non-zero new content. Each change is compressed with zstd level 1 both as XOR and as full ([`common/piecestats.py`](../common/piecestats.py)). Python's zstd sizes match the emulator's stored payloads byte for byte.
- **Times:** [`zstdtiming.cpp`](zstdtiming.cpp), built by [`build-timing.sh`](build-timing.sh) against the static zstd in the emulator's build directory. It is called exactly as `ttdcompression.h` does: `ZSTD_compress` / `ZSTD_decompress`, level 1, no context reuse.
  - Input: 3,628 real (previous, new) piece pairs, an even sample of the changes of every session.
  - Every call is timed 15 times and the minimum kept; the host was loaded (load average 80–120), and load only ever adds time.
- **Policy under test:** compress the XOR; compress the full piece too only when the XOR result is larger than T; keep the smaller.
- **Reported:** the share of changes where the full compression is skipped, stored bytes relative to v1, and encode time relative to v1. Encode time is modeled from the measured per-call times of the sample.

## Run

```bash
../common/record-datasets.sh     # once
python3 run.py                   # builds zstdtiming, writes results.md and timing.json
```

## Results

Full output: [results.md](results.md). Per-call times, µs per 4 KB piece:

| Call | mean | p50 | p99 |
|---|---|---|---|
| compress XOR | 2.06 | 1.46 | 7.52 |
| compress full | 8.75 | 9.62 | 12.83 |
| decompress XOR | 1.20 | 0.96 | 3.99 |
| decompress full | 3.94 | 4.29 | 5.71 |

XOR size of a change (zstd-1): p50 35 B, p90 216 B, p99 1,160 B, max 3,460 B. The full piece is smaller than the XOR in **4.99%** of changes.

| T (bytes) | Full compression skipped | Stored bytes vs v1 | Encode time vs v1 |
|---|---|---|---|
| **128** | 83.0% | **1.0007** | **0.284** |
| 256 | 93.0% | 1.0082 | 0.233 |
| 512 | 97.5% | 1.0159 | 0.222 |
| 1,024 | 99.0% | 1.0365 | 0.218 |
| 2,048 | 99.2% | 1.0394 | 0.214 |
| never compress full | 100% | 1.0459 | 0.191 |

## Analysis

- **The second compression is almost always wasted.** It costs 4× the XOR compression and wins in 5% of changes; those are pieces rewritten wholesale (loading, decompression, screen clears).
- **Small differences never need the full try.** Below T = 128 the full piece practically never wins (+0.07% bytes when it is skipped), and 83% of all changes are that small.
- **The curve flattens early.** From T = 128 to T = 1,024 encode time drops only from 0.28 to 0.22 of v1, while stored bytes grow from +0.07% to +3.7%. The 1 KB starting value proposed in the TDD gives up 3.7% of bytes for almost nothing.
- **Decode costs, for E4 and seek estimates:** 1.2 µs per XOR link and 3.9 µs per Full piece, as the emulator calls zstd today.
- **Limit:** times are minimums on a loaded host. They show how much the work costs, not its worst case under load.

## Conclusions

1. **T = 128 bytes** for Phase 1, Step 7: encode time per changed piece drops to 0.28 of v1 (3.5× faster), and stored bytes stay within 0.1% of v1.
2. **T = 256 is the alternative** if capture time matters more than 0.8% of stored bytes: 4.3× faster.
3. **Correct the TDD:** the starting threshold is 128 B, not 1 KB.
4. **Untested idea, not part of Phase 1:** the emulator calls zstd without a reused context, so every call also sets one up. Whether a reused context in `ttdcompression.h` saves time was **not measured** here; it would be its own A/B step.
