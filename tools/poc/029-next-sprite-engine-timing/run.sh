#!/bin/bash
# POC 029: the sprite engine's clock budget, wrap and collision rules, measured on sprites.vhd alone (GHDL, VHDL-2008).
# Usage: run.sh <cores/zxnext/src> [work dir]
# Prints the overtime / cut-off table (sweep) and the collision cases (q3).
set -euo pipefail
SRC=${1:?path to cores/zxnext/src}
WORK=${2:-scratch/2026-10-12-poc-029}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$WORK"
cp "$HERE/models.vhd" "$HERE/tb.vhd" "$HERE/q3.sh" "$WORK/"
cd "$WORK"
ghdl -a --std=08 -fsynopsys models.vhd "$SRC/video/sprites.vhd" tb.vhd
ghdl -e --std=08 -fsynopsys tb
# the budget sweep: L pixels per line (448 = 48K / Pentagon, 456 = 128K / +3), K unscaled sprites of 16 pixels at the same y, a tuner
# sprite (index 126, x = 320 - m), a probe sprite of 16 pixels at index 127 (x = 288): T = 1 + 128 + 16 K + m + 16 clocks
for L in 448 456; do
  for K in $(( (4 * L - 145) / 16 )) $(( (4 * L - 145) / 16 + 1 )); do
    for m in 1 4 8 12 15 16; do
      r=$(./tb -gg_len=$L -gg_k=$K -gg_tx=$((320 - m)) 2>&1 | grep -E "STATUS|SAMPLE" | tail -2 | tr '\n' ' ')
      echo "L=$L K=$K m=$m T=$((1 + 128 + 16 * K + m + 16)) 4L=$((4 * L)) $r"
    done
  done
done
bash q3.sh
