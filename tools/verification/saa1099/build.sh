#!/bin/sh
# Build the co-simulation drivers into bin/ (git-ignored). Needs ./fetch-refs.sh
# first for the reference drivers; drv-ours builds from the core sources alone.
#   ./build.sh            all drivers
#   ./build.sh ours       one of: ours saasound mame mister
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../../.. && pwd)
CXX=${CXX:-c++}
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))
OPT="-std=c++20 -O2"
WARN="-Wall -Wextra"
mkdir -p bin build
WHAT=${1:-all}

want() { [ "$WHAT" = all ] || [ "$WHAT" = "$1" ]; }

if want ours; then
    $CXX $OPT $WARN -I"$ROOT/core/src" -I"$ROOT/core/src/3rdparty" -Idrivers \
        drivers/drv-ours.cpp drivers/logger-stub.cpp \
        "$ROOT/core/src/emulator/sound/chips/saa1099/saa1099.cpp" \
        "$ROOT/core/src/3rdparty/blip_buf/blip_buf.cpp" -o bin/drv-ours
    echo "built bin/drv-ours"
fi

need_ref() {
    if [ ! -e "refs/$1" ]; then
        echo "refs/$1 missing: run ./fetch-refs.sh first" >&2
        exit 1
    fi
}

if want saasound; then
    need_ref saasound
    S=refs/saasound/src
    # Third-party code: its warnings are not ours to fix (-w). private/protected opened
    # for instrumentation in every unit alike, so the class layouts agree
    $CXX $OPT -w -Dprivate=public -Dprotected=public -Irefs/saasound/include -I$S -Idrivers \
        drivers/drv-saasound.cpp $S/SAADevice.cpp $S/SAAFreq.cpp $S/SAANoise.cpp $S/SAAEnv.cpp $S/SAAAmp.cpp \
        -o bin/drv-saasound
    echo "built bin/drv-saasound"
fi

if want mame; then
    need_ref mame/saa1099.cpp
    $CXX $OPT -w -Dprivate=public -Dprotected=public -Idrivers/mame-shim -Irefs/mame -Idrivers \
        drivers/drv-mame.cpp refs/mame/saa1099.cpp -o bin/drv-mame
    echo "built bin/drv-mame"
fi

if want mister; then
    need_ref sam-coupe-mister/rtl/saa1099.sv
    verilator --cc --exe --build -j "$JOBS" --public-flat-rw -Wno-fatal -Wno-lint -Wno-style \
        --top-module saa1099 -Mdir build/mister -o ../../bin/drv-mister \
        -CFLAGS "-std=c++20 -O2 -I$(pwd)/drivers" \
        refs/sam-coupe-mister/rtl/saa1099.sv drivers/drv-mister.cpp > build/mister.log 2>&1 \
        || { tail -30 build/mister.log; exit 1; }
    echo "built bin/drv-mister"
fi
