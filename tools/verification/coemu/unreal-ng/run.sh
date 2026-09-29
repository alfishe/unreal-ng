#!/usr/bin/env bash
# Co-emulation runner: unreal-ng (see ../README.md for the contract).
#   UNREAL_BUILD=<unreal-ng build directory with core-tests> ./run.sh [machine...]
# The test suite's CoEmu_Test loads the program as a user would and writes the dumps.
. "$(dirname "$0")/../common/common.sh"
coemu_init unreal-ng "$@"

BUILD=${UNREAL_BUILD:-$COEMU_ROOT/cmake-build-agent-release}
TESTS=$BUILD/bin/core-tests
if [ ! -x "$TESTS" ]; then
	coemu_not_found "no core-tests in $BUILD: set UNREAL_BUILD to a build directory configured with -DTESTS=ON and built (ninja core-tests)"
fi

for m in $MACHINES; do rm -f "$OUT/$m.bin" "$OUT/$m.skip"; done
COEMU_OUT=$OUT COEMU_PROGRAM=$PROGRAM COEMU_MACHINES="$MACHINES" COEMU_MAX_FRAMES=$MAX_FRAMES \
	"$TESTS" --gtest_filter='CoEmu_Test.Run' > "$OUT/core-tests.log" 2>&1

for m in $MACHINES; do
	if [ -f "$OUT/$m.skip" ]; then
		coemu_result "$m" skipped "$(head -1 "$OUT/$m.skip")"
	elif [ -s "$OUT/$m.bin" ]; then
		coemu_compare "$m"
	else
		coemu_result "$m" error "$(head -1 "$OUT/$m.log" 2>/dev/null || echo 'no result, see core-tests.log')"
	fi
done
coemu_finish
