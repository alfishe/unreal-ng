# 038 — conclusions

1. **The block search finds ZX motion.** On scrollers, moving pictures and demo motion `c-block` halves to sixths the
   blend's error (7th Reality 66 -> 10, Demarche 98 -> 52, Action 166 -> 106, Aleste 135 -> 94) and gets 52-90 % of
   the changed pixels exactly right, where the blend gets none.
2. **Against the criteria:**
   - MAE at most half of the blend's on the scroller and the game: met on Demarche and 7th Reality, not on Aleste
     (0.69) and Action (0.64); not applicable to the games, which repeat pictures (point 3).
   - Never worse than the blend by more than 5 %: met on 7 of 8; Thundercats is 9 % worse on 8 triples (an idle
     screen, a few sprite pixels), Darkwing Duck 3 %.
   - Under 2 ms per pair: met at p50 (1.3-2.3 ms, scalar, one thread, a loaded machine), not at p95 (4.4-6.2 ms;
     the load inflates it, a quiet run is pending). SIMD or the GPU (the search is per block, independent) bring it
     well under.
3. **Games are not helped, and should not be.** Most draw every 2nd-5th frame; the in-between of two repeated
   pictures is the same picture. Smoothing a game's own 10-25 Hz cadence would need frames from its future (latency)
   and change how it looks. The presenter interpolates only between consecutive emulated frames.
4. **The fallback holds visually:** where nothing matches (full-screen effects, colour cycling) the output is the
   blend; raster tears of the source stay as they are.

Proposal: adopt `interpolate` as the third `SmoothMotion` mode after `blend` (design section 4.4), behind the same
off-by-default setting; implement the search on the GPU (Metal compute / D3D11 compute / GL fragment passes) or with
SIMD on the presenter thread; owner review of the 60 Hz videos first.
