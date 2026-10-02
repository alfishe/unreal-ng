#!/bin/sh
# 06: the antialiasing models against the BT8XX golden cases, then the GPU timing.
#   06-gpu-aa-256/run.sh golden    every model on the AA golden cases (out/golden-<model>.txt)
#   06-gpu-aa-256/run.sh gpu       Metal: 256-subsample coverage vs the table, timed
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
# The BT8XX goldens live in an eve-emu checkout (testdata/golden; the vendored copy has no tests)
GOLDEN=${EVE_GOLDEN:?set EVE_GOLDEN to the testdata/golden folder of an eve-emu checkout}
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
mkdir -p "$OUT"
CASES="aa-lines-a aa-lines-b aa-lines-c aa-lines-d aa-points-a aa-points-b aa-points-c lines points rects probe-line probe-point probe-rect"
case "$1" in
golden)
    for model in table box256 box256c box256le; do
        dirs=""
        for c in $CASES; do dirs="$dirs $GOLDEN/$c"; done
        # shellcheck disable=SC2086
        EVE_AA_MODEL=$model "$POC/build/golden-run" $dirs > "$OUT/golden-$model.txt"
        echo "== $model"; cat "$OUT/golden-$model.txt"
    done
    ;;
gpu)
    echo "loadavg: $(sysctl -n vm.loadavg)"
    "$POC/build/metal-aa" | tee "$OUT/metal-aa.txt"
    echo "loadavg: $(sysctl -n vm.loadavg)"
    ;;
*)
    echo "usage: $0 golden|gpu"; exit 2 ;;
esac
