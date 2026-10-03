# eve-emu (vendored)

The FT812 emulator library for the TS-Conf VDAC2 card, developed in its own repository
(eve-emu, `main`) and copied here so every unreal-ng checkout builds the card
(`ENABLE_VDAC2`, ON by default). Design: `docs/inprogress/2026-10-01-tsconf-vdac2/`
(`eve-emu-architecture.md`).

| | |
|---|---|
| Source | eve-emu `main`, commit `bc5196a` (2026-10-03: ROM layouts on the fast path, adding blends and modulation in SIMD; shapes reach only nearby lines; lines kept by their own steps; unchanged lines kept; display list walked once per frame, tag buffer only where read, blend shortcuts; rotated NEAREST fast path, palettes kept across lines; BILINEAR fast path, SIMD masking blends, skipped empty glyphs; frame metrics block, state version 8) |
| Copied | `CMakeLists.txt`, `LICENSE`, `README.md`, `cmake/CheckSymbols.cmake`, `include/`, `src/`, `docs/performance.md`, `vendor/README.md`, `vendor/config/` |
| Vendored decoders | only the files the library compiles: `vendor/miniz/` (`miniz.h`, `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, `miniz_tinfl.c`, `miniz_zip.h`, `LICENSE`; miniz 3.1.2) and `vendor/stb/` (`stb_image.h`, `LICENSE`; stb `2c980bb`) - in eve-emu they are git submodules |
| Not copied | the library's tests, benchmarks, tools (`eve-replay`), golden cases and oracle harness: they live in eve-emu |
| Local changes | none: change the library in eve-emu, then copy again |

Update: copy the same files from an eve-emu checkout (with its submodules initialized),
replace the commit above, build with `ENABLE_VDAC2=ON` and run the VDAC2 tests
(`core-tests --gtest_filter='Vdac2Card_Test.*'`). unreal-ng's own CMake sets
`EVE_BUILD_TESTS`, `EVE_BUILD_BENCHMARKS` and `EVE_BUILD_TOOLS` off.
