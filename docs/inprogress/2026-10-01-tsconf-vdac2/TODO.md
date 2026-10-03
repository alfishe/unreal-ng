# TODO — VDAC2 (FT812) for TS-Conf

Status 2026-10-01: research done, design written, nothing implemented.

- Design: [vdac2-tdd.md](vdac2-tdd.md).
- Games for testing (untracked): `testdata/machines/tsconf/vdac2/` (R-Type 1.01, Zuma 1.1,
  HMM2 v022).

## Remaining

- [ ] D-A: FT812 behavior specification - draft [ft812-behavior-spec.md](ft812-behavior-spec.md) (2026-10-01), awaiting review; open items V1-V18 in its §12
- [ ] D-B: library architecture and implementation brief - draft
  [eve-emu-architecture.md](eve-emu-architecture.md) (2026-10-01): separate static library,
  decoder options BUILTIN / EXTERNAL, standard decoder interfaces, stable snapshot +
  in-flight operation records; A1-A3 decided (own repository + submodule `lib/eve-emu`, `stb_image`, BUILTIN decoders)
- [ ] D-C: integration design - draft [vdac2-integration-design.md](vdac2-integration-design.md)
  (2026-10-01): submodule `lib/eve-emu` + `ENABLE_VDAC2`, SPI hub, `Vdac2Card`, INT line
  source, FT812-rate presentation, exact LUT, TTD id 26 + regions; open C1-C2
- [ ] D-D: test corpus and oracles - draft [vdac2-test-corpus.md](vdac2-test-corpus.md)
  (2026-10-01): layers, TS-Labs SDK programs, BT8XX harness, replay format `.evr`,
  card test programs; open O1-O3
- [x] TTD for the FT812 (design §9): `Vdac2Memory` (regions, zero runs dropped) + `Vdac2` blobs, the picture after a seek by frame / T-state, tests `ttdvdac2_test.cpp`
- [x] TTD history limit (frames / bytes, every automation surface, Qt TTD panel "Keep N GB"): long VDAC2 sessions (~0.6 MB per frame with content) stay in memory; a file saved after a release replays its remaining frames
- [x] Line budget metrics L1: metrics block in the chip state (eve-emu `db828d0`, state version 8); design [line-budget-metrics.md](line-budget-metrics.md), the model explained in [line-budget-model.md](line-budget-model.md)
- [x] Line budget metrics L2: Vdac2Control metrics API (+ the frame in flight, eve-emu `125876d` `EveFrameLinesPassed`), `[VDAC2] LineBudgetMargin`, measure-always, the five automation surfaces, OpenAPI, recipe
- [x] Line budget metrics L3: Debug -> FT812 Debug window (TSL-VDAC2 only, bars in the main screen's scale, the metrics kept with the presented picture)
- [ ] Line budget metrics L4: calibration on a card (line overhead, fill rates, the look of an overflow)
- [ ] When TTD v2 memory regions exist: `Vdac2Memory` as changed pages only
- [x] I1 parts that need no library, on master 2026-10-01: VDAC2 LUT `b115af790`, IDE off
  `13263c804`, ROM extraction tool `e6d50bc9d`, SPI hub `d3fecc61a`
- [ ] Library L0-L3b: in progress by a separate agent (own repository)
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
- [ ] A/B benchmark of the TS-Conf per-step path (`BM_HostFrame_TSConf_*`, I1 vs I2) on a quiet machine
- [x] Performance: the library optimized on the replay stream (R-Type 0.61x -> 4.5x real time on one core)
- [x] Acceleration experiments (CPU line threads, native GPU, three profiles): results in
  [acceleration-experiments.md](acceleration-experiments.md), experiments in `tools/poc/021-eve-accel/`
- [x] BILINEAR fast path + SIMD masking blends + skipped empty glyphs (eve-emu `61a3f19`): Zuma 0.48x -> 2.74x real time ([acceleration-experiments.md](acceleration-experiments.md) §3.6)
- [x] Rotated NEAREST fast path + palettes kept across lines (eve-emu `5f47ded`): Zuma 2.66x -> 3.8x; every round: [optimization-walkthrough.md](optimization-walkthrough.md)
- [x] Display list walked once per frame (recorded walk), tag buffer only on REG_TAG's line, blend shortcuts for alpha 0 / 255: Zuma 3.9x -> 5.46x ([optimization-walkthrough.md](optimization-walkthrough.md) rounds 4-6)
- [ ] Next drawing round: span setup kept in the recorded walk, magnified decoding, R-Type profile
- [ ] Build the rest of the recommendation of acceleration-experiments.md into eve-emu: line threads, deferred
  graphics memory writes, skip unchanged frames; then the GPU backend per batch
- [ ] Integration I3-I5; the TO VERIFY list in design §12.1
