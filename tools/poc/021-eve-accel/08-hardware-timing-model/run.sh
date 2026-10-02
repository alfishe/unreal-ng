#!/bin/sh
# 08: the hardware-level line timing model (public documentation only).
#   08-hardware-timing-model/run.sh   -> out/costs.txt (line cost vs budget on the captures),
#                                        out/clocked.txt (clock stepping vs closed form),
#                                        out/gpu-line-threads.txt (one GPU thread per line)
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
{
    "$POC/common/replay.sh" base boot --no-hash --costs | tail -4
    "$POC/common/replay.sh" base play --no-hash --costs | tail -4
    "$POC/common/replay.sh" base zuma --no-hash --costs | tail -4
} 2>&1 | tee "$OUT/costs.txt"
"$POC/common/wait-load.sh" 600 12 | tee "$OUT/clocked.txt"
for run in 1 2; do "$POC/build/clocked-model"; done 2>&1 | tee -a "$OUT/clocked.txt"
SNAP=$POC/04-metal-compute/out/snap-play
FRAMES=$(ls "$SNAP"/frame-*.snap | sort -t- -k2 -n | tail -8)
{
    echo "loadavg: $(sysctl -n vm.loadavg)"
    # shellcheck disable=SC2086
    "$POC/build/metal-render" --repeat 3 $FRAMES | tail -1
    # shellcheck disable=SC2086
    "$POC/build/metal-render" --repeat 3 --line-threads $FRAMES | tail -1
} 2>&1 | tee "$OUT/gpu-line-threads.txt"
