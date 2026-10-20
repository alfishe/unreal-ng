# 032 — streamdeck-jpeg-encoder

Can a small embedded baseline JPEG encoder replace the system libjpeg-turbo (`turbojpeg`, Homebrew, pkg-config) in
the Stream Deck companion? Feature: [`docs/inprogress/2026-10-19-streamdeck-integration`](../../../docs/inprogress/2026-10-19-streamdeck-integration/)
(`source-layout.md` L-6, `TODO.md` SD2). The owner decided "a small embedded JPEG encoder, after a measurement"; this
POC is that measurement.

Written 2026-10-20, before any measurement.

## Goals

1. Is there a small, permissively licensed, single-file baseline encoder that libsdb can use instead of turbojpeg for
   every image it sends (key images, the + / + XL / Neo strip, full-screen images), with no visible or practical loss?
2. If several qualify, which one (speed, output size, quality, code size, ease of vendoring)?
3. What does the switch cost or save: encode time per frame, bytes on the USB wire, binary size of `unreal-streamdeck`?

## Variants

| Name | Encoder | Licence | Settings |
|---|---|---|---|
| `a-turbojpeg` | libjpeg-turbo 3.x `tj3Compress8` (the current code, system library) | IJG / BSD-3 / zlib | 4:4:4, `TJPARAM_QUALITY`, default (Annex K) tables, standard Huffman tables; a handle per call, as `image.cpp` does |
| `b-jpge` | jpge.cpp / jpge.h v1.05 (Rich Geldreich, jpeg-compressor, commit aeb7d3b463aa) | public domain or Apache 2.0 | H1V1 (4:4:4), one pass (standard Huffman tables) |
| `b-jpge-2pass` | same | | H1V1, two pass (optimized Huffman tables) |
| `b-jpge-std` | same | | H1V1, one pass, Annex K quantization tables (`m_use_std_tables`, the same tables as turbojpeg) |
| `c-stb` | `stb_image_write.h` v1.16 (Sean Barrett), commit a7d4c2175630 | public domain or MIT | patched to 4:4:4 at every quality (upstream subsamples at quality <= 90) |
| `d-toojpeg` | TooJpeg (Stephan Brumme), commit 341de5c295e4 | zlib | `downsample = false` (4:4:4) |

## Requirements

- **Inputs at the real device sizes**, from `companions/streamdeck/lib/src/models.cpp`, in device space (rotation /
  mirror applied by `ToDeviceSpace`, outside the timed region):
  - keys: 72 (Original V2 / MK.2), 96 (XL / Neo), 112 (+ XL), 120 (+);
  - strips: 1200x100 (+ XL, encoded rotated as 100x1200), 800x100 (+), 248x58 (Neo, rotated 180);
  - full screens: 1024x600 (XL), 800x480 (+), 1280x800 (+ XL, rotated to 800x1280).
- **Realistic content** rendered by libsdb itself: every key image of the theme `data/streamdeck/themes/unrealng`
  (fitted like `LoadTheme` / `Upload` do), the theme's strip, `scenes::StatusScene` (several statuses), `LoadingScene`
  (noisy tape stripes, the heaviest scene), `TtdScene`, `Marquee`, `Splash` for full screens; plus a **noise** worst
  case (random RGB pixels) at each size.
- **Quality setting**: libsdb's default, q = 90 (`Theme::quality`, `EncodeForFormat` defaults); a sweep q = 50..98
  for the rate-distortion comparison.
- **Measured per variant and image**: encode time (median and p99 over many iterations, Release build: -O3, LTO, the
  project's flags), output bytes, HID reports per image (`ceil(bytes / 1016)` for keys, `/ 1008` for strip regions,
  `docs/companions/streamdeck/performance.md`), PSNR (RGB, 8-bit) of the decoded result against the source.
- **Decoder compatibility**: the JPEG stream structure of every variant (markers, SOF type, sampling factors, Huffman
  tables); every output must decode with two independent decoders (stb_image, libjpeg-turbo). On the attached
  Stream Deck + XL: the candidates' bytes sent raw to keys, the strip and the full screen; confirm that they render.
- **Binary size** of `unreal-streamdeck` before and after (turbojpeg is a dylib today, so its size is listed too).

## Constraints

- Work in the worktree `scratch/wt-jpeg-poc` only; nothing is committed by the POC.
- The harness may link turbojpeg (POC only, pkg-config); the product build must not after the switch.
- Builds through `tools/build/build.sh` only; the harness is target `poc032-jpegbench` (configured with
  `-DBUILD_POC=ON -DUNREAL_STREAMDECK=ON`).
- One machine: Apple silicon (arm64), macOS 15 (Darwin 24.6). Timing on a loaded shared machine: medians and p99 are
  reported with the load average; absolute numbers carry noise, ratios between variants in the same run are what
  decides. The other platforms (gcc, MSVC, MinGW) are checked for warnings, not timed.
- The device firmware is the judge for compatibility; "renders" can only be confirmed visually if a camera or the
  owner looks at the deck. If neither is available, the report says so.
- Raw data goes to `scratch/2026-10-20-poc-032/`; only summaries are kept in this folder.

## Acceptance criteria (go / no-go for a variant)

