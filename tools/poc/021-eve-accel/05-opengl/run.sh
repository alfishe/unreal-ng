#!/bin/sh
# 05: the op list as an OpenGL 4.1 fragment shader, on the snapshots of 04.
#   05-opengl/run.sh   -> out/gl-<trace>.txt
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
"$POC/common/wait-load.sh" 600 12
for t in play boot zuma; do
    "$POC/build/gl-render" --repeat 10 "$POC"/04-metal-compute/out/snap-$t/*.snap > "$OUT/gl-$t.txt" 2>&1
    echo "== $t"; head -2 "$OUT/gl-$t.txt"; tail -2 "$OUT/gl-$t.txt"
done
echo "loadavg: $(sysctl -n vm.loadavg)"
