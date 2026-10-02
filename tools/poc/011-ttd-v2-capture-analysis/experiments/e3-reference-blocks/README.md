# E3 — Copy-on-write page reference table

Part of the [TTD v2 Phase 1 experiments](../README.md). Design under test: [Phase 1 TDD §4.5](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md) (Step 4).

## Goal

Every checkpoint needs to know which stored piece holds each 4 KB piece of memory. v1 writes this whole table, 4 bytes per piece, into every checkpoint: 4 KB per frame on a 4 MB machine, even when nothing changed. Phase 1 cuts the table into blocks that are shared between checkpoints until one of their pieces gets a new slot. Questions:
1. What block size minimizes table bytes per frame, in memory and in the file?
2. Does the second level (sharing the per-region block table itself) pay off?
3. Is PR-10 met: at most 64 bytes for a frame in which nothing changed?

## Method

- **Input:** the real change history of every region of all 17 input sessions ([data set](../README.md#data); [`common/piecestats.py`](../common/piecestats.py)).
- **For each checkpoint:** the blocks containing a changed piece are the blocks that must be cloned.
- **Costs per frame:**

  | Layout | Cost |
  |---|---|
  | v1 dense | 4 B per piece |
  | One level | 8-byte pointer per block + the cloned blocks |
  | Two levels | 8-byte pointer per region + the block table when a block changed + the cloned blocks |
  | File, sparse | 4-byte count + 10-byte (region, position, block index) per changed block + the new block's contents |

- **Block sizes:** 4, 8, 16, 32 and 64 pages of 16 KB (16 to 256 pieces).
- **Not modeled:** extra slot changes from the chain length limit. [E1](../e1-chain-limit/README.md) measures at most 4 per frame, and usually none.

## Run

```bash
../common/record-datasets.sh     # once
python3 run.py                   # writes results.md
```

## Results

Full tables per session: [results.md](results.md). All inputs summed, relative to v1's dense table:

| Block (pages) | One level, memory | Two levels, memory | File, sparse |
|---|---|---|---|
| 4 | 0.237 | 0.224 | 0.135 |
| **8** | **0.215** | **0.214** | **0.170** |
| 16 | 0.255 | 0.260 | 0.239 |
| 32 | 0.359 | 0.368 | 0.357 |
| 64 | 0.568 | 0.576 | 0.571 |

Bytes per frame, examples:

| Session, region | v1 dense | 8-page blocks, two levels (memory / file) |
|---|---|---|
| ZX-Evo idle, RAM (1,024 pieces) | 4,096 | 520 / 280 |
| Pentagon demo 2, RAM (32 pieces) | 128 | 144 / 142 |
| Pentagon GS upload, GS RAM (128 pieces) | 512 | 17 / 12 |
| ZX-Evo + GS512 idle, GS RAM | 512 | 8 / 4 |

## Analysis

- **The big win is on large machines.** On ZX-Evo the table drops from 4,096 B to about 520 B per frame in memory and 280 B in the file. At 50 frames per second that saves about 175 KB/s of memory, and about 114 MB of file per 10 minutes for the table alone (v1: about 120 MB per 10 minutes, the "~720 MB per hour" of `current-state.md`).
- **Changes are spread out.** On ZX-Evo idle, the 4 changed pieces per frame fall into 3 different 4-page blocks: the stack, the screen and the variables live in different pages. That is why small blocks do not win by much, and why one cloned block per frame is the floor.
- **8 pages is the sweet spot overall.** Smaller blocks clone more blocks per frame and add pointers; larger ones copy more bytes per clone. In the file, 4 pages is slightly better (0.135 against 0.170), because the file pays no pointers.
- **The second level matters for device memory, not for RAM.** Machine RAM changes a block in nearly every frame, so its block table is cloned anyway. Device memory is mostly idle (a GS card after its upload), and there the second level cuts 68 B to 15 B per frame (4-page blocks) and to 8 B when nothing changes.
- **PR-10 (≤ 64 B per unchanged frame) holds with two levels:** 8 B in memory and 4 B in the file per idle region. One level would cost a pointer per block every frame (128 B for ZX-Evo RAM alone at 16-page blocks).
- **Small machines gain nothing,** and lose nothing that matters: a Pentagon 128 table is 128 B, and a single block plus its pointer is 144 B.

## Conclusions

1. **Blocks of 8 pages** (32 pieces, 128 B) for Phase 1, Step 4, instead of the 16 assumed in the TDD.
2. **Keep the two levels:** required for PR-10 on idle device regions, and free for RAM.
3. **Sparse file records as designed:** 4-page blocks would save a little more in the file, but one block size for memory and file keeps the code simple. Revisit only if file size becomes the bottleneck (Phase 5).
