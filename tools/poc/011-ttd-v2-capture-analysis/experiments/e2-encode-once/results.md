# E2 results - encode a changed piece once

Changes analyzed (non-zero new content): 70,283. Full piece smaller than the XOR in 4.99% of them. Timing sample: 3,628 real piece pairs, zstd level 1 as the emulator calls it, minimum of 15 runs per call.

## zstd call times (µs per 4 KB piece)

| Call | mean | p50 | p99 |
|---|---|---|---|
| compress XOR | 2.06 | 1.46 | 7.52 |
| compress full | 8.75 | 9.62 | 12.83 |
| decompress XOR | 1.20 | 0.96 | 3.99 |
| decompress full | 3.94 | 4.29 | 5.71 |

## XOR size distribution of changes (bytes, zstd-1)

| p50 | p90 | p99 | max |
|---|---|---|---|
| 35 | 216 | 1160 | 3460 |

## Threshold T: compress the full piece only when the XOR result is larger than T

| T (bytes) | Full compression skipped | Stored bytes vs v1 | Encode time vs v1 |
|---|---|---|---|
| 128 | 83.0% | 1.0007 | 0.284 |
| 256 | 93.0% | 1.0082 | 0.233 |
| 512 | 97.5% | 1.0159 | 0.222 |
| 1,024 | 99.0% | 1.0365 | 0.218 |
| 2,048 | 99.2% | 1.0394 | 0.214 |
| never | 100.0% | 1.0459 | 0.191 |

