# TODO — VDAC2 (FT812) for TS-Conf

Status 2026-10-03: implemented and on master. The `eve-emu` library (own repository, vendored
at `core/src/3rdparty/eve-emu`), the machine variant `TSL-VDAC2`, the FT812 interrupt and
picture, TTD, the line budget metrics with the FT812 Debug window, the bus capture, every
automation surface. Open: what needs a card or TTD v2, two review questions, the drawing
optimization (paused). PLAN #87 (remainder), related to #41 and #40.

History (2026-10-01): research done, design written, nothing implemented.

- Design: [vdac2-tdd.md](vdac2-tdd.md).
- Games for testing (untracked): `testdata/machines/tsconf/vdac2/` (R-Type 1.01, Zuma 1.1,
  HMM2 v022).

## Remaining

- [x] D-A: FT812 behavior specification [ft812-behavior-spec.md](ft812-behavior-spec.md): the reference `eve-emu` implements (its items still to verify: open line below)
- [x] D-B: library architecture [eve-emu-architecture.md](eve-emu-architecture.md): built as
  `eve-emu` (own repository; unreal-ng carries a vendored copy instead of the planned submodule)
- [x] D-C: integration design [vdac2-integration-design.md](vdac2-integration-design.md): I1-I4
  on master (TTD as the blobs `Vdac2` / `Vdac2Memory` until TTD v2 regions exist), I5 below;
  its review questions C1-C2: open line below
- [x] D-D: test corpus [vdac2-test-corpus.md](vdac2-test-corpus.md): the TS-Labs SDK programs
  in `testdata/machines/tsconf/vdac2-sdk/` (O2), the BT8XX golden cases (113) and the `.evr`
  replays in `eve-emu`, the patched capture build kept local (O3)
