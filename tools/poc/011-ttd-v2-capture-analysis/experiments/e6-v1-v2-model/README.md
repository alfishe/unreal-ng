# E6 — TTD v1 and v2 data model on real-use sessions

Part of the [TTD v2 Phase 1 experiments](../README.md). Designs under test: the [migration roadmap](../../../../../docs/inprogress/2026-09-25-ttd-v2-migration/README.md), Phases 1, 2, 4 and 5, with the parameters [E1](../e1-chain-limit/README.md)–[E4](../e4-restore-differences/README.md) measured.

## Goal

How much memory and how much file a recording will take with v2, against v1, on real use (an idle machine, a game, three demos), and why. Speed is out of scope. The cost of a seek is counted as work (XOR links to decode), because a smaller recording can make seeks slower.

## Method

- **Input:** six sessions recorded with the benchmark harness by [`common/record-real-sessions.sh`](../common/record-real-sessions.sh), 1 minute and 5 minutes each:
  - Pentagon 128 at the BASIC prompt;
  - Pentagon 128 playing a game with scripted input;
  - Pentagon 128 running 7th Reality, Across the Edge, Eye Ache;
  - ZX-Evo at the BASIC prompt.

  The Pentagon configuration has its default cards: NeoGS, MoonSound, TurboSound FM.
- **[`model.py`](model.py)** computes for each stream the bytes held in memory and written to the file, v1 and v2.
  - **v1** is modeled from what the engine does: key frames every 50 frames, payloads allocated at `ZSTD_compressBound`, dense reference tables, whole device blobs in every checkpoint, a 12-byte-per-write journal in a ring of 8,388,608 writes.
  - **v2** as designed:

    | Stream | v2 design | Phase |
    |---|---|---|
    | Memory pieces | each change stored once, encoded once (T = 128 B), at most K = 50 links per piece, exact-size payloads with a 16-byte slot header | 1 |
    | Reference table | copy-on-write blocks of 8 pages, two levels | 1 |
    | Device state | stored only when changed, as the compressed XOR against the previous version | 2 |
    | Write journal and coverage | kept in memory as the compressed blocks the file already uses, whole history | 4–5 |

- **The v1 model is checked against v1 first.** The benchmark's measured heap split ([E5](../e5-heap-split/README.md)) and the real file size are the reference. One input is taken from the measurement, because no file records it: the coverage index's working set (seen-bitmaps, open block, caches, 3–5 MB). v1 and v2 hold it alike.
- **Seek work:** mean number of XOR links a restore decodes per non-zero piece, at a random checkpoint.

## Run

```bash
../common/record-real-sessions.sh   # once: ~15 minutes, ~1 GB in scratch/ttd-experiments/real/
python3 run.py                      # writes results.md (~10 minutes)
```

## Results

Full tables, per stream and per session: [results.md](results.md).

**The v1 model matches v1.**

| Measure | Agreement |
|---|---|
| Memory, the ten Pentagon sessions | within 0.6% |
| Memory, ZX-Evo | +2% |
| File | within 5% |

**One minute of recording, MB:**

| Session | v1 memory | v2 memory | v1 file | v2 file |
|---|---|---|---|---|
| Pentagon 128, BASIC prompt | 23.8 | 6.4 | 3.9 | 3.2 |
| Pentagon 128, game | 192.9 | 35.1 | 32.8 | 30.0 |
| 7th Reality | 96.8 | 15.7 | 17.5 | 12.2 |
| Across the Edge | 211.5 | 32.3 | 32.5 | 27.8 |
| Eye Ache | 195.9 | 41.4 | 35.0 | 36.9 |
| ZX-Evo, BASIC prompt | 100.7 | 9.9 | 24.0 | 5.9 |

**Five minutes of recording, MB held at the end:**

