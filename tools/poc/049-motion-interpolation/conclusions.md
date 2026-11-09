# 046 — conclusions

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
3. **Games, by their own cadence (stage 2).** Most draw every 2nd-5th frame, so consecutive frames give nothing.
   Between the game's own pictures the search works: Darkwing Duck's scroll 78 -> 22.5 (81 % exact) interpolated,
   78 -> 19.5 (85 %) carried on; Thundercats and Garfield about a third less error. Without added latency
   (owner): the period is measured (half a second), and
   - `e-cadence` interpolates to the next picture once it is in the lookahead (the audio delay holds about two
     frames): the full step for 25 Hz games, the last two frames of the period for slower ones;
   - `f-carry` carries the last motion on through the period and the real picture replaces it; uniform motion, a
     small correction where the motion changes; artefacts where content enters the window.
   Animation cycles in place (Dizzy, Green Beret) gain nothing either way.
4. **The fallback holds visually:** where nothing matches (full-screen effects, colour cycling) the output is the
   blend; raster tears of the source stay as they are.

Proposal (owner decisions 2026-10-26 applied):

- `interpolate` becomes the third `SmoothMotion` mode after `blend` (design section 4.4), off by default.
- Games' cadence modes come from the game profile's `video:` section (period, mode, play window, excluded regions,
  optionally the "picture done" probe), not from run-time guessing (design section 4.5, profile format
  `mapping-and-profiles.md` section 7.7). `capture.py` and `cadence-eval` become the drafting tool (`tools/smoothmotion/capture.py`, `draft-video-profile.py`).
- Every effect runs on the GPU and on the CPU, on every platform: an integer, bit-exact CPU reference in `core/`
  and GPU passes per presenter tested against it (design section 6.5).
- Owner review of the 60 Hz videos picks the default game mode (`cadence` or `carry`).
