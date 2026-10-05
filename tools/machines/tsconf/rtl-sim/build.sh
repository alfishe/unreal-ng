#!/usr/bin/env bash
# Builds the TS-Conf RTL simulators with Verilator: tsconf-video-sim (video pipeline)
# and tsconf-cpu-sim (CPU memory waits).
#
# Usage: build.sh [RTL_DIR] [BUILD_DIR]
#   RTL_DIR   - the ZX-Evo "pentevo/fpga/current" folder (the one with video/, dram/,
#               common/, quartus/tune.v). Defaults to $TSCONF_RTL_DIR.
#   BUILD_DIR - where Verilator output and the binary go. Defaults to
#               <repo>/scratch/rtl-sim-build (git-ignored).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"
RTL="${1:-${TSCONF_RTL_DIR:-}}"
OUT="${2:-$REPO/scratch/rtl-sim-build}"

if [[ -z "$RTL" || ! -f "$RTL/video/video_top.v" ]]; then
  echo "error: pass the RTL folder (pentevo/fpga/current) as argument 1 or set TSCONF_RTL_DIR" >&2
  exit 1
fi

CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
JOBS=$(( CORES / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))

mkdir -p "$OUT"
VFLAGS=(--cc --exe --build -j "$JOBS" -O2 --default-language 1800-2017
  -Wno-fatal -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL
  -Wno-UNUSEDPARAM -Wno-UNDRIVEN -Wno-CASEINCOMPLETE -Wno-MULTIDRIVEN -Wno-SYNCASYNCNET
  -Wno-PINMISSING -Wno-DECLFILENAME -Wno-BLKSEQ -Wno-INITIALDLY -Wno-UNSIGNED
  -I"$RTL/quartus" -I"$RTL/video" -I"$RTL/dram" -I"$RTL/common" -I"$RTL/z80")
VIDEO=("$RTL/video/video_top.v" "$RTL/video/video_ports.v" "$RTL/video/video_mode.v"
  "$RTL/video/video_sync.v" "$RTL/video/video_fetch.v" "$RTL/video/video_render.v"
  "$RTL/video/video_out.v" "$RTL/video/video_ts.v" "$RTL/video/video_ts_render.v")

verilator "${VFLAGS[@]}" --top-module tbtop --Mdir "$OUT/obj" -o "$OUT/tsconf-video-sim" \
  "$HERE/tbtop.v" "$HERE/altdpramstub.v" \
  "$RTL/common/clock.v" "$RTL/dram/arbiter.v" "${VIDEO[@]}" \
  "$HERE/harness.cpp"
echo "built: $OUT/tsconf-video-sim"

verilator "${VFLAGS[@]}" --top-module tbcpu --Mdir "$OUT/obj-cpu" -o "$OUT/tsconf-cpu-sim" \
  "$HERE/tbcpu.v" "$HERE/altdpramstub.v" \
  "$RTL/common/clock.v" "$RTL/dram/arbiter.v" "${VIDEO[@]}" \
  "$RTL/z80/zclock.v" "$RTL/z80/zsignals.v" "$RTL/z80/zmem.v" \
  "$HERE/cpuharness.cpp"
echo "built: $OUT/tsconf-cpu-sim"
