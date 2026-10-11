# 027 — ttd-gui

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

History: added as `tools/poc/02-ttd-gui/` in `d4b5789a3` (2026-08-04), renamed to `010-ttd-gui` in
`11d1cbb67` (2026-08-26) and to `027-ttd-gui` in `d5b7abb99` (2026-10-10, 010 is ttd-compression). Its
`add_subdirectory` in `tools/poc/CMakeLists.txt` was commented out in `06afdb117` (2026-09-11) and is
still commented out.

## Goals

From `CMakeLists.txt`: "a standalone Qt6 app for offline .ttd file scrubbing with video-editor-style
timeline and instant ZX Spectrum screen rendering. No emulation — pure data." That is: can a recorded TTD
(time-travel debugging) file be scrubbed interactively, rebuilding the screen of any frame from the file
alone.

## Requirements

- Qt6 Widgets / Gui, C++20, zstd (the embedded copy in `core/src/3rdparty/zstd`).
- Reads the v1 `.ttd` format, schema 3 (`src/ttd/ttd_format.h`); materialises RAM for a frame and renders
  the ZX screen; timeline with marks.
- `--load <path>`, else a default `action_demo.ttd` / `active_demo.ttd` searched relative to the
  executable (the search list still names the old folder `tools/poc/02-ttd-gui/testdata/`).
- Timeline marks: single-frame user marks (right click) and section spans.
- `scripts/capture_ttd.py` records a `.ttd` through the emulator WebAPI (localhost:8090);
  `scripts/benchmark_frame.py` measures emulator frame time in turbo mode for three modes: no TTD, TTD
  without the write journal, TTD with the full write journal.
- Anything else: not recorded.

## Constraints

No emulation — the GUI only reads files. Built only with `-DBUILD_POC=ON`. Anything else: not recorded.

## Acceptance criteria / expected results

Not recorded. No measurement report from this POC is in the repository.

## Status

**abandoned (pre-guideline)** — the build entry has been disabled since 2026-09-11; the TTD v2 migration
documents record that its reader expects schema 3 and is "already broken"
(`docs/inprogress/2026-09-25-ttd-v2-migration/current-state.md`) and list it for retirement in Phase 6
(`phase-6-inventory.md`, `migration-trajectory.md`). The production TTD panel and scrubber is
`unreal-qt/src/widgets/ttdwidget.*` (added `b4afd9a31`, 2026-09-23); no commit or document links it to
this POC's code. Serves: `docs/inprogress/2026-09-25-ttd-v2-migration/` (as an item to retire or update).

## Files

- `src/main.cpp` — entry point and command line.
- `src/ttd/` — `.ttd` reader, frame materialisation, screen renderer, format constants.
- `src/model/marks_model.*` — timeline marks.
- `src/widgets/` — main window, timeline widget, screen widget.
- `scripts/capture_ttd.py`, `scripts/benchmark_frame.py` — see Requirements.
- `CMakeLists.txt` — target `poc_ttd_gui`, output in `<build>/bin`. Its `ROOT_DIR` is computed as
  `../../../..` from the POC folder, one level above the repository root, so the standalone build cannot
  find `core/src/3rdparty/zstd`.
