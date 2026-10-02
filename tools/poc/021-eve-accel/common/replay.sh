#!/bin/sh
# Run one replay: replay.sh <variant> <boot|play|zuma> [eve-replay options...]
# Prints the 1/5/15-minute load before the run. Paths of the captures and the ROM can be
# overridden with EVE_CAPTURES (directory) and EVE_ROM.
HERE=$(cd "$(dirname "$0")/.." && pwd)
CAPTURES=${EVE_CAPTURES:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/scratch}
ROM=${EVE_ROM:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)/data/rom/ft81x.rom}
export EVE_ROM="$ROM"  # the GPU backends read the ROM fonts from it too
variant=$1
trace=$2
shift 2
case "$trace" in
    boot) file=$CAPTURES/rtype-boot.evr ;;
    play) file=$CAPTURES/rtype-play.evr ;;
    zuma) file=$CAPTURES/zuma-flick.evr ;;
    *) file=$trace ;;
esac
echo "loadavg: $(sysctl -n vm.loadavg)"
echo "run: eve-replay-$variant $(basename "$file") $*"
exec "$HERE/build/eve-replay-$variant" "$file" --rom "$ROM" "$@"
