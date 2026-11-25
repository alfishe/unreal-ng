# 055 — proposal

Adopt `c-lite` as a third interpolation of smooth motion, for machines the reference is too slow on. Not a
replacement: the reference stays what a machine that can afford it draws.

1. **Where it goes.** `core/src/emulator/video/`: the plane packing, the kept field and the mask composition beside
   `smoothlayers.cpp`, behind the `Device`-less CPU path; re-implemented with its own tests and A/B benchmarks
   (POC code is not linked in). The screen hands over the colours' indices it already has, so no packing from RGBA.
2. **When it draws.** The window's guard (stabilization, section 1.6) today blends when the interpolation's pictures
   come a refresh late. With `c-lite` it steps down in two: the reference, then `c-lite`, then the blend. A setting
   to pick it outright (`smooth_motion = interpolate-lite`, the name to settle) for a machine known to be slow.
3. **Before adopting, measure**:
   - on a real low-end machine (a Core 2 class or an Atom): the budget was set from a model. The quiet Steam Deck
     thread is measured: `lite14` 0.45-0.90 ms a refresh;
   - the live window on real games through `tools/verification/smoothmotion/live` (the suite is synthetic);
4. **The form to adopt is `lite19`** (2026-11-23): `lite6` + credible + slowest of equals + tied regions, with half
   the search budget (144 row scores a pair: the suite's sum 58.7 against 57.3, inside its noise, 11-15 % less time). Done of
   the quality steps: colour changing in place (`flux` 16.1 to 3.7), periodic patterns (`diagonal` 4.3 to 0.6,
   `vertical` 6.7 to 2.6), thin lines (`thin` 8.4 to 1.8). Open, each to be scored before it is kept:
   - dots a pitch apart moving by the pitch (`dotmatrix` -8, `animated` dots -8: 23-28 % wrong);
   - motions that stop and start (`cadence` 6.6 against 3.3): the kept field finds them late;
   - the kept vectors' scores (10 a row, every row of a game): the field's remaining cost. Sleeping rows and local
     scoring were tried and cut nothing; fewer kept vectors lose the scenes of several layers.
5. **Not proposed**: making the reference exact and cheap. Its windows and decisions cost 12 ms a thread on the
   worst content whatever the pixel tests cost; the 2026-11-22 pass (`9a6bb6834`) took what was to be had without
   changing its form.