| # | Criterion | Threshold |
|---|---|---|
| A1 | Encode time | median per image <= 2x `a-turbojpeg` at the same size and content, AND a full + XL frame (36 keys at 112 px + one 1200x100 strip) encodes in <= 5 ms on this machine (the 60 fps budget is 16.7 ms and USB costs far more, ~48 ms per panel) |
| A2 | Output size | at equal PSNR (interpolated on each variant's q sweep) the total bytes of the realistic set within +15 % of `a-turbojpeg`; at q = 90 the HID report count per frame within +15 % too |
| A3 | Quality | at q = 90, PSNR not more than 1 dB below `a-turbojpeg` on the realistic set (A2 covers the trade-off) |
| A4 | Compatibility | baseline sequential (SOF0) only, 4:4:4 (all components 1x1), decodes with stb_image and libjpeg-turbo; renders on the + XL keys, strip and full screen |
| A5 | Portability | builds with clang, gcc, MSVC and MinGW; zero warnings in our code (the vendored file may be compiled with `-w`, like hidapi) |
| A6 | Size / simplicity | one or two source files, no system dependency, <= ~50 KB of object code |

Expected before measuring: turbojpeg (SIMD) is the fastest, roughly 2-4x faster than the scalar candidates, but on
images this small all variants stay well under 1 ms per key. jpge (mozjpeg-style tables, optional optimized Huffman)
should be the smallest at equal PSNR; stb and TooJpeg use Annex K tables and fixed Huffman tables and should land
within a few percent of turbojpeg. The winner is expected to be `b-jpge` (one or two pass, depending on the device and
the time cost of the second pass).

## Stage 2: our own encoder from TooJpeg with libjpeg-turbo's techniques (charter addition, 2026-10-20, before measuring)

Stage 1 (above, [conclusions.md](conclusions.md)) chose TooJpeg: turbojpeg's bytes and PSNR, 2.1x its encode time.
The owner asked for "the best of both": TooJpeg as the base of our own encoder `sdb::jpeg` (our code from now on,
`companions/streamdeck/lib/src/jpeg.cpp`, TooJpeg's zlib notice kept) with libjpeg-turbo's optimisations applied, one
step at a time, each step measured.

### Goals

4. How much of the 2.1x gap to turbojpeg closes with (S1) a buffered output and a 64-bit bit buffer, (S2) integer
   arithmetic (colour conversion over rows, libjpeg's islow DCT, reciprocal quantisation, written to auto-vectorise),
   (S3) NEON / SSE2 intrinsics if S1-S2 leave a clear gap?
5. Where does the gain stop paying for the code?

### Variants (added)

| Name | Code | What changes |
|---|---|---|
| `d-toojpeg` | TooJpeg as vendored in stage 1 (POC copy) | the reference for S1 |
| `e-step1` | POC copy of TooJpeg with S1 only | output into a vector (no callback, no `thread_local` sink), 64-bit bit buffer emitted 32 bits at a time with a fast path for words without 0xFF, value bit length by `std::countl_zero`, static Huffman / code tables built once |
| `f-sdbjpeg` | `sdb::jpeg`, the product | S1 + S2: fixed-point RGB -> YCbCr per 8-row strip (libjpeg's constants), islow integer DCT (`jfdctint.c`), quantisation by an exact 32-bit reciprocal multiply, loops shaped for auto-vectorisation |
| `g-sdbjpeg-simd` | only if built | S3 |

### Requirements (added)

- The same inputs, machines and method as stage 1 (151 images; M3 Ultra wall clock, M1 Ultra thread CPU time;
  interleaved against `a-turbojpeg` and `d-toojpeg`).
- Per step: ns per key, strip and whole + XL frame against turbojpeg; bytes; PSNR; lines of code; binary size.
- Our code, no `-w`: zero warnings on Apple clang (project build), gcc 16, MinGW gcc, MSVC (/W3 /WX at least).
- Tests in `streamdeck-tests`: golden bytes, a PSNR check, a stb_image round trip, concurrent encoders.

### Acceptance criteria (added)

| # | Criterion | Threshold |
|---|---|---|
| B1 | S1 is a pure refactor | byte-identical output to `d-toojpeg` on all 151 images at q = 50..98 |
| B2 | S2 keeps the quality | at q = 90: PSNR within 0.1 dB of turbojpeg and total bytes within 1 % per category |
| B3 | Speed | report the ratio to turbojpeg per step; a step is worth keeping if it gains >= 15 % on the + XL frame; S3 is started only if S2 is still >= 1.5x turbojpeg |
| B4 | Device | the result renders on the + XL keys and strip (owner's look) |

Expected: S1 gains 20-35 % (TooJpeg's per-byte callback and per-call table setup are a large share for 4-30 KB
outputs); S2 another 30-50 % (no float conversion per pixel, a vectorised DCT); together near 1.0-1.3x turbojpeg on
keys, since turbojpeg's SIMD wins mostly on the DCT and colour conversion. With islow and libjpeg's colour constants the
quantised coefficients should equal turbojpeg's, so S2's scan data may even be byte-identical to turbojpeg's.

## Files

| File | Content |
|---|---|
| `README.md` | this charter |
| `cpp/jpegbench.cpp`, `CMakeLists.txt` | the harness: `bench` (timing, size, PSNR, q sweep, frame), `inspect` (stream structure), `device` and `keys-compare` (the + XL) |
| `cpp/3rdparty/` | the stage 1 candidates with their licences: jpge, stb_image_write (patched to 4:4:4), TooJpeg (upstream, the base of `sdb::jpeg`) |
| `python/summarize.py` | CSV -> the report's tables |
| `measurements-2026-10-20-encoders.md` | stage 1: setup, method, tables |
| `measurements-2026-10-20-optimised.md` | stage 2: S1 / S2 against turbojpeg and TooJpeg |
| `cpp/step1/` | stage 2 step S1 (TooJpeg with the buffered output only), POC only |
| `conclusions.md` | the answers, criteria met / not met, and the analysis (the numbers against the expectations) |
| `proposal.md` | what to adopt |
