#!/usr/bin/env bash
# Video fetch oracle: zx_main (unchanged) with its line buffers on mainclk for one frame in the ZX mode, trace in
# scratch/2026-11-11-poc-051/video/zx.txt, then video_fetch.py maps every screen read to the emulator's frame.
# About 2 minutes of ghdl.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WORK="$ROOT/scratch/2026-11-11-poc-051"
SRC="$WORK/src"
[ -d "$SRC/.git" ] || "$HERE/fetch.sh"
mkdir -p "$WORK/video"
cd "$WORK/video"
FLAGS=(--std=08 -fsynopsys -frelaxed --workdir="$WORK/video")
ghdl -a "${FLAGS[@]}" "$SRC/common/ram/ram_32.VHD" "$SRC/common/ram/ram_border.VHD" "$SRC/common/zx_main.vhd"
ghdl -a "${FLAGS[@]}" "$HERE/tb/p2666_video_tb.vhd"
ghdl -e "${FLAGS[@]}" p2666_video_tb
# border writes (FE[2:0] = 2, 5, 3) at emulator line 100 T 10, line 100 T 180, line 60 T 100: edge = T * 32 - 114176
ghdl -r "${FLAGS[@]}" p2666_video_tb -gTRACE="$WORK/video/zx.txt" -gMAX_MAINCLK=2300000 \
    -gFE_EDGE1=602944 -gFE_EDGE2=608384 -gFE_EDGE3=319104 --ieee-asserts=disable
python3 -I "$HERE/video_fetch.py" "$WORK/video/zx.txt" 
