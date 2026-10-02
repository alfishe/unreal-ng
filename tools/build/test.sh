#!/usr/bin/env bash
# Run core-tests through the machine-wide "test" pool: one test run at a time.
#
#   tools/build/test.sh                     # build core-tests (build pool), then test-parallel (test pool)
#   tools/build/test.sh --gtest_filter=X.*  # build core-tests, then run the binary with those arguments
#
# The two phases take their slots one after the other, never nested, so a queued test run
# does not hold a build slot. Details: tools/build/README.md
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-$ROOT/cmake-build-agent-release}
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)
JOBS=${UNREAL_JOBS:-$(( CORES / 2 ))}
[ "$JOBS" -ge 1 ] || JOBS=1

# Without this the test binary can be stale (plain ninja never rebuilds it), see AGENTS.md.
"$HERE/build.sh" core-tests || exit $?

if [ $# -gt 0 ]; then
  exec "$HERE/slot.sh" test -- "$BUILD_DIR/bin/core-tests" "$@"
fi
exec "$HERE/slot.sh" test -- cmake --build "$BUILD_DIR" --target test-parallel -- -j "$JOBS"
