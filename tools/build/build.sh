#!/usr/bin/env bash
# Build through the machine-wide "build" pool: at most 2 builds at once across all agents,
# half the logical cores per build, lowered priority.
#
#   tools/build/build.sh [ninja targets...]          # default: everything (the pre-commit build)
#   BUILD_DIR=other-dir tools/build/build.sh core-tests
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
[ -d "$BUILD_DIR" ] || { echo "build.sh: $BUILD_DIR does not exist; configure first (see AGENTS.md)" >&2; exit 66; }
exec "$HERE/slot.sh" build -- ninja -C "$BUILD_DIR" -j "$JOBS" "$@"
