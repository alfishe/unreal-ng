# E5 — Where v1's recording memory goes

Part of the [TTD v2 Phase 1 experiments](../README.md). Tool: the [TTD benchmark harness](../../../../verification/ttd-bench/README.md) and its new BM-4 split.

## Goal

The benchmark reported the memory a recording holds as one number (`bm4_resident_bytes`). Against the bytes of the streams it records (`bm3_*`), 15–95 MB per minute were unexplained. Questions:
1. Where does that memory go?
2. Which parts would v2 remove, and which would it not touch?

## Method

- `TimeTravelManager::GetHeapBreakdown()` splits the session heap into the parts `EstimateSessionHeapBytes()` adds up. `sessionHeapBytes` is now that sum, so the total is unchanged.
- Unused allocation is reported where it can be known exactly: the page-store payloads, the coverage blocks, the port-journal blocks (capacity minus content), and the write journal's committed but unwritten ring space.
- The harness reports each part as `bm4_heap_<part>_bpf`. The CI gate checks that the parts sum to `bm4_resident_bpf`.
- **Input:** the six real-use sessions of [`common/record-real-sessions.sh`](../common/record-real-sessions.sh), 1 minute each (3,000 frames).

## Run

```bash
../common/record-real-sessions.sh     # writes scratch/ttd-experiments/real/{1min,5min}/bench.json
```

## Results

MB held after one minute of recording (`bm4_heap_*` × frames):

| Part | Pentagon 128, BASIC | Pentagon 128, game | 7th Reality | Across the Edge | Eye Ache | ZX-Evo, BASIC |
|---|---|---|---|---|---|---|
| pieces, content | 0.2 | 5.3 | 6.5 | 6.7 | 5.9 | 1.8 |
| **pieces, unused allocation** | **13.4** | **70.7** | **45.5** | **85.4** | **59.1** | **52.7** |
| slot table | 0.2 | 1.3 | 0.7 | 1.3 | 0.7 | 0.7 |
| reference tables | 0.4 | 0.4 | 0.4 | 0.4 | 0.4 | 12.3 |
| device state | 2.7 | 2.8 | 2.7 | 2.7 | 2.8 | 6.7 |
| checkpoint structs | 0.9 | 0.9 | 0.9 | 0.9 | 0.9 | 0.9 |
| write journal | 2.4 | 84.1 | 31.5 | 90.4 | 100.7 | 16.5 |
| coverage | 3.7 | 28.0 | 9.2 | 24.1 | 25.7 | 6.9 |
| of which unused allocation | 0.5 | 21.8 | 5.3 | 19.2 | 20.9 | 3.6 |
| port journals, frame cache | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| **total** | **23.7** | **193.5** | **97.3** | **211.9** | **196.0** | **98.4** |

## Analysis

- **The unexplained memory is unused allocation, and its cause is one line.** `ttd::Compress` (`core/src/debugger/ttd/ttdcompression.h`) allocates its output at `ZSTD_compressBound`, 4,174 bytes for a 4 KB piece, and then shrinks the vector's size, not its capacity. Every stored piece therefore holds about 4 KB of heap, whatever it compressed to. On Across the Edge the content is 6.7 MB and the allocation 92 MB. Coverage blocks go through the same function, which adds another 5–22 MB per minute.
- **The write journal is the other big part.** It holds 12 bytes per memory write, uncompressed. A ring caps it at 8,388,608 writes (100.7 MB: the nominal 64 MB, rounded up to a power of two in records). Eye Ache fills the ring within the first minute, and from then on the oldest writes are dropped.
- **The reference tables matter only on large machines:** 12.3 MB per minute on ZX-Evo, 0.4 MB on 128 KB machines.
- **Device state is small but constant:** 2.7 MB per minute even at the BASIC prompt (see [E6](../e6-v1-v2-model/README.md) for which devices).
- **Port journals and the frame cache hold nothing** in these sessions.

## Conclusions

1. **Exact-size payloads are a v1 fix of their own, worth more than any v2 step for memory.** Shrinking the compressed output to its size (`shrink_to_fit`, or compressing into a reused scratch buffer and copying the exact bytes) would release 13–85 MB per minute of pieces and up to 22 MB per minute of coverage, roughly halving memory on active content, with no format change. It is not done here: it changes core code and gets its own change with tests.
2. **After that, the write journal dominates** active content: 30–100 MB per minute, capped by a ring that loses history. That is Phase 4 / 5 territory: [E6](../e6-v1-v2-model/README.md) puts numbers on keeping it compressed.
3. **The BM-4 split stays in the benchmark.** The parts are allocator-dependent, so `ttd_bench_compare.py` compares them with the heap tolerance. The CI gate keeps only their total, and checks that the parts add up to it.
