# 055 — analysis

Numbers: [measurements-2026-11-22-cost-and-quality.md](measurements-2026-11-22-cost-and-quality.md).

## Against the expectations

| Expected | Found |
|----------|-------|
| The field on bit planes 10x or more faster | Wrong. The same field, and no faster (0.8-3x slower on easy clips, 1.2x faster on the worst). The reference scores a vector for one block and stops at the first mismatch past the best so far; a row of blocks scored at once has no early stop, so 110-280 of the 289 vectors are scored for the rows that hold one unsettled block. The compare is not what costs: the full search is |
| The per-vector pixel tests 10x or more | Right, and more: the whole composition by masks is 0.5 ms on the host where the reference's picture is 57 ms, 45 of them `Compose` |
| Window counts and block decisions bound an exact variant near 12 ms | Right: they are 12 ms of the worst picture and 4 ms of the simplest. No exact variant fits |
| So `go, exact` is not expected | Confirmed |

## Where the time went, and what replaced it

1. **The search, not the compare.** A full search is 289 vectors for every block of every pair. Motion lasts: the
   last pair's vectors explain this one. `LiteField` scores zero and the ten vectors kept, and only rows with an
   unexplained block go on through the search order, 288 row scores a pair, each row from where it stopped: 19
   pictures' worth of scores on the worst trace in place of 278. The field: 13.8 ms -> 0.85 ms.
2. **Masks in place of windows.** The reference decides a pixel by counts over windows of 11 and 33 pixels for each
   of up to 32 vectors. `c-lite` decides it by four masks of whole words per vector: the samples agree; the vector
   carries the pixel's own colour; the vector is the block's own, or one of the blocks round it. 0.5 ms.
3. **What the reference's rules buy.** `lite6` keeps two of them in mask form: the own-colour fit (a still pixel
   over a moving layer stays: `overlay` 12.05 -> 0.89, under the reference's 1.04) and the carry. It has none of the
   window rules, and the scenes say which matter:
   - `flux` (16.1 against 3.0): a colour cycling in place fits a moving vector by chance; the reference's `credible`
     (support beyond the unexplained changes round the pixel) is a window count.
   - `diagonal`, `vertical` (dither layers 11-17 %): look-alike vectors of a periodic pattern; the reference's tie
     and alias rules, with regions of tied blocks.
   - `thin`, `cadence` (8.4 and 6.2 against 2.9 and 3.5 with the exact field): the price of the kept field, a new
     or stopping motion found a few pairs late.
   - `animated` (21.8 against 14.6): shapes changing on the move.

## The 2026-11-23 rules

[Measurements](measurements-2026-11-23-quality-rules.md). What each showed:

- **credible** (`flux` 16.1 to 3.1): colour changing in place fits a moving vector by chance; a vector that
  explains no change near a block has no claim on its pixels. Block counts (bytes of a word) in a 3 x 3 window stand
  in for the reference's pixel window at no loss on this scene.
- **slowest of equals** (`diagonal` 4.3 to 0.3, `vertical` 6.7 to 3.7, `thin` 7.4 to 1.7): the kept field took the
  first vector that explained a row, and a periodic pattern fits every vector of its period. The exact field had
  the same fault (`lite`, 4.0 on `diagonal`), so the fault was the tie rule, not the kept field.
- **regions** (`dotmatrix` 12.5 to 7.4): the slowest rule alone freezes the middle of a row of dots (zero ties with
  the row's motion); the row's ends decide. One-colour blocks must stay out, or paper votes.
- A larger search budget does not buy quality: the late finds of `cadence` are a matter of which pairs are still,
  not of how many vectors are tried.

## Reading the scores

- `lite6` is 3.1 times better than `off` over the suite (91 against 279) and 2.1 times worse than the reference
  (44). It beats `off` on eleven scenes of twelve; on `diagonal` it is worse (4.3 against 0.9: that scene's slow
  terrain is nearly right when repeated, and its dither layer is where the look-alikes cost).
- By the suite's measure the blend is the worst of all (797): every pixel of a moving edge is neither frame's. It is
  the measure of sharpness, not of how the eye takes a soft picture; the charter's criterion against the blend is
  met by a wide margin and says little. The comparison that tells is against `off` and the reference.
- The scores are of synthetic scenes. The reference was tuned on them (POC 047); `c-lite` was not, beyond the two
  rules above, both taken from the reference.

## Limits of this measurement

- `c-lite`'s cost on 2026-11-22 was measured on a shared host under load, as a ratio to the reference in the same
  run; the quiet Deck thread was measured on 2026-11-23 (`lite6` 0.90-1.00 ms where 0.85 ms was derived).
- Packing a frame into planes (0.16-0.37 ms on the Deck after the 2026-11-23 cost pass) is on top; the emulator's screen has the colours' indices
  before it makes RGBA, so a real implementation need not derive them.
- More than 256 colours: no planes. The hi-colour modes are out of this variant.
- One thread, x86-64 only so far; plain 64-bit words, no intrinsics, so ARM64 is expected the same way.
