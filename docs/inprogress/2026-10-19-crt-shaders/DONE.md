# DONE — CRT shaders: the libretro set in unreal-qt (2026-10-19)

**Status:** complete — landed on master 2026-10-19 / 2026-10-20 (last: `d2ca4aa63`). Design: [README.md](README.md).
The feature: [crt-effects.md](../../features/crt-effects.md).

| Phase | Work | Status |
|:--|:--|:--|
| 1 | The broken profiles removed (Aperture, Shadow Mask, Slot Mask, "Megatron"): code, automation, tests, docs | done |
| 2 | GL 3.3 core context (4.1 on macOS) with the compatibility prelude for GLSL 1.20 custom shaders; the parity check still exact | done |
| 3 | `tools/crt-shaders/import.py` + `SlangPresetRunner`: the 19 presets GPU only, each against its librashader render (mean difference at most 0.08 / 255 on macOS) | done |
| 4 | `CRTBeamModel`: integer models of the seven single-pass shaders, CPU = GPU bit for bit, 0.06-0.28 / 255 from librashader | done |
| 5 | View > CRT Profile, `crt_profile` on WebAPI / CLI / Python / Lua / MCP (the words come from the window), docs, the gallery and comparison tools in `tools/crt-shaders/` | done |

After phase 4 (2026-10-20): the models' CPU path twice as fast (SIMD on NEON / SSE2 / plain C++, the filtered rows of
the pixel above reused, the glow's columns as a per-frame table: 27-80 ms of CPU time per 1408x1152 frame), and the
tables built the same on every compiler (`unreal-qt/src/widgets/crtmath.h`, `-ffp-contract=off`).

Verification at the close (2026-10-20):

- `TheSameBitsOnEveryCompiler` (seven models, None, Basic) and the rest of the CRT tests pass with clang (macOS arm64,
  Linux 18 arm64 / x86_64), gcc 13 (Linux arm64 / x86_64), MSVC 2019 and MinGW (Windows); gcc 16.2 at `-O3 -Wall
  -Wextra -Wshadow -Werror` compiles the CRT sources, their tests, `slangpresetrunner` and `crt-render` clean.
- The live window (703x576, a paused Pentagon): `source=window` grabs with `gpu` on and off are the same image for
  Basic and all seven models.
- `CRTBeamModel_Test.AgainstLibrashader` with the gallery reference: the means unchanged (0.06-0.28 / 255).

Not done: no software twin for the 12 multi-pass presets (GPU only by the owner's decision).
