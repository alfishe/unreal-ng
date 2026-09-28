# GigaScreen De-flicker — Prior Art

**Created:** 2026-09-27
**Sources:** local emulator collection `/Volumes/TB4-4Tb/Projects/emulators/github`
(paths relative to it). Read-only survey.

## 1. Summary

| Emulator | Detection | Mixing | Color space | Motion handling |
|---|---|---|---|---|
| spectaculator-gigascreen-noflick (koval) | per pixel: static skip, A,B,A test, period-3 test for pure primaries | 2-frame blend, 3-frame blend | linear light (5/6-bit precision) | per-pixel A,B,A check only |
| Xpeccy / xpeccy-plus | port of the koval logic (4 modes) | 2- or 3-frame | linear light (8-bit tables) | same as koval |
| Unreal Speccy (NoFlic) | none — whole frame | 50/50 with previous frame | sRGB; 8-bit mode uses **hand-tuned mixed levels** close to linear light | none |
| Unreal Speccy (resampler) | none | windowed-sinc temporal filter over 2–8 frames, 12 Hz / 8 Hz cut-offs for 2- and 3-color | sRGB | none |
| UnrealSpeccyP | none | 50/50 GPU shader | sRGB | none |
| ZXMAK2 | none | 50/50 | sRGB | none |
| pico-spec | **Auto mode: a `#7FFD` screen-page flip enables blending for 3 frames** | pair lookup table on palette indices | sRGB | equal pixels pass through |
| Spectral | none | recursive "RF ghosting" | — | — (look effect, not de-flicker) |

No emulator does region-level, motion-aware de-flicker. No hardware
GigaScreen / 3-color display mode was found in any FPGA source in the
collection (zx-evo, TSConf, karabas-pro); Unreal's `#EFF7` "gigascreen" bit
only switches turbo.

## 2. The koval algorithm in detail

`spectaculator-gigascreen-noflick/src/gigascreen_main.cpp` — the most
advanced existing detector:

- Ring of 5 frames of RGB565 output.
- `p0 == p1 == p2` → static, unchanged.
- Mode 2: if none of p0..p2 has more than one non-zero RGB channel **and**
  `p0==p3 && p1==p4 && p2==p5` → 3-frame average; else 2-frame blend.
- `motion_check`: blend only if `p0 == p2 && p0 != p1` (A,B,A), else pass the
  current pixel through.
- Blend in linear light via per-channel LUTs; `ratio` 0.5–1.0 weights the
  current frame.
- README test material: **GBT269, Across the Edge, Kpacku, Paralactika**,
  three-color images "3c-mk" and "3c-cat".

Limitations: single-pixel decisions (speckle on sprite edges), 3-color test
only for pure primaries, decisions on output RGB (palette-dependent), manual
off switch needed for 50 fps scrollers.

## 3. Unreal's calibrated pair levels

Unreal's 8-bit NoFlic palette uses hand-tuned mixed brightness levels
(`draw.cpp:116-152`, `Unreal.ini:454-472`): `ZZ=00, ZN=80, NN=C0, NB=E0,
BB=FF, ZB=C8` (Z = zero, N = normal, B = bright). The arithmetic means would be
60/DF/80; the tuned values are close to a linear-light mix. This is historical
evidence, from people who compared against real CRTs, that sRGB averaging is
too dark.

## 4. Adopted into our design

| Idea | Source | Where it goes |
|---|---|---|
| Linear-light mixing with high precision (float or ≥ 12-bit) | koval, Xpeccy, Unreal tuned levels | [design-mixers.md](design-mixers.md) default |
| Unreal pair levels as a selectable mixer | Unreal | mixer `unreal-pairs` |
| Windowed-sinc temporal filter as a selectable mixer | Unreal resampler | mixer `sinc-window` |
| Koval-compatible mixer for side-by-side comparison | koval | mixer `koval-compat` |
| Screen-page flip as a hint, never as proof | pico-spec | analysis §5 page hint |
| A,B,A and static-skip as segment-level primitives | koval | analysis §6 (at segment level, not per pixel) |
| Detect on raw history, never on blended output | ZXMAK2, pico-spec, koval | analysis §5 |
| History advances once per emulated frame; reset on mode/palette/snapshot/seek | all (pitfalls) | analysis §5 |

## 5. Pitfalls to avoid

- Whole-frame blending smears every moving object (Unreal, USP, ZXMAK2).
- Quantizing linear values to 5–8 bits bands dark tones (koval, Xpeccy).
- Per-pixel decisions create blended/unblended speckle.
- Page-flip detection alone misfires on ordinary double-buffered games
  (Shadow Fields, Cubix) and misses GigaScreen made by rewriting attributes
  in one screen.
- Negative-lobe filters lose dynamic range (Unreal resampler).

## 6. Test material named by the sources

- Positive: GBT269, Across the Edge, Kpacku, Paralactika, "3c-mk", "3c-cat",
  animated-border GigaScreen demos (pico-spec), Multi-GigaScreen images
  IMG/GSC/MG1/MG2/MG4/MG8 (WC viewer plugin).
- **Negative** (page flipping that is *not* GigaScreen): Shadow Fields, Cubix.
