#!/bin/sh
# 04: snapshots -> op lists -> CPU check and Metal, for the three captures.
#   04-metal-compute/run.sh snap     write the snapshots (out/snap-<trace>/, ~4 MB each)
#   04-metal-compute/run.sh check    CPU evaluation of the op lists against eve-emu's pictures
#   04-metal-compute/run.sh metal    Metal, one dispatch per frame (and --per-op for 07)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
export EVE_ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
case "$1" in
snap)
    mkdir -p "$OUT/snap-play" "$OUT/snap-boot" "$OUT/snap-zuma"
    EVE_POC_SNAP_STEP=80 "$POC/common/replay.sh" threads play --no-hash --frames 4000 --snap "$OUT/snap-play" 0 1000000 --stats > "$OUT/snap-play.log" 2>&1
    EVE_POC_SNAP_STEP=20 "$POC/common/replay.sh" threads boot --no-hash --snap "$OUT/snap-boot" 0 1000000 --stats > "$OUT/snap-boot.log" 2>&1
    EVE_POC_SNAP_STEP=16 "$POC/common/replay.sh" threads zuma --no-hash --snap "$OUT/snap-zuma" 0 1000000 --stats > "$OUT/snap-zuma.log" 2>&1
    ;;
check)
    for t in play boot zuma; do
        "$POC/build/snap-check" "$OUT"/snap-$t/*.snap > "$OUT/snap-check-$t.txt" 2>&1 || true
        echo "== $t"; tail -4 "$OUT/snap-check-$t.txt"
    done
    ;;
metal)
    echo "loadavg: $(sysctl -n vm.loadavg)"
    "$POC/build/metal-render" --overhead --repeat 1 > "$OUT/metal-overhead.txt" 2>&1 || true
    cat "$OUT/metal-overhead.txt"
    for t in play boot zuma; do
        "$POC/build/metal-render" --repeat 10 "$OUT"/snap-$t/*.snap > "$OUT/metal-frame-$t.txt" 2>&1 || true
        "$POC/build/metal-render" --repeat 10 --per-op "$OUT"/snap-$t/*.snap > "$OUT/metal-per-op-$t.txt" 2>&1 || true
        echo "== $t"; tail -2 "$OUT/metal-frame-$t.txt"; tail -2 "$OUT/metal-per-op-$t.txt"
    done
    echo "loadavg: $(sysctl -n vm.loadavg)"
    ;;
*)
    echo "usage: $0 snap|check|metal"; exit 2 ;;
esac
