#!/bin/sh
# Configure and build every variant (Release, NEON) with half the logical cores.
#   tools/poc/021-eve-accel/build.sh [ninja targets...]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 ))
[ "$JOBS" -lt 1 ] && JOBS=1
cmake -S "$HERE" -B "$HERE/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
ninja -C "$HERE/build" -j "$JOBS" "$@"
