#!/usr/bin/env bash
# Builds build/mscosim: the four Verilated variants of the card's CPLD (pro / classic control mask x 1 MB / 2 MB GS
# RAM) linked with MultiSoundLogic and the shared scenario helper. Needs Verilator (5.x) and refs/ (fetch-rtl.sh).
# Runs in the machine-wide build slot at half the logical cores (AGENTS.md).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="$HERE/build"
VERILATOR="${VERILATOR:-$(command -v verilator || echo /opt/homebrew/bin/verilator)}"
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))
CXX="${CXX:-c++}"

[[ -f "$HERE/refs/top-pro.v" ]] || "$HERE/fetch-rtl.sh"
VROOT="$("$VERILATOR" --getenv VERILATOR_ROOT)"

build() {
    mkdir -p "$BUILD"
    for variant in pro1m pro2m classic1m classic2m; do
        local mask="${variant%[12]m}"
        local defines=()
        [[ "$variant" == *2m ]] && defines=(+define+GS_RAM_2MB)
        "$VERILATOR" --cc --build -j "$JOBS" -O3 --x-assign fast --x-initial fast \
            --public-flat-rw --pins-inout-enables -Wno-fatal -Wno-lint -Wno-style \
            --top-module zx_multisound --prefix "Vms_$variant" -Mdir "$BUILD/$variant" \
            -CFLAGS "-O2" "${defines[@]}" "$HERE/refs/top-$mask.v" > "$BUILD/$variant.log" 2>&1 \
            || { cat "$BUILD/$variant.log"; exit 1; }
    done
    "$CXX" -std=c++20 -O2 -Wall -Wextra \
        -isystem "$VROOT/include" -isystem "$VROOT/include/vltstd" \
        -isystem "$BUILD/pro1m" -isystem "$BUILD/pro2m" -isystem "$BUILD/classic1m" -isystem "$BUILD/classic2m" \
        -I"$ROOT/core/src" -I"$ROOT/core/tests" \
        "$HERE/tb/mscosim.cpp" \
        "$ROOT/core/src/emulator/slots/cards/multisound/multisoundlogic.cpp" \
        "$ROOT/core/tests/_helpers/multisoundscenario.cpp" \
        "$BUILD"/pro1m/Vms_pro1m__ALL.a "$BUILD"/pro2m/Vms_pro2m__ALL.a \
        "$BUILD"/classic1m/Vms_classic1m__ALL.a "$BUILD"/classic2m/Vms_classic2m__ALL.a \
        "$BUILD/pro1m/libverilated.a" -lpthread -o "$BUILD/mscosim"
    echo "built $BUILD/mscosim"
}

if [[ "${MSCOSIM_IN_SLOT:-}" == 1 || ! -x "$ROOT/tools/build/slot.sh" ]]; then
    build
else
    MSCOSIM_IN_SLOT=1 exec "$ROOT/tools/build/slot.sh" build -- nice -n 10 "$0" "$@"
fi
