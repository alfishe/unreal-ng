#!/usr/bin/env bash
# Build through the machine-wide "build" pool: at most 2 builds at once across all agents,
# half the logical cores per build, lowered priority.
#
#   tools/build/build.sh [ninja targets...]          # default: everything (the pre-commit build)
#   BUILD_DIR=other-dir tools/build/build.sh core-tests
#
# It re-runs the CMake configure step first (a few seconds): sources are globbed, so a file added by
# a merge/rebase is invisible to ninja until CMake runs again, and the link fails on missing symbols.
# A missing build directory is configured from scratch (with -DTESTS=ON, which test.sh needs).
# UNREAL_NO_CONFIGURE=1 skips the step.
#
# Run it in the foreground and wait for it to end; if both slots are busy it queues
# (and prints that it does). Details: tools/build/README.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-$ROOT/cmake-build-agent-release}
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)
JOBS=${UNREAL_JOBS:-$(( CORES / 2 ))}
[ "$JOBS" -ge 1 ] || JOBS=1

# The project's own CMake prints a very long source list: keep it in a log, show it only on failure
CONFIGURE=true
if [ "${UNREAL_NO_CONFIGURE:-0}" != 1 ]; then
  ARGS=""
  [ -f "$BUILD_DIR/CMakeCache.txt" ] || ARGS="-G Ninja -DTESTS=ON"
  CONFIGURE='mkdir -p "$BUILD_DIR" && { cmake -S "$ROOT" -B "$BUILD_DIR" '$ARGS' > "$BUILD_DIR/configure.log" 2>&1 || { cat "$BUILD_DIR/configure.log"; false; }; }'
fi
export ROOT BUILD_DIR JOBS
exec "$HERE/slot.sh" build -- bash -c "$CONFIGURE && exec ninja -C \"\$BUILD_DIR\" -j \"\$JOBS\" \"\$@\"" build.sh "$@"
