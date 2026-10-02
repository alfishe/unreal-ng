#!/bin/sh
# 04b: one dispatch per line / per band of N lines - the per-dispatch and per-sync cost.
# Uses the R-Type play snapshots of 04 (04-metal-compute/run.sh snap).
#   04b-line-per-dispatch/run.sh    -> out/sweep.txt
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
SNAP=$POC/04-metal-compute/out/snap-play
mkdir -p "$OUT"
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
# eight in-game frames (level graphics, 87-232 ops)
FRAMES=$(ls "$SNAP"/frame-*.snap | sort -t- -k2 -n | tail -8)
{
    "$POC/common/wait-load.sh" 600 12
    "$POC/build/metal-render" --overhead --repeat 1 | grep -E "dispatch"
    # shellcheck disable=SC2086
    "$POC/build/metal-render" --repeat 5 $FRAMES | tail -1
    for n in 1 2 4 8 16 32 64 128 256 768; do
        echo "== bands of $n lines"
        # shellcheck disable=SC2086
        "$POC/build/metal-render" --repeat 5 --bands $n $FRAMES | tail -2
        # shellcheck disable=SC2086
        "$POC/build/metal-render" --repeat 5 --bands $n --concurrent $FRAMES | tail -2
        # shellcheck disable=SC2086
        "$POC/build/metal-render" --repeat 3 --bands $n --sync $FRAMES | tail -2
    done
    for n in 1 16 768; do
        echo "== 256 threads per pixel, bands of $n lines"
        # shellcheck disable=SC2086
        "$POC/build/metal-render" --repeat 2 --bands $n --sub $FRAMES | tail -2
    done
    echo "loadavg at the end: $(sysctl -n vm.loadavg)"
} 2>&1 | tee "$OUT/sweep.txt"
