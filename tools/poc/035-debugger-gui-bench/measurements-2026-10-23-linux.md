# POC 035 — measurements, Linux (stages 1 and 2), 2026-10-22/23

Measured in cloud sessions before the POC was registered, with the same sources as this folder (before the macOS
port: the variant directories were then named `qtwidgets_raster` etc., the font was the system DejaVu Sans Mono, the
results carry no `platform` / `backend` fields). Method as in the charter; the scripts are `scripts/run_linux.sh` and
`python/summarize_matrix.py` (stage 2), `python/summarize.py` / `summarize_scaling.py` (stage 1).

## Setup

| | Stage 1 | Stage 2 |
|---|---|---|
| Machine | cloud VM, 2 vCPU Intel Xeon @ 2.1 GHz (AVX-512), no GPU, kernel 6.18 | same type, another host after a container restart |
| Display | Xvfb 21.1.12, 1600 x 1000 window, 24 bit | own Xvfb per run (3200 x 2000), its CPU added per phase |
| Qt | 6.4.2 (Ubuntu 24.04 packages) | 6.9.3 (conda-forge `qt6-main`; download.qt.io was not reachable) |
| GL | Mesa 25.2.8 | Mesa 26.2.1 (conda-forge `mesalib`), one Mesa and libstdc++ for every variant |
| SDL3 / ImGui / RmlUi | release-3.2.24 / docking d88f238 / master 821cbd2 | same |
| Compiler | gcc 13.3, `-O3 -DNDEBUG` | same |

"With GPU" = Mesa's no-op Gallium driver (`GALLIUM_NOOP=1` over EGL, `cpp/noop_egl_shim.c` against its crash on
context destroy): GL calls cost their CPU side only, rasterization is free, so it is the CPU cost on a machine with any
GPU; GPU time is not measured. "Without GPU" = llvmpipe for GL, Qt's raster for Widgets, the Quick software
adaptation. Without a GPU the X server receives every frame, so stage 2 adds the X server's CPU ("compositor").

Absolute numbers between stage 1 and stage 2 differ by the host (ImGui's frame went 0.55 -> 1.22 ms with the same
code), not by Qt: compare variants within one stage.

## Stage 2: Qt 6.9.3, with and without the GPU (median of 3 runs)

CPU in % of one core at 50 Hz; footprint = private memory (clean + dirty).

### With the GPU (Mesa no-op: the CPU side of the work)

| Variant | Process CPU, % | Compositor, % | Total, % of a core | Worst second, % | Frame p50, ms | p99, ms | Holds 50 Hz | Headroom, frames/s | Footprint, MB | Idle, % |
|---|---|---|---|---|---|---|---|---|---|---|
| `imgui` | 5.9 | 0.5 | 6.4 | 7.3 | 1.22 | 4.07 | yes | 923 | 92.1 | 0.01 |
| `rmlui-custom` | 7.5 | 0.5 | 8.0 | 9.6 | 1.52 | 3.91 | yes | 743 | 99.5 | 0.01 |
| `qtquick-raster` (OpenGL RHI) | 15.0 | 0.4 | 15.3 | 16.1 | 3.06 | 5.35 | yes | 333 | 132.6 | 0.00 |
| `qtwidgets-raster` | 20.2 | 7.9 | 28.1 | 21.2 | 4.11 | 7.01 | yes | 248 | 42.6 | 0.01 |
| `qtwidgets` | 43.8 | 8.5 | 52.3 | 52.8 | 8.47 | 15.09 | yes | 121 | 36.6 | 0.02 |
| `qtquick` (OpenGL RHI) | 63.8 | 0.4 | 64.2 | 74.2 | 12.17 | 22.43 | yes | 80 | 149.6 | 0.00 |
| `qtquick-sg` (OpenGL RHI) | 79.0 | 0.3 | 79.3 | 82.0 | 15.29 | 32.76 | 45.4 fps | 60 | 144.4 | 0.00 |

### Without the GPU (llvmpipe / Qt raster / Quick software adaptation), X server included

| Variant | Process CPU, % | Compositor, % | Total, % of a core | Worst second, % | Frame p50, ms | p99, ms | Holds 50 Hz | Headroom, frames/s | Footprint, MB | Idle, % |
|---|---|---|---|---|---|---|---|---|---|---|
| `qtwidgets-raster` | 19.7 | 7.6 | 27.3 | 22.1 | 3.97 | 7.05 | yes | 244 | 42.5 | 0.01 |
| `qtquick-raster-software` | 25.9 | 8.0 | 33.8 | 33.4 | 5.21 | 9.06 | yes | 192 | 61.4 | 0.00 |
| `qtwidgets` | 42.2 | 8.3 | 50.5 | 52.5 | 8.06 | 14.83 | yes | 127 | 36.6 | 0.00 |
| `qtquick-sg-software` | 82.5 | 6.4 | 89.1 | 84.6 | 26.42 | 45.59 | 30.1 fps | 37 | 60.2 | 0.03 |
| `qtquick-raster` (RHI on llvmpipe) | 126.5 | 7.2 | 133.7 | 132.0 | 18.90 | 26.14 | 43.4 fps | 50 | 201.1 | 0.03 |
| `qtquick` (RHI on llvmpipe) | 140.8 | 3.5 | 144.4 | 144.9 | 43.79 | 63.99 | 20.4 fps | 22 | 206.8 | 0.01 |
| `imgui` (llvmpipe) | 156.7 | 3.7 | 160.4 | 163.1 | 44.78 | 64.83 | 20.5 fps | 20 | 151.7 | 0.01 |
| `rmlui-custom` (llvmpipe) | 176.6 | 1.8 | 178.4 | 180.1 | 91.84 | 129.39 | 10.2 fps | 11 | 211.4 | 0.01 |

