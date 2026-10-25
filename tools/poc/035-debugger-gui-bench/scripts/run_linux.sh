#!/usr/bin/env bash
# POC 035 stages 1-2 on Linux (how measurements-2026-10-23-linux.md was produced), unattended.
#
#   POC035_OUT=<scratch dir> POC035_ENV_PREFIX=<conda prefix with qt6-main 6.9.3 + mesalib> scripts/run_linux.sh
#
# Needs Xvfb. Each run gets its own Xvfb; the X server's CPU is added per phase (x_cpu_pct).
#   gpu/  Mesa no-op Gallium driver: GL calls cost only their CPU side, rasterization is free -> the CPU cost on a
#         machine with a GPU (GPU time not measured). The EGL shim avoids a crash of the no-op driver at context destroy.
#   cpu/  no usable GPU: llvmpipe for the GL paths, Qt raster for Widgets, the Quick software adaptation.
# Build: cmake -S <checkout> -B "$POC035_OUT/build" -G Ninja -DBUILD_POC=ON -DPOC035_GUI_BENCH=ON
#        -DPOC035_DEPS_PREFIX="$POC035_OUT/deps/prefix" -DCMAKE_PREFIX_PATH="$POC035_ENV_PREFIX", then
#        BUILD_DIR="$POC035_OUT/build" tools/build/build.sh poc035-all   (scripts/fetch-deps.sh first)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$POC/../../.." && pwd)
OUT=${POC035_OUT:-$ROOT/scratch/$(date +%F)-poc-035-linux}
BIN=$OUT/build/bin/poc035
R=$OUT/results
mkdir -p "$R/gpu" "$R/cpu" "$R/views" "$OUT/logs"
if [ ! -f "$OUT/libnoop_egl_shim.so" ]; then
  cc -O2 -shared -fPIC -o "$OUT/libnoop_egl_shim.so" "$POC/cpp/noop_egl_shim.c"
fi
export POC035_NOOP_SHIM=$OUT/libnoop_egl_shim.so
run() {  # run <out> <noop|sw> <binary> [args...]   (QT_QUICK_BACKEND passes through)
  local out=$1; shift
  [ -f "$out" ] && return 0
  python3 "$POC/python/run_with_xcpu.py" "$out" "$@" > "$OUT/logs/$(basename "$(dirname "$out")")-$(basename "$out" .json).log" 2>&1
}
for rep in 1 2 3; do
  for v in imgui rmlui-custom qtwidgets qtwidgets-raster qtquick qtquick-raster qtquick-sg; do
    run "$R/gpu/${v}_r$rep.json" noop "$BIN/poc035-$v"
  done
  for v in imgui rmlui-custom qtwidgets qtwidgets-raster qtquick qtquick-raster; do
    run "$R/cpu/${v}_r$rep.json" sw "$BIN/poc035-$v"
  done
  QT_QUICK_BACKEND=software run "$R/cpu/qtquick-raster-software_r$rep.json" sw "$BIN/poc035-qtquick-raster"
  QT_QUICK_BACKEND=software run "$R/cpu/qtquick-sg-software_r$rep.json" sw "$BIN/poc035-qtquick-sg"
  echo "$(date +%T) rep $rep done"
done
SHORT=(--active=10 --unthrottled=4 --idle=2)
for v in imgui qtwidgets-raster; do
  run "$R/cpu/${v}_scale2.json" sw "$BIN/poc035-$v" --scale=2 "${SHORT[@]}"
done
QT_QUICK_BACKEND=software run "$R/cpu/qtquick-raster-software_scale2.json" sw "$BIN/poc035-qtquick-raster" --scale=2 "${SHORT[@]}"
for view in disasm hex images timeline; do
  V=(--views=$view --active=8 --unthrottled=2 --idle=1)
  run "$R/views/qtwidgets-raster_$view.json" sw "$BIN/poc035-qtwidgets-raster" "${V[@]}"
  QT_QUICK_BACKEND=software run "$R/views/qtquick-raster-software_$view.json" sw "$BIN/poc035-qtquick-raster" "${V[@]}"
  run "$R/views/qtquick-raster-gpu_$view.json" noop "$BIN/poc035-qtquick-raster" "${V[@]}"
done
echo DONE
