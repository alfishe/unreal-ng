# E1 — Chain length limit per piece

Part of the [TTD v2 Phase 1 experiments](../README.md). Design under test: [Phase 1 TDD §4.3](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md) (Step 2).

## Goal

Choose the chain length limit K, and decide whether pieces need staggered limits, by replaying real recordings under:
- the v1 key frame: all non-zero memory stored again in full every 50 frames;
- a per-piece limit K: a piece is stored in full only when its own chain would reach K links.

Questions:
1. What does dropping the key frame save in stored bytes?
2. Does the spike disappear, and are staggered limits needed to keep busy pieces from reaching their limit on the same frame?
3. How does K trade stored bytes against the decode work of a seek?

## Method

- **Input:** every session of the [data set](../README.md#data): the 5 fixtures in `testdata/ttd` (Pentagon 128K, 300 frames each) and 12 recorded benchmark-matrix cases (1,500 frames each: ZX-Evo idle and with GS512 + MoonSound + TSFM, ATM710 turbo, Scorpion, Pentagon demo / game / GS upload / MoonSound upload / disk loading, Pentagon 1024, Profi).
- **Regions:** machine RAM, and the General Sound RAM where the session has a classic GS card (v1 keeps it inside the GS device blob).
- **Change history:** [`common/ttdhistory.py`](../common/ttdhistory.py) finds the pieces whose **content** changed between consecutive checkpoints. v1 key frames give unchanged pieces new slots, so slot ids alone would overstate change.
- **Sizes:** [`common/piecestats.py`](../common/piecestats.py) compresses each change with zstd level 1, both as XOR and as a full piece. The sizes match the payloads the emulator stored: 0.0% difference on 400 Full pieces of the ZX-Evo session.
- **Encoder:** v1's — keep the smaller of XOR and full; a Full piece resets its chain, a Zero piece has no chain.
- **Policies:**
  - the v1 key frame every 50 checkpoints;
  - a per-piece limit K = 16 / 32 / 50 / 64 / 100;
  - each limit also **staggered**: every piece gets its own limit in [K/2, K].
- **Metrics per policy:**
  - stored bytes per frame;
  - **forced Full stores per frame**: pieces stored in full only because of the key frame or the limit, which is the spike;
  - **restore links**: chain links summed over all non-zero pieces, the decode work of a seek that restores all memory;
  - the deepest chain.

## Run

```bash
../common/record-datasets.sh        # once: records the matrix sessions into <repo>/scratch/ttd-experiments/sessions
python3 run.py                      # writes results.md
```

## Results

Full tables per session: [results.md](results.md). Summary over all 17 sessions (bytes as the geometric mean of the ratio to v1):

| Policy | Bytes vs v1 | Worst forced Full / frame | Worst restore links p99 | Deepest chain |
|---|---|---|---|---|
| v1 key frame / 50 | 1.000 | 30 | 403 | 49 |
| K = 16 | 0.739 | 4 | 202 | 15 |
| K = 32 | 0.572 | 4 | 296 | 31 |
| **K = 50** | **0.506** | **4** | **387** | **49** |
| K = 64 | 0.477 | 4 | 565 | 63 |
| K = 100 | 0.442 | 4 | 774 | 99 |
| K = 50 staggered | 0.550 | 3 | 333 | 49 |

Examples (bytes per frame, v1 → K = 50):

| Session | v1 | K = 50 | Forced Full max, v1 → K = 50 |
|---|---|---|---|
| ZX-Evo idle (RAM) | 584 | 277 | 17 → 4 |
| Pentagon demo 2 (RAM) | 2,100 | 770 | 30 → 3 |
| Across the Edge fixture (RAM) | 1,938 | 611 | 27 → 3 |

## Analysis

- **Most of v1's stored bytes are key frames.** With the same worst chain depth (49), K = 50 stores about half as many bytes, because a piece that does not change is never stored again.
- **The spike is gone without staggering.** On real recordings, pieces change at different moments, so per-piece limits are reached on different frames by themselves: at most 4 forced Full stores per frame across all 17 sessions, against up to 30 for the v1 key frame.
  - Staggering lowers that to 2–3 and shortens restore chains slightly, but costs 5–9% more bytes.
  - The synchronized case it guards against (every piece changing every frame since the same baseline) does not occur in the data.
- **K trades bytes against seek work almost linearly.** From K = 50 to K = 32: +13% bytes, −24% worst restore links. Beyond K = 64 the gain in bytes is small (−3% per step) and seek work grows fast.
- **Little of a 4 MB machine is in use.** ZX-Evo at the BASIC prompt has 34 non-zero pieces out of 1,024. v1's key frame therefore costs in proportion to *used* memory. The 190 µs per capture measured in Phase 0 comes from the 4 MB delta-base copy and the dense reference table (Steps 3 and 4), not from the key frame.
- **Limits of this experiment:**
  - restore links count decode steps, not microseconds; [E2](../e2-encode-once/README.md) measures the time per step;
  - only regions present in v1 files were analyzed: machine RAM and classic GS RAM. MoonSound wave memory, NeoGS memory and the EEPROMs are not recorded by v1.

## Conclusions

1. **K = 50, no staggering** as the starting value for Phase 1, Step 2. This is the same worst chain as today, half the stored bytes, and no spike.
2. **Drop staggered limits from the design.** The data shows no synchronized re-store; staggering only costs bytes. If a synthetic workload ever shows the spike, staggering is a local change in the encoder.
3. **Reconsider K after Step 6** (restore only differing pieces). Once a seek decodes only the pieces that differ, full-restore links matter less, and a larger K (64) may pay off. Measure it with [E4](../e4-restore-differences/README.md).
