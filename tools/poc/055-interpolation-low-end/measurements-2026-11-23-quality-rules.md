# 055 — measurements 2026-11-23: quality rules on `c-lite`

Each rule is a filter of `cpp/lite.cpp` on top of the one before, scored on the suite of POC 047 (12 scenes, mean
share of wrong pixels over a scene's elements, %). Reference figures: `off` 278.79, `layers` 43.78, `lite6` 91.23.

## Rules

| Filter | Rule added | Where |
|--------|------------|-------|
| `lite7` | **credible**: a moving vector takes pixels of a block only where, in the 3 x 3 blocks round it, the changed pixels it carries both ways are some and no fewer than the changes no vector explains | `Lite::Credible` |
| `lite9` | **slowest of equals**: of the vectors that fit a block equally the slowest, of equally slow ones the first in the search order; every kept vector is scored on every row that moves | `LiteField::slowestOfEquals` |
| `lite11` | **verify slower**: a row explained by a kept vector still tries the shorter vectors of the search order | `LiteField::verifySlower` |
| `lite14` | **regions**: 4-connected blocks tied between the same vectors take the vote of the decided blocks on their border; one-colour blocks neither join nor vote | `LiteField::Regions` |

`lite8`, `lite10`, `lite12`: `lite7`, `lite9`, `lite11` with three times the search budget (864 row scores a pair).
`lite13`: `lite14` + verify slower.

## Scores

| Scene | layers | lite6 | lite7 | lite9 | lite14 |
|-------|--------|-------|-------|-------|--------|
| strips | 1.07 | 2.63 | 2.62 | 2.35 | 2.01 |
| vertical | 2.04 | 6.66 | 6.59 | 3.71 | 2.61 |
| diagonal | 0.48 | 4.33 | 4.32 | 0.33 | 0.60 |
| overlay | 1.04 | 0.89 | 0.88 | 0.88 | 1.34 |
| textlines | 0.58 | 1.24 | 1.05 | 1.20 | 0.89 |
| parallax | 4.08 | 5.06 | 5.19 | 5.56 | 5.07 |
| sprites | 7.36 | 8.84 | 8.04 | 8.12 | 8.04 |
| cadence | 3.33 | 6.20 | 5.68 | 6.69 | 6.60 |
| dotmatrix | 4.77 | 9.06 | 8.92 | 12.46 | 7.39 |
| thin | 1.43 | 8.41 | 7.38 | 1.74 | 1.77 |
| flux | 3.02 | 16.09 | 3.10 | 3.03 | 3.65 |
| animated | 14.58 | 21.82 | 19.06 | 20.65 | 17.35 |
| **sum of 12** | **43.78** | **91.23** | **72.83** | **66.72** | **57.32** |

Columns are of the last run of each filter (`r19` for `lite7` and `lite14`, `r15` for `lite9`). The same filter
scored twice differs by up to 2 in the sum (`lite7` 74.84 and 72.83, `lite14` 59.05 and 57.32): the last change
between those runs cut the credible pass to the rows where a vector is a candidate, which also changes what counts
as explained. A difference under 2 in the sum is not a finding.

Dropped:

| Form | Sum | Against | Why dropped |
|------|-----|---------|-------------|
| three times the budget (`lite8`) | 86.39 | 74.84 | `vertical` 19.5 against 6.6: more look-alikes found sooner, before the slowest rule |
| three times the budget (`lite10`, `lite12`) | 65.66, 65.01 | 66.03 | within the noise, for 30 % more time |
| verify slower (`lite11`) | 66.52 | 66.03 | no gain, 10 % more time |
| verify slower on regions (`lite13`) | 61.00 | 59.05 | `cadence` 8.1 against 6.6 |
| regions with one-colour blocks as members and voters | 63.61 | 59.05 | paper round a row of dots outvoted the row's ends (`cadence`, `overlay`) |

## Cost

One thread, `lite6` beside it in the same run:

| Host, clip | lite6 | lite7 | lite14 |
|------------|-------|-------|--------|
| .238 under load (load average 22-32), Across the Edge trace | 1.48-1.77 ms | 2.20-2.39 ms before the cut | 1.83-2.18 ms |
| Mac M-series, suite `parallax` | 0.77 ms | 1.01 ms | 1.41 ms |
| Mac, suite `dotmatrix` | 0.60 ms | 0.71 ms | 0.89 ms |
| Mac, suite `animated` | 0.59 ms | 0.75 ms | 0.80 ms |
| Mac, suite `sprites` | 0.63 ms | 0.75 ms | 0.71 ms |

`lite14` is 1.23 times `lite6` on the game trace and up to 1.8 times on `parallax` (three layers: every kept vector
scored on every moving row). The Mac figures of `lite7` and `lite14` are of before the cut of the credible pass.
The Deck: the next section.

## Where the rest is

`lite14` against the reference, the elements that carry the difference (wrong pixels, %):

| Scene, element | lite14 | Note |
|----------------|--------|------|
| dotmatrix, dots text -8 | 23.0 | dots a dot's pitch apart moving by the pitch: zero and the motion tie over whole words, the region has few decided neighbours |
| animated, anim dots -8 | 28.0 | the same, with the dots changing |
| animated, water -2 / spinning | 24.6 / 18.0 | shapes changing on the move |
| cadence, text -8 every 2nd | 12.2 | a motion that stops every other pair: found late by the kept field |
| cadence, noise speeding up | 11.3 | a new vector every few pairs |
| overlay, tiles -4 | 6.3 | — |

## Steam Deck, one quiet thread

The CI image's g++ (`-O3`, no `-march`), the four game traces of the stabilization work (352 x 288, 50 frames/s to
60 refreshes/s), load average under 1. A refresh, ms:

