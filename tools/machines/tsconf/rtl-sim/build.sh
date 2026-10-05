#!/usr/bin/env bash
# Builds the TS-Conf video RTL simulator with Verilator.
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
verilator --cc --exe --build -j "$JOBS" -O2 \
  --top-module tbtop --Mdir "$OUT/obj" -o "$OUT/tsconf-video-sim" \
  --default-language 1800-2017 \
  -Wno-fatal -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-PINCONNECTEMPTY -Wno-UNUSEDSIGNAL \
  -Wno-UNUSEDPARAM -Wno-UNDRIVEN -Wno-CASEINCOMPLETE -Wno-MULTIDRIVEN -Wno-SYNCASYNCNET \
  -Wno-PINMISSING -Wno-DECLFILENAME -Wno-BLKSEQ -Wno-INITIALDLY -Wno-UNSIGNED \
  -I"$RTL/quartus" -I"$RTL/video" -I"$RTL/dram" -I"$RTL/common" \
  "$HERE/tbtop.v" "$HERE/altdpramstub.v" \
  "$RTL/common/clock.v" "$RTL/dram/arbiter.v" \
  "$RTL/video/video_top.v" "$RTL/video/video_ports.v" "$RTL/video/video_mode.v" \
  "$RTL/video/video_sync.v" "$RTL/video/video_fetch.v" "$RTL/video/video_render.v" \
  "$RTL/video/video_out.v" "$RTL/video/video_ts.v" "$RTL/video/video_ts_render.v" \
  "$HERE/harness.cpp"

echo "built: $OUT/tsconf-video-sim"