| Session | v1 memory | v2 memory | v1 file | v2 file |
|---|---|---|---|---|
| Pentagon 128, BASIC prompt | 104 | 19 | 19 | 16 |
| Pentagon 128, game | 683 | 137 | 76 | 131 |
| 7th Reality | 198 | 33 | 60 | 30 |
| Across the Edge | 350 | 58 | 66 | 53 |
| Eye Ache | 539 | 188 | 81 | 183 |
| ZX-Evo, BASIC prompt | 489 | 36 | 120 | 29 |

v1's journal ring is full after 0.8–3.2 minutes on the active sessions. From then on, v1's memory and file keep only the last 8.4 million writes (under the last minute of Eye Ache), while v2 keeps the whole history. Where v2's file is larger (game, Eye Ache), it holds more.

**Seek work:**

| Session length | v1 links | v2 links |
|---|---|---|
| 1 minute | 2.6–4.9 | 4.8–8.1 |
| 5 minutes | 0.8–4.9 | 4.8–12.0 |

## Analysis

- **v2 memory is 3–14× smaller than v1.** The largest single cause is not a v2 mechanism: v1 allocates every stored piece and coverage block at `ZSTD_compressBound` (E5). Exact-size payloads alone take most of it.
- **Memory pieces, v2:** 0.2–3.5 MB per minute against 13–93 MB in v1. Storing each change once (no key frames) halves the stored bytes (E1). Exact allocation removes the rest.
- **The write journal decides everything else.**
  - 870–3,500 memory writes per frame on the demos and the game, 2.5–3.4 bytes per write compressed: 7–31 MB per minute in v2, in memory and in the file.
  - It is most of the v2 file on every active session, and it grows without a bound once the history is kept whole, which v1's ring does not do.
  - Phase 4 has to decide what to keep. The deciding fact is that the journal is reproducible: with the inputs recorded (sealed replay), re-running a frame regenerates its writes exactly. It could be kept only near the current position, or regenerated on demand.
- **Device state is a constant 2 MB per minute,** and only partly reduced by v2. Every frame changes the state of the default cards: the MoonSound, NeoGS and TurboSound FM blobs (386, 191 and 189 bytes) carry timers and counters. They change although no music plays. Storing the XOR of a changed blob saves only a quarter. Phase 2 should store the fields that changed, and treat counters that advance with time as derived from time, not as state.
- **Reference tables:** v2 removes them on large machines (ZX-Evo 12.3 → 1.6 MB per minute in memory, 0.8 in the file). On 128 KB machines they are 0.4 MB either way.
- **Seeks get longer chains:** up to 12 links per piece on average at 5 minutes, against v1's at most 5. E4 measured about 60 µs for a busy piece at depth 49. With Step 6 (restore only differing pieces) that stays far inside the 5 ms budget, but it is the price of the K = 50 saving and is measured, not assumed.

## Limits

- Modeled, not run: the v2 numbers come from the recorded content with the v2 rules applied. The C++ v2 must reproduce them.
- The v1 files leave out MoonSound wave memory, NeoGS memory and the EEPROMs, so v2's cost for them is not counted. None of these sessions uses them.
- Journal compression is the file's columnar format at zstd 1. A v2 in-memory journal may choose differently.
- The per-minute rates vary with content: the first minute of Across the Edge is its busiest, so 1-minute and 5-minute rates differ.

## Conclusions

1. **Fix the v1 allocation first.** Exact-size payloads release half or more of a recording's memory today, independent of v2. It is a small core change with its own tests (noted in E5).
2. **v2 Phase 1 then removes most of what remains of memory pieces and reference tables.** The numbers support its design as is: K = 50, T = 128, 8-page blocks.
3. **The write journal is the next design decision, before Phase 4.** It is the largest stream in memory and in the file on any active session. Next experiment: what keeping the journal only for a window around the current position, and regenerating the rest by replay, would cost.
4. **Phase 2 has to treat free-running device counters as derived from time.** Otherwise idle cards cost 2 MB per minute forever.
5. **This model is the reference for the C++ v2.** A standalone v2 fed with these sessions frame by frame has to match these bytes, and restore every frame bit-exact against v1.