| Trace | layers | lite6 | lite14, as scored above | lite14, after the cost pass below |
|-------|--------|-------|-------------------------|-----------------------------------|
| Across the Edge (the worst) | 33.0-36.3 | 0.90-1.00 | 1.02-1.13 | 0.89-0.90 |
| action | 22.3-22.6 | 0.64-0.66 | 0.80-0.81 | 0.72-0.79 |
| Aleste | 13.6 | 0.73 | 0.86 | 0.48-0.69 |
| scroll | 9.7-10.2 | 0.59-0.63 | 0.69-0.74 | 0.45-0.60 |

The budget of the charter is 1.7 ms. `lite14` on the worst trace is 1 / 37 of the reference. The 2026-11-22 figure
derived for `lite6` (0.85 ms) was measured at 0.90-1.00 ms. The Deck's own runs differ by up to a quarter between
invocations a few minutes apart (Aleste 0.48 and 0.69 from one binary); within one invocation by 1 %.

Where `lite14`'s time is after the cost pass (Across the Edge / scroll, ms a refresh): the field 0.51 / 0.22-0.29,
credible 0.19 / 0.09-0.12, the composition 0.20 / 0.14-0.18. Packing a frame into planes, on top: 0.16-0.37 ms
(0.38-0.55 before).

## The cost pass

Changes that keep every picture's bits (checked on the 12 scenes and two traces, `lite2`, `lite6` and `lite14`
outputs compared byte for byte), with what each bought on the Deck:

| Change | Effect |
|--------|--------|
| Composition: a vector's fit masks only in the words under its own or neighbouring blocks; a word of one colour filled, not unpacked | the composition 2.5 times faster on the suite scenes (Mac); on the game traces it was 0.2 ms and stayed |
| `own` / `near` masks by spreading bits of words (was 9 block reads per vector and block) | the field's time outside the row scores halved: Across the Edge 1.08 to 0.93 ms a refresh |
| No ranking of common vectors (`MotionField::Assign` sorts them with a search of the order in every comparison; `c-lite` never reads the result): the field goes out as a plain grid | action 0.78 to 0.72 ms, within the Deck's spread on the others |
| Vector counts by a table instead of a list search; 3 x 3 sums of the credible counts by rows then columns | not measurable |
| Packing eight pixels to a word, one multiply a plane | packing 0.38-0.55 to 0.16-0.37 ms a frame |
| A row score stops when no block can equal its best any more | 70 % of the row scores of Across the Edge stop early, the time is the same: they stop in the last rows |

Tried and dropped:

| Change | Why dropped |
|--------|-------------|
| Shifts decided once a row in the row score | the same time |
| `lite15`: a search's score and the credible counts only in the words with an open block | not the same pictures (sum 58.78 against 57.32) and no faster: on a game the whole row is open |
| A small table of recent colours before the palette's map | the same time |

## Fewer row scores

What a pair costs on a game (`LITE_STATS=1`), row scores:

| Trace | zero | kept vectors | search | Rows open after zero | Rows open at the end, their blocks |
|-------|------|--------------|--------|----------------------|------------------------------------|
| Across the Edge | 36 | 351 | 288 | 35.2 of 36 | 34.6, 896 of 1584 blocks |
| action | 36 | 233 | 288 | 23.4 | 17.6, 70 blocks |

