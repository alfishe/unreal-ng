# 035 — debugger-gui-bench

Which GUI technology should the new debugger (ZX Spectrum Next / TSConf / Sprinter views, TTD timeline, several windows
and profiles) be built on, so that it costs little CPU, works without a usable GPU like the emulator itself, scales
cleanly (HiDPI, font size) and does not look like a tool from the 1990s? Feature:
[`docs/inprogress/2026-09-28-debugger-model`](../../../docs/inprogress/2026-09-28-debugger-model/) (the "modern GUI
skin", `gui-main-debugger.md`); the Next / TSConf / Sprinter debugger mock-ups of 2026-10-22 define the screen measured
here.

Charter written 2026-10-24. Stages 1 and 2 (Linux) were measured on 2026-10-22/23 in cloud sessions before the POC was
registered; their method and numbers are kept as they were measured ([measurements-2026-10-23-linux.md](measurements-2026-10-23-linux.md)).
Stage 3 (macOS, the owner's machine) is chartered here before any macOS measurement: its expectations below are
written before running it.

## Goals

1. For a debugger screen with the real hot views (hex dump with writes highlighted, disassembly following PC, a TTD
   timeline of 500 000 events zooming every frame, four 640 x 512 RGBA layer images refreshed every frame), what does
   each candidate cost per frame and per second at 50 Hz: CPU of the process, CPU of the compositor (X server /
   WindowServer), memory footprint, frame time p50 / p99, headroom (unthrottled frames/s), idle cost?
2. Does each candidate hold 50 Hz **without a GPU** (no usable GPU / no driver / VM / software rendering), and at what
   CPU cost?
3. How do HiDPI (2x, x1.5 UI scale) and run-time font / scale switching change the cost?
4. Is a thin own CPU raster for the dense views (glyph atlas, per-cell dirty tracking) on top of Qt cheaper than
   painting them with the toolkit, and which Qt front end (Widgets / Quick) should carry it?
5. Stage 3: do the Linux conclusions hold on macOS (Apple silicon, Retina, Metal, WindowServer), where the emulator's
   main users and the owner work?

## Variants

All variants draw the same 1600 x 1000 (logical) screen from one deterministic workload (`cpp/common/workload.*`) and
are measured by one harness (`cpp/common/harness.*`). Names are the same in every file.

| Name | Toolkit | How the hot views are drawn |
|---|---|---|
| `imgui` | Dear ImGui (docking, 1.92 WIP @ d88f238) + SDL3 3.2.24 + OpenGL 3.3 | `ImDrawList` per view, textures by `glTexSubImage2D` |
| `rmlui-custom` | RmlUi master @ 821cbd2 + SDL3 + its GL3 backend | RML/RCSS panels; hex, disassembly, timeline are custom elements building their own meshes |
| `rmlui-dom` | same | the hot views are DOM nodes updated in place (only changed nodes touched) |
| `qtwidgets` | Qt 6 Widgets | `QPainter` per view on the raster backing store; repaints only the four views |
| `qtwidgets-raster` | Qt 6 Widgets | own CPU raster per view (`cpp/qtcommon/rasterviews.h`): QImage in device pixels, glyph atlas, dirty cells / rows / columns; Qt repaints only the dirty rectangles |
| `qtquick` | Qt 6 Quick (QML) | `Repeater` + `Text` delegates, `QSGGeometryNode` timeline, texture nodes for images |
| `qtquick-sg` | Qt 6 Quick | own scene-graph nodes, Qt 6.7+ `QSGTextNode` rows |
| `qtquick-raster` | Qt 6 Quick | the same own CPU raster as `qtwidgets-raster`, each view one `QSGImageNode` texture |

Each Quick variant runs on the RHI (Metal on macOS, OpenGL on Linux) and on the software adaptation
(`QT_QUICK_BACKEND=software`, suffix `-software`). The SDL variants run on the GPU and on a software GL (Linux: Mesa
llvmpipe; macOS: Apple's software renderer, `POC035_GL_SOFTWARE=1`, suffix `-sw`). Widgets always rasterize on the CPU.

## Requirements

- Same picture in every variant (screenshots checked), same data per frame, workload cost measured and subtracted.
- Phases per run: warm-up 2 s, active 15 s at 50 Hz, unthrottled 8 s, idle 8 s (machine paused, event-driven idle);
  switching runs: font 12 <-> 16 px and scale x1 <-> x1.5 every 250 ms for 10 s.
- CPU: the process (all threads, `CLOCK_PROCESS_CPUTIME_ID`, sampled each second) **plus the compositor's CPU** over the
  same wall window: Xvfb (`/proc/<pid>/stat`) on Linux, WindowServer (`ps`) on macOS minus its idle level before the
  run. Without a GPU the compositor receives every frame, so a variant's cost is both.
- Memory: Linux PSS / private / anonymous / file from `smaps_rollup`; macOS `phys_footprint` (what Activity Monitor
  shows; includes GPU / IOSurface memory charged to the task), resident, compressed (`task_info TASK_VM_INFO`).
- Frame time includes the GPU finishing the frame (`glFinish` / `QRhi::finish`), so a GPU-bound frame is not hidden.
- With and without the GPU on both platforms; HiDPI at the display's native ratio (Retina 2x) and at one pixel per
  logical pixel (`--scale=0.5` on a 2x display) to compare with Linux; three repetitions of the main runs.
- Qt 6.9.3 (the version unreal-qt is built with).

## Constraints

- Nothing in `core/` or the apps changes; the POC is opt-in (`-DBUILD_POC=ON -DPOC035_GUI_BENCH=ON`), its targets are
  `EXCLUDE_FROM_ALL`, built by `tools/build/build.sh poc035-all` in its own build folder.
- Third-party libraries (SDL3, Dear ImGui, FreeType, RmlUi) are fetched and built at pinned commits by
  `scripts/fetch-deps.sh` into the scratch folder, outside the emulator's CMake tree; they are not vendored.
- Raw data, builds, logs and screenshots go to `scratch/YYYY-MM-DD-poc-035/`; only summaries are kept here.
- Linux stages ran in a 2-vCPU cloud VM (Xeon 2.1 GHz, no GPU): the "with GPU" numbers there are the CPU side only
  (Mesa no-op driver, rasterization free); absolute numbers carry VM noise, ratios within one run set decide.
- macOS: one machine (the owner's Mac Studio, Apple silicon); the agent cannot type into a terminal on it, so the
  owner starts `scripts/run_mac.sh` once and leaves the machine alone for ~40 min. WindowServer always composites on
  the GPU; "without a GPU" on macOS means the application itself does not use it.
- GPU time itself is not measured (no counters); GPU load shows only as frame time (finish) and WindowServer CPU.

## Acceptance criteria

| # | Criterion | Threshold |
|---|---|---|
| C1 | Holds 50 Hz | active phase >= 47.5 frames/s, p99 frame <= 20 ms, with the full screen, with and without the GPU |
| C2 | CPU budget | process + compositor <= 20 % of one core at 50 Hz with the GPU, <= 35 % without (the emulator itself needs the rest of a core on the heavy machines, `docs/inprogress/2026-10-13-heavy-machines-perf`) |
| C3 | Idle | <= 0.1 % of a core when nothing changes |
| C4 | Memory | footprint <= ~150 MB for this screen (the owner: ~100 MB is acceptable, CPU matters more) |
| C5 | Scaling | x2 / x1.5 / font switching keeps C1; a switch frame <= 50 ms |
| C6 | Looks / customisation | native-looking, styleable (not ImGui's look: the owner ruled ImGui out for the product UI on 2026-10-22) |
| C7 | Toolkit fit | C++ only; runs where unreal-qt runs (macOS, Linux, Windows; no discrete GPU required) |

A variant is recommended if it meets C1-C5 both with and without the GPU on both platforms and C6-C7.

## Expected results for stage 3 (macOS), written before measuring

- Apple silicon per-core speed is ~2-3x the cloud Xeon: every CPU number should drop accordingly, ratios between
  variants should stay as on Linux.
- Retina doubles the pixels per side: the own-raster variants (`qtwidgets-raster`, `qtquick-raster`) do ~4x the pixel
  work of the Linux runs; Linux x2 cost them ~2.8x (27 -> 76 % of a core). Expected on macOS at 2x: `qtwidgets-raster`
  15-30 %, `qtquick-raster` on Metal 8-20 %, both holding 50 Hz.
- GPU-rendered variants (`imgui`, `rmlui-custom`, `qtquick-raster` on Metal) should be cheapest with the GPU (single-digit
  % for ImGui / RmlUi).
- Apple's software GL should be as poor as llvmpipe or worse (it is not optimized for throughput): ImGui / RmlUi
  without the GPU expected not to hold 50 Hz at 2x.
- `qtquick` (QML delegates) and `qtquick-sg` (QSGTextNode) stay the most expensive Qt variants: text layout per changed
  row dominates, independent of the platform.
- WindowServer adds a few % of a core per variant (it composites every presented frame); more for full-window
  CPU-backed surfaces than for partial updates.
- The Linux recommendation (own raster for the dense views + Qt; Widgets or Quick with an automatic software fallback)
  is expected to hold.

## Files

| File | Content |
|---|---|
| `README.md` | this charter |
| `CMakeLists.txt` | targets `poc035-imgui`, `poc035-rmlui-dom`, `poc035-rmlui-custom`, `poc035-qtwidgets`, `poc035-qtwidgets-raster`, `poc035-qtquick`, `poc035-qtquick-sg`, `poc035-qtquick-raster`, `poc035-all` |
| `cpp/common/` | workload, layout, harness (Linux `/proc`, macOS `task_info`, App Nap opt-out) |
| `cpp/imgui/`, `cpp/rmlui/`, `cpp/qt*/` | the variants; `cpp/qtcommon/rasterviews.h` is the own CPU raster shared by the two `-raster` variants |
| `cpp/sdlcommon/sdlgl.h`, `cpp/qtcommon/quickhelpers.h` | HiDPI, software GL, RHI selection and GPU finish for the SDL / Quick variants |
| `cpp/3rdparty/stb/` | `stb_image_write.h` (screenshots) |
| `assets/fonts/` | DejaVu Sans Mono and its licence: the same glyphs on every platform |
| `scripts/fetch-deps.sh` | SDL3, Dear ImGui, FreeType, RmlUi at pinned commits |
| `scripts/run_mac.sh` | stage 3: build through `tools/build/build.sh` and run the macOS matrix |
| `scripts/run_linux.sh` | stages 1-2: the Linux matrix (Xvfb, Mesa no-op / llvmpipe) |
| `python/run_with_wscpu.py`, `python/run_with_xcpu.py` | run one binary and add the compositor's CPU per phase |
| `python/summarize_matrix.py` | results of `run_mac.sh` / `run_linux.sh` -> the report tables; `summarize.py`, `summarize_scaling.py`, `make_gallery.py`: stage 1 (Linux, Qt 6.4.2) tables and gallery |
| `measurements-2026-10-23-linux.md` | stages 1-2: setup, method, tables |
| `measurements-2026-10-24-macos.md` | stage 3 (after the run) |
| `analysis.md`, `conclusions.md`, `proposal.md` | after stage 3 |
