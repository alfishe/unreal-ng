# bomzh (vendored)

The C model of the "bomzh-3D" GPU: the Quake II FPGA core the VDAC4 board's Tang runs (personality 0x51 'Q',
`Q2.BIN`). The emulated Tang executes the core's command stream with it (`Vdac4BomzhCore`,
`core/src/emulator/platforms/tsconf/vdac4/vdac4bomzh.h`); the model is the reference the core's RTL is checked
against, pixel for pixel.

| | |
|---|---|
| Source | Quake-II-for-ZX-Evolution-VDAC4, `gpu/model/bomzh.c` and `gpu/model/bomzh.h`, commit `c5b4143` (2026-11-25) |
| License | MIT, Copyright (c) 2026 Andrew Lazarev (`LICENSE`, the repository's root license; `gpu/` carries no other) |
| Copied | `bomzh.h` as is, `bomzh.c` as `bomzh.cpp` (the model builds as C++: the core library globs `*.cpp`) |
| Local changes | `bz_shown_rgb` reads the shown frame through `rd8` (the address wraps in SDRAM): the original indexed `mem` directly and could read past its end for a frame placed near the top of SDRAM. `poly` returns at once for `n <= 0` (gcc 13 -O3 -Wmaybe-uninitialized: `vy[top]` was read with no vertex written) |
| Build | `core/src/CMakeLists.txt`: no precompiled header (`SKIP_PRECOMPILE_HEADERS`) |

Update: copy both files again (`bomzh.c` to `bomzh.cpp`), reapply the local change, replace the commit above, build
and run `core-tests --gtest_filter='*Bomzh*'`.