More than 100 % = more than one core (llvmpipe's threads).

### UI scale x2 without the GPU (one run each; 3200 x 2000 framebuffer)

| Variant | Total, % of a core (x1 -> x2) | Frame p50, ms (x1 -> x2) | Holds 50 Hz at x2 | Footprint x2, MB |
|---|---|---|---|---|
| `imgui` (llvmpipe) | 160.4 -> 167.9 | 44.78 -> 117.34 | 7.8 fps | 174.5 |
| `qtwidgets-raster` | 27.3 -> 76.2 | 3.97 -> 10.38 | yes | 59.4 |
| `qtquick-raster-software` | 33.8 -> 101.6 | 5.21 -> 15.62 | yes | 89.3 |

### One view alone (frame p50 ms / total % of a core)

| Variant | disasm | hex | images | timeline |
|---|---|---|---|---|
| `qtwidgets-raster` (no GPU) | 1.53 / 10.6 | 0.82 / 5.3 | 1.67 / 10.4 | 1.23 / 8.9 |
| `qtquick-raster-software` | 1.84 / 12.2 | 1.22 / 8.0 | 2.05 / 12.0 | 1.63 / 10.7 |
| `qtquick-raster` (no-op GPU) | 1.39 / 7.1 | 0.90 / 4.7 | 1.72 / 8.7 | 1.19 / 6.2 |

Spread of frame p50 between the 3 runs: within ±5 % for every variant except `qtquick-sg` with the GPU
(14.98–22.26 ms) and `qtwidgets` (7.70–8.83 ms).

## Stage 1: Qt 6.4.2, the first five variants (median of 3 runs, no-op GL, X server not counted)

`qtwidgets` here repainted the whole window every frame (fixed in stage 2: the four views only); the comparison of
toolkits is unaffected in direction, Widgets' absolute cost is overstated.

| Variant | CPU per frame, ms | Frame p50, ms | p99 | CPU at 50 Hz, % (mean / worst second) | Headroom, frames/s | Idle, % |
|---|---|---|---|---|---|---|
| `imgui` | 0.47 | 0.55 | 1.09 | 2.4 / 3.0 | 2402 | 0.008 |
| `rmlui-custom` | 0.72 | 0.78 | 1.30 | 3.6 / 4.1 | 1644 | 0.009 |
| `rmlui-dom` | 21.84 | 21.74 | 37.66 | 88.0 / 96.9 | 48 | 0.009 |
| `qtwidgets` (whole window) | 6.68 | 6.78 | 12.41 | 33.4 / 41.6 | 163 | 0.001 |
| `qtquick` | 6.20 | 5.97 | 11.29 | 31.0 / 34.3 | 166 | 0.003 |

Memory (MB; the workload's own data is in every number):

| Variant | RSS empty window | RSS full | PSS | private | private: full − empty |
|---|---|---|---|---|---|
| `imgui` | 81.5 | 85.6 | 64.8 | 45.6 | 4.1 |
| `rmlui-custom` | 89.4 | 94.9 | 73.2 | 53.1 | 5.5 |
| `rmlui-dom` | 89.6 | 136.8 | 114.8 | 94.6 | 47.2 |
| `qtwidgets` | 39.9 | 42.4 | 35.3 | 29.9 | 2.4 |
| `qtquick` | 116.6 | 148.2 | 126.0 | 105.5 | 31.7 |

UI scale through each toolkit's own HiDPI path, CPU per frame ms / private MB (no-op GL):

| Variant | x1.0 | x1.25 | x1.5 | x2.0 | growth x1 -> x2 |
|---|---|---|---|---|---|
| `imgui` | 0.52 / 46.3 | 0.49 / 46.1 | 0.51 / 46.6 | 0.50 / 46.3 | 0.95x |
| `rmlui-custom` | 0.80 / 52.8 | 0.85 / 52.3 | 0.83 / 52.8 | 0.84 / 53.0 | 1.06x |
| `rmlui-dom` | 25.11 / 94.6 | 25.02 / 94.6 | 24.76 / 94.6 | 24.83 / 93.9 | 0.99x |
| `qtwidgets` (whole window) | 6.85 / 30.1 | 7.68 / 30.0 | 8.98 / 30.1 | 11.90 / 30.1 | 1.74x |
| `qtquick` | 6.38 / 106.9 | 6.68 / 105.5 | 6.34 / 106.4 | 6.52 / 106.2 | 1.02x |

Run-time switching, the frame that applies the change (median / worst of ~40, ms), and the 3 frames after it:

| Variant | font 12 <-> 16 | settle | scale x1 <-> x1.5 | settle | steady frame |
|---|---|---|---|---|---|
| `imgui` | 0.60 / 1.09 | 0.61 | 1.83 / 4.71 | 0.59 | 0.58 |
| `rmlui-custom` | 0.92 / 1.84 | 0.85 | 2.40 / 5.77 | 0.89 | 0.84 |
| `rmlui-dom` | 28.89 / 35.60 | 26.51 | 32.69 / 54.11 | 27.83 | 25.68 |
| `qtwidgets` | 7.49 / 10.98 | 6.63 | no run-time API | — | 6.90 |
| `qtquick` | 36.83 / 64.12 | 5.98 | 9.10 / 18.42 (root item scaled) | 5.59 | 6.22 |

With llvmpipe the RmlUi GL3 renderer's MSAA x2 layer buffers grow with the framebuffer: RSS 208 -> 428 MB from x1 to
x2 (`rmlui-custom`), ImGui 150 -> 211 MB, Qt Widgets 57 -> 113 MB.
