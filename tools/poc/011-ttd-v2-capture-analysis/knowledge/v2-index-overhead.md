# V2 Index Overhead

## The Metadata Cost of Tiered Storage
The page-granular approach employed by TTD v2 requires an indexing subsystem to track where the active pages reside within the tier storage model (the fast Hot Buffer in RAM, or flushed to the File Stream on disk). 

The indexing architecture comprises:
- **PageSlotEntry (20 bytes + hash overhead)**: Tracks a specific 4KB page, maintaining its position within the hot buffer and its eventual absolute offset within the file stream.
- **FrameIndexEntry (36 bytes + 4 bytes per modified page)**: A checkpoint lookup dictionary binding a specific frame (by T-states or frame number) to the array of Page Slots that it modified.

## Experimental Setup
We modeled the indexing overhead via `ttd_v2_index_overhead_bench`, calculating the total structural cost relative to the allocated Hot Buffer over a standard 5-minute session (15,000 frames) with a typical dirty page rate.

See: [`../benchmarks/ttd_v2_index_overhead_bench.cpp`](../benchmarks/ttd_v2_index_overhead_bench.cpp)

## Empirical Results

| Hot Buffer Size | Page Capacity | Page Index (MB) | Frame Index (KB) | Total Index (MB) | Overhead (%) |
|-----------------|---------------|-----------------|------------------|------------------|--------------|
| 64 MB           | 16,384        | 0.49 MB         | 645 KB           | 1.12 MB          | **1.75%**    |
| 128 MB          | 32,768        | 0.97 MB         | 645 KB           | 1.60 MB          | **1.25%**    |
| 256 MB          | 65,536        | 1.95 MB         | 645 KB           | 2.58 MB          | **1.01%**    |
| 512 MB          | 131,072       | 3.90 MB         | 645 KB           | 4.53 MB          | **0.88%**    |
| 1024 MB         | 262,144       | 7.80 MB         | 645 KB           | 8.43 MB          | **0.82%**    |

*Assuming 2 dirty pages per frame on average.*

## Storage Strategy & Recommendations
Because the `FrameIndexEntry` scales solely with session length, its footprint is exceptionally stable (~644 KB per 5 minutes). The only variable overhead is the `PageSlotEntry` table, which is fixed at allocation relative to the Hot Buffer size.

By keeping the index metadata entirely separate from the payload pages, seeking becomes an O(1) hash lookup, avoiding expensive sequential stream decoding.

### Hot Buffer Eviction Protocol
When the Hot Buffer nears capacity:
1. Identify the oldest pages that have not been referenced by the most recent N frames.
2. Flush these pages to the File Stream sequentially.
3. Update their `PageSlotEntry::hotBufferOffset` to 0, leaving their `fileOffset` pointing to the persistent stream.
4. On a `SeekTo` event, if the page is missing from the hot buffer, fetch directly from disk using `fileOffset`.

### Recommended Configurations
- **Standard Emulation (48K/128K):** A 64MB Hot Buffer provides >15,000 page slots. Eviction to disk will practically never trigger during a standard 5-minute session.
- **Advanced Emulation (ZX-Evo + GS):** A 256MB Hot Buffer is recommended, accommodating the massive burst rates when the GeneralSound peripheral loads megabytes of audio samples simultaneously. 

## Conclusion
The indexing footprint scales extremely well. For all practical configurations, the metadata overhead remains **under 2%** of the allocated Hot Buffer memory. This explicitly validates the tiered storage indexing architecture for TTD v2.
