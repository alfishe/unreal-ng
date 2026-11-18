# eve-emu (vendored)

The FT812 emulator library for the TS-Conf VDAC2 card, developed in its own repository
(eve-emu, `main`) and copied here so every unreal-ng checkout builds the card
(`ENABLE_VDAC2`, ON by default). Design: `docs/inprogress/2026-10-01-tsconf-vdac2/`
(`eve-emu-architecture.md`).

| | |
|---|---|
| Source | eve-emu `main`, commit `262b3f7` (2026-10-03: `if constexpr` for compile-time conditions (MSVC `/W4 /WX`); PALETTED8 on the fast path, filled shapes as spans; ROM layouts on the fast path, adding blends and modulation in SIMD; shapes reach only nearby lines; lines kept by their own steps; unchanged lines kept; display list walked once per frame, tag buffer only where read, blend shortcuts; rotated NEAREST fast path, palettes kept across lines; BILINEAR fast path, SIMD masking blends, skipped empty glyphs; frame metrics block, state version 8) |
| Copied | `CMakeLists.txt`, `LICENSE`, `README.md`, `cmake/CheckSymbols.cmake`, `include/`, `src/`, `docs/performance.md`, `vendor/README.md`, `vendor/config/` |
| Vendored decoders | only the files the library compiles: `vendor/miniz/` (`miniz.h`, `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, `miniz_tinfl.c`, `miniz_zip.h`, `LICENSE`; miniz 3.1.2) and `vendor/stb/` (`stb_image.h`, `LICENSE`; stb `2c980bb`) - in eve-emu they are git submodules |
| Not copied | the library's tests, benchmarks, tools (`eve-replay`), golden cases and oracle harness: they live in eve-emu |
| Local changes | the VDAC4 profile (`EVE_MODEL_VDAC4`, design `docs/inprogress/2026-11-15-tsconf-vdac4/design.md` §5.2), not yet in eve-emu: `include/eve/eve.h` (model, `EveSetRamGLimit`, `EveRamGLimit`, `EveVdac4SetFontsReady`, `EveVdac4FontsReady`), `src/eve-internal.h` (`Vdac4State` saved after `ControlState`, derived memory map fields, region `VDAC4_3D`), `src/eve-registers.cpp` (chip table row), `src/eve-memory.cpp` (per-chip RAM_G, 2/3 MB limit, ROM read-only and blank before the font copy, INT_FLAGS swap after read), `src/eve-chip.cpp` (power-up ACTIVE x8, core reset keeps RAM_DL/RAM_CMD, 128-clock sweep), `src/eve-spi.cpp`, `src/eve-timing.cpp`, `src/eve-state.cpp`, `src/eve-dl.cpp`, `src/eve-bitmap.cpp` (PALETTED8 ARGB8888), `src/eve-copro.h`, `src/eve-copro.cpp` (BL616 command table, image fault report), `src/eve-copro-text.cpp` (font wait, CMD_PROGRESS, BL616 text/number rules), `src/eve-copro-media.cpp` (CMD_LOADIMAGE_BOX, CMD_IMAGEINFO), new `src/eve-vdac4-3d.cpp` (3D commands A0...AF), `CMakeLists.txt`. FT812 behaviour is unchanged (eve-emu suite with golden cases: 186/186 before and after). Port these to eve-emu before the next copy |

Update: copy the same files from an eve-emu checkout (with its submodules initialized),
replace the commit above, build with `ENABLE_VDAC2=ON` and run the VDAC2 tests
(`core-tests --gtest_filter='Vdac2Card_Test.*'`). unreal-ng's own CMake sets
`EVE_BUILD_TESTS`, `EVE_BUILD_BENCHMARKS` and `EVE_BUILD_TOOLS` off.