- [x] TTD for the FT812 (design §9): `Vdac2Memory` (regions, zero runs dropped) + `Vdac2` blobs, the picture after a seek by frame / T-state, tests `ttdvdac2_test.cpp`
- [x] TTD history limit (frames / bytes, every automation surface, Qt TTD panel "Keep N GB"): long VDAC2 sessions (~0.6 MB per frame with content) stay in memory; a file saved after a release replays its remaining frames
- [x] Line budget metrics L1: metrics block in the chip state (eve-emu `db828d0`, state version 8); design [line-budget-metrics.md](line-budget-metrics.md), the model explained in [line-budget-model.md](line-budget-model.md)
- [x] Line budget metrics L2: Vdac2Control metrics API (+ the frame in flight, eve-emu `125876d` `EveFrameLinesPassed`), `[VDAC2] LineBudgetMargin`, measure-always, the five automation surfaces, OpenAPI, recipe
- [x] Line budget metrics L3: Debug -> FT812 Debug window (TSL-VDAC2 only, bars in the main screen's scale, the metrics kept with the presented picture)
- [ ] Line budget metrics L4: calibration on a card (line overhead, fill rates, the look of an overflow)
- [ ] When TTD v2 memory regions exist: `Vdac2Memory` as changed pages only
- [x] I1 parts that need no library, on master 2026-10-01: VDAC2 LUT `b115af790`, IDE off
  `13263c804`, ROM extraction tool `e6d50bc9d`, SPI hub `d3fecc61a`
- [x] Library L0-L3b: `eve-emu` (own repository, local), vendored into unreal-ng
- [x] I1 rest, branch `vdac2-i1`: CMake `ENABLE_VDAC2` (OFF until the submodule exists) +
  `EVE_EMU_DIR`, `[VDAC2] RomImage`, refusal without library support, `Vdac2Card` (hub slot 1,
  raster tacts -> FT812 clocks with exact remainder, frame-end call from the engine), tests
  `vdac2card_test.cpp`
- [x] The library vendored at `core/src/3rdparty/eve-emu` (eve-emu `d7d28e2`, R-Type 6-7x faster), `ENABLE_VDAC2` ON
- [x] Machine variant `TSL-VDAC2` (Machine menu: TS-Conf + VDAC2 (FT812)), created by name on every surface
- [x] I2 (branch `vdac2-i1`): FT812 INT_N as the line interrupt on msel lines, the FT812 picture
  on the monitor through the Screen's external picture source, latched at the FT812 rate
- [x] FT812 bus capture (.evr replay stream, test corpus §4) on every automation surface; R-Type
  gameplay capture taken (local, 144 MB) for the library's replay tests and optimization
- [x] I5: what the VDAC2 integration costs a TS-Conf without the card (2026-10-03). Measured on
  master `ce770941e` built with and without `ENABLE_VDAC2` (instead of I1 vs I2: other TS-Conf
  changes landed in between), `BM_HostFrame_TSConf_Fast` / `_Debug`, 5 repetitions, medians,
  the builds in turns: 1906 / 1959 us with VDAC2 against 2000 / 1977 us without (load 10), 1947
  / 1999 against 1934 / 1991 (load 20-24). The difference is inside the noise: the Pentagon
  control, which VDAC2 does not touch, moved by up to 4 % between the same runs (1552 - 1626
  us). No measurable cost.
- [x] Performance: the library optimized on the replay stream (R-Type 0.61x -> 4.5x real time on one core)
- [x] Acceleration experiments (CPU line threads, native GPU, three profiles): results in
  [acceleration-experiments.md](acceleration-experiments.md), experiments in `tools/poc/021-eve-accel/`
- [x] BILINEAR fast path + SIMD masking blends + skipped empty glyphs (eve-emu `61a3f19`): Zuma 0.48x -> 2.74x real time ([acceleration-experiments.md](acceleration-experiments.md) §3.6)
- [x] Rotated NEAREST fast path + palettes kept across lines (eve-emu `5f47ded`): Zuma 2.66x -> 3.8x; every round: [optimization-walkthrough.md](optimization-walkthrough.md)
- [x] Display list walked once per frame (recorded walk), tag buffer only on REG_TAG's line, blend shortcuts for alpha 0 / 255: Zuma 3.9x -> 5.46x ([optimization-walkthrough.md](optimization-walkthrough.md) rounds 4-6)
- [x] Unchanged lines kept in the frame buffer (inputs: graphics memory, drawing registers, display list contents, output, recorded walk): Zuma 5.75x -> 13.3x, R-Type boot 6.9x -> 12.6x (walkthrough round 7)
- [x] Lines kept by their own steps (reaching steps equal, RAM_G read by them unchanged per 256-byte block): Zuma 12.9x -> 20.7x, R-Type boot 12.5x -> 24x (walkthrough round 8)
- [x] Rectangles / points / lines reach only nearby lines (walkthrough round 9); live profile of unreal-qt with R-Type: FT812 ~1/3 of the emulation thread
- [x] TS-Labs SDK programs profiled (walkthrough round 10): ROM layouts on the fast path, ONE / ONE and DST_ALPHA / ONE blends and COLOR_RGB modulation in SIMD; test9 0.83x -> 3.75x, test6 1.59x -> 2.94x
- [x] ROM image and golden-case profiles (eve-tests-profile); PALETTED8 on the fast path, filled rectangles / edge strips as spans (walkthrough round 11)
- [ ] Drawing optimization paused until captures of other usage patterns exist; candidates in optimization-walkthrough.md "Next round"
- [ ] Build the rest of the recommendation of acceleration-experiments.md into eve-emu: line threads, deferred
  graphics memory writes, skip unchanged frames; then the GPU backend per batch
- [x] Integration I3 (bus capture and line budget metrics on the five automation surfaces,
  OpenAPI, recipe `.recipe/machines/tsconf-vdac2.md`, ROM extraction tool) and I4 (TTD blobs,
  restore tests `ttdvdac2_test.cpp`)
- [ ] The spec's items to verify (ft812-behavior-spec.md §12, V1-V19) on BT8XX or a card;
  `eve-emu`'s tunables mark the open ones `TO VERIFY`
- [ ] Review questions C1 (recording across an msel switch) and C2 (the native picture under
  the FT812 for the debugger), integration design §15
