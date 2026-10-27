# 013 — nvenc-poc

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

History: added as `tools/nvenc_poc/test_quality_presets.py` in `42f79bde6` (2026-07-17, "docs(nvenc):
update integration docs with blur fix and quality test results"), moved to `tools/poc/nvenc_poc/` in
`11d1cbb67` (2026-08-26), copied to `tools/poc/013-nvenc-poc/` in `06afdb117` (2026-09-11); the duplicate
`nvenc_poc` was removed in `d5b7abb99` (2026-10-10).

## Goals

Check that the five UI quality levels of the NVENC video recorder (Fastest, Fast, Medium, High, Best)
give monotonically larger, sharper output, with the encoder parameters the C++ `NvencEncoder` uses
(NVENC presets p1 / p3 / p5 / p6 / p7, constant QP 28 / 24 / 20 / 16 / 12), on pixel-art content.

## Requirements

- An NVIDIA GPU and an `ffmpeg` with `hevc_nvenc` / `h264_nvenc`; Python 3.
- Source: an animated mandelbrot zoom at 352x288 (ZX frame with border), nearest-neighbour upscaled x2,
  50 fps, 4 s; HEVC by default, H.264 with `--h264`.
- The preset / QP tables in the script must match
  `core/recording/src/platform/windows/nvenc_encoder.cpp` (they did on 2026-10-10: QP = 30 − 2·q,
  clamped to 8..28).

## Constraints

It drives `ffmpeg`, not the emulator's encoder, so it validates the parameter choice, not the C++ code
path. Anything else: not recorded.

## Acceptance criteria / expected results

From the script: file size increases monotonically from Fastest to Best ("PASS", else "WARN"), and detail is
kept: the luma standard deviation at Best is at least 0.95 of that at Fastest ("PASS", else "WARN").

## Status

**concluded (pre-guideline)** — results recorded in
`docs/emulator/design/recording/nvenc-integration.md` ("Quality Preset Test Results"): monotonic size,
detail preservation and profile checks pass for HEVC and H.264, 3.3–4.7x size from Fastest to Best. The
production counterpart is `core/recording/src/platform/windows/nvenc_encoder.cpp`. No `docs/inprogress/`
folder references this POC.

## Files

- `test_quality_presets.py` — generates the source with `ffmpeg`, encodes the five levels, extracts a
  frame per level, measures size / bitrate / luma statistics and prints the verdicts. Run:
  `python tools/poc/013-nvenc-poc/test_quality_presets.py [--h264]`. It writes its output to
  `<repo>/scratch/nvenc_quality_test`.