"Open" is a block no vector explains exactly. On a game most rows keep one: every kept vector is scored on every
row and the search spends its whole budget, every pair. So two forms that were to cut the scores cut nothing:

| Form | Row scores a pair, Across the Edge | Suite sum | Verdict |
|------|-------------------------------------|-----------|---------|
| `lite14` | 674.9 | 57.32 | — |
| `lite16` / `lite18`: a row searched through the whole order sleeps 25 pairs | 674.9 / 672.2 | 59.68 (`lite18`) | dropped: 8-9 rows asleep, the budget goes to the others |
| `lite17` / `lite18`: a kept vector on an explained row only where it was used | 672.5 / 672.2 | 59.68 (`lite18`) | dropped: the rows are open, not explained |

The search budget is the knob that is left:

| Form | Suite sum | Across the Edge, Deck | action | Aleste | scroll |
|------|-----------|-----------------------|--------|--------|--------|
| `lite14`: 288 row scores of search a pair | 57.32 | 0.89-0.90 ms | 0.78 | 0.49-0.52 | 0.53-0.60 |
| `lite19`: 144 | 58.66 | 0.80 ms | 0.66 | 0.42-0.44 | 0.46-0.52 |
| `lite20`: 72 | 63.00 | 0.74 ms | 0.61 | 0.38-0.41 | 0.43-0.48 |

`lite19` is inside the suite's noise of `lite14` (under 2) for 11-15 % less time. `lite20` loses on `cadence` (9.5
against 6.6) and `vertical` (4.4 against 2.6): motions that start are found too late.

Fewer kept vectors (on `lite19`; the default keeps 10):

| Form | Suite sum | Worst scenes | Across the Edge, Deck | Differs from the reference there |
|------|-----------|--------------|-----------------------|----------------------------------|
| `lite19`: 10 kept | 58.66 | — | 0.80-0.81 ms | 1.164 % |
| `lite21`: 6 | 82.18 | `vertical` 20.5 (was 3.1), `cadence` 10.1 | 0.69 ms | 1.180 % |
| `lite22`: 4 | 100.74 | `vertical` 28.7, `strips` 9.3 | 0.63 ms | 1.190 % |
| `lite23`: 3 | 124.56 | `vertical` 36.2, `strips` 15.1 | 0.59 ms | 1.211 % |

Dropped. A game's trace hardly notices (its tail vectors win 2-9 blocks of 1584 each), but a picture of several
layers needs a vector a layer and the slower look-alikes beside them: with 6 kept the suite's `vertical` (four
layers) is lost. How many of the kept vectors win a block, by rank (pairs %, `LITE_STATS=1`): Across the Edge and
action 97-100 % for all of the first 11; suite `parallax` 100 % to rank 7, 37 % at rank 12; `sprites` under 50 % from
rank 6.

## ARM64

Apple M-series (clang, `-O3`), the Across the Edge trace, against x86-64 (gcc, .238): the pictures of `layers`,
`lite6`, `lite14` and `lite19` are the same bytes on both (md5 of each filter's 296 pictures). One thread, three
runs, the machine under other load (load average 10-36):

| Filter | A refresh |
|--------|-----------|
| `layers` | 20.4-20.7 ms |
| `lite6` | 0.46-0.49 ms |
| `lite14` | 0.49-0.50 ms |
| `lite19` | 0.43-0.44 ms |

Packing 0.09 ms a frame. `lite19` is 1 / 47 of the reference there.

## Against the reference on games

No ground truth on a game's trace, so the measure is the share of pixels that differ from the reference's picture
([agree.py](agree.py)), over all refreshes:

| Trace | off | blend | lite6 | lite14 | lite19 | lite20 |
|-------|-----|-------|-------|--------|--------|--------|
| Across the Edge (the reference changes 25.1 % of the pixels) | 25.12 % | 49.48 % | 2.26 % | 1.16 % | 1.16 % | 1.20 % |
| action (0.23 %) | 0.225 % | 0.440 % | 0.172 % | 0.023 % | 0.022 % | 0.023 % |

Of the pixels the reference changed, `lite14` draws 3.1 % (Across the Edge) and 6.7 % (action) differently; `lite6`
3.3 % and 30.7 %.

Raw outputs: `scratch/2026-11-22-poc-055/r7` .. `r23`, `ag-*`, not kept in the repository.
