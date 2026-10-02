# 02 - What changes in the middle of a frame (batch census)

Feeds: **OPTIMAL** (how large the batches of lines are that a thread pool or a GPU dispatch
gets) and **CHEAP** (how often nothing changes at all).

## Goal

Count, on the real captures, how often something that drawing reads changes while a frame
is being scanned: display list swaps per line (`DLSWAP_LINE`), writes to `RAM_G`, writes to
registers that drawing reads. Each such change forces the lines drawn so far to be drawn
before it (a "batch"). Measure how large the batches are in practice, and how much larger
they could be.

## Why it matters

TS-Labs' first hypothesis: "multithreading is possible only for several screen lines in
parallel, and not when the display list is swapped per line". Both a thread pool (03) and
a GPU dispatch (04a-04c) need many lines at once. eve-emu already draws lazily: a line is
drawn only when something forces it (a `RAM_G` write, a drawing register write, the end
of the frame, a change of the output buffer), so the batch sizes it produces today are
exactly what a parallel renderer would get.

## Method

A library variant `census` (overlay files in `overlay/`: `eve-dl.cpp`, `eve-memory.cpp`,
`eve-timing.cpp`, `eve-copro.cpp` with counters, `eve-poc-stats.h`, `eve-poc-census.cpp`)
counts, for every catch-up that draws at least one line: what triggered it, how many
lines it drew; per frame: how many batches; every swap request and where it was applied;
every `RAM_G` byte written (by the host or by the coprocessor) and drawing register write;
whether the bitmap handle table changed inside a batch; and the time of each drawn line.

For every batch triggered by a `RAM_G` write it also checks whether the written byte lies
in the active display list's **read set**: the `RAM_G` ranges of every bitmap the list
draws (source, stride x layout height, per cell), its palettes and text glyph fonts,
collected by running the list once without drawing. A write outside the read set cannot
change any line of the current list, so the catch-up before it is unnecessary.

```
tools/poc/021-eve-accel/build.sh
tools/poc/021-eve-accel/common/replay.sh census boot --stats --no-hash
tools/poc/021-eve-accel/common/replay.sh census play --stats --no-hash --frames 4000
tools/poc/021-eve-accel/common/replay.sh census zuma --stats --no-hash
```

Outputs: `out/census-*.txt` (kept; small).

## Results

Counts do not depend on the machine load (line timing does, see below).

| | rtype-boot (1032 frames) | rtype-play (first 4000 frames) | zuma-flick (830 frames) |
|---|---|---|---|
| batches | 20 414 | 137 564 | 51 386 |
| frames drawn in one batch | 981 (95.1 %) | 2 783 (69.6 %) | 752 (90.6 %) |
| ... if writes outside the read set did not split | **1 032 (100 %)** | **3 211 (80.3 %)** | **830 (100 %)** |
| frames with 17+ batches (loading) | 49 | 1 000 | 76 |
| lines drawn in batches of 512+ lines | 94.7 % | 80.7 % | 89.2 % |
| lines drawn in batches of 1-7 lines | 2.6 % | 4.8 % | 8.2 % |
| `RAM_G` batches whose write hit the read set | 0 of 19 405 | 12 371 of 133 771 | 0 of 50 616 |
| `DLSWAP_LINE` requests | **0** | **0** | **0** |
| `DLSWAP_FRAME` requests | 616 | 1 404 | 2 592 |
| drawing register writes during play | 0 (only the mode set-up) | 0 | 0 |
| handle table changed by a batch's later line | 0 | 0 | 0 |

Batches by trigger (rtype-play, first 4000 frames):

| Trigger | Batches | Lines | Of which 1-line batches |
|---|---|---|---|
| `RAM_G` written by the host | 39 116 | 257 832 | 30 967 |
| `RAM_G` written by the coprocessor (inflate while loading) | 94 655 | 351 226 | 82 315 |
| drawing register | 1 | 2 | 0 |
| frame end | 3 792 | 2 450 540 | 14 |

Line cost (instrumented, one core). These figures are taken under a load average of 67-130
(other agents' builds on the shared machine), so single lines are often preempted; the
shape of the distribution is what matters:

| | rtype-boot | rtype-play | zuma-flick |
|---|---|---|---|
| lines below 1 us / 2-16 us / above 16 us | 39 % / 49 % / 3.5 % | 12 % / 83 % / 1.5 % | 55 % / 0 % / 45 % |
| imbalance of 8 contiguous chunks (slowest / ideal) | 2.13 | 1.93 | 1.65 |
| imbalance of 8 round-robin chunks | 1.40 | 1.40 | 1.33 |

(An earlier census run at load 82-104 gave 1.19 / 1.09 / 1.12 for 8 round-robin chunks:
most of the imbalance above is preemption, not the lines themselves.)

## Analysis

- **No capture swaps the display list per line.** Every swap is a frame swap. TS-Labs'
  case (a list rewritten during the raster, e.g. a planned TS-Conf demo) does not occur
  in these programs; when it does, each line swap is a batch boundary (03 and 04c handle
  it; the lines between two swaps are still drawn in parallel).
- **Drawing register writes never happen during play** (only at mode set-up).
- **The splits come from `RAM_G` writes**: the host updating graphics during the frame,
  and the coprocessor inflating the next level's data while a loading screen is shown.
  Almost none of these writes touch memory that the current display list reads: on the
  boot and Zuma captures none at all, on the play capture 9 % of the splitting writes do
  (the game updates tiles that are on screen). With the read-set test, every boot and
  Zuma frame becomes a single batch, and 80 % of the play frames.
- Lines are similar enough in cost for round-robin (interleaved) scheduling; contiguous
  chunks are worse because the expensive lines cluster (sprites, the status bar).
- The bitmap handle table is a fixed point after a batch's first line, always: a parallel
  renderer can draw the first line serially and the rest from that table (03).

## Conclusions

- Lazy batching already gives whole-frame batches for 70-95 % of frames; deferring
  `RAM_G` writes that miss the read set raises this to 80-100 %. The serial part of the
  work (lines in batches too small to split) is 2.6-8 % of lines without the read-set
  test and well under 1 % with it (03 measures it: 0.13-0.95 %).
- TS-Labs' first hypothesis holds as stated (parallel only across lines, a line swap is a
  boundary), but its practical cost is nil on these captures: swaps per line do not occur,
  and the real boundaries (`RAM_G` writes) can mostly be skipped.
- Windows / Linux: nothing here is platform-dependent; the census is a property of the
  programs and of eve-emu's lazy drawing.
