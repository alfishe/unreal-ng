#!/usr/bin/env bash
# Regenerates the RTL-derived test data that core-tests freezes (multisoundlogic_test.cpp):
#   testdata/sound/multisound/scenarios/*.expected        RTL records of every scenario
#   testdata/sound/multisound/traces/*.rtl                RTL record hashes of every real-program trace (CL-2)
#   core/tests/emulator/slots/cards/multisound/multisoundrtltables.h   decode sweep + GS map sweep
# Every scenario and the sweep are also checked against MultiSoundLogic; any difference fails the run and nothing
# is overwritten for that scenario.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SIM="$HERE/build/mscosim"
[[ -x "$SIM" ]] || "$HERE/build.sh"

status=0
for scenario in "$ROOT"/testdata/sound/multisound/scenarios/*.msc; do
    expected="${scenario%.msc}.expected"
    if "$SIM" run "$scenario" --expect "$expected.tmp"; then
        mv "$expected.tmp" "$expected"
    else
        rm -f "$expected.tmp"
        status=1
    fi
done

# Real-program traces: the captured card configuration, plus Ball Quest on the issue #11 firmware ("classic" control
# mask, which compares five bits only while the SAA is off)
trace() {
    local name="$1" out="$2"; shift 2
    if "$SIM" trace "$ROOT/testdata/sound/multisound/traces/$name.msc.zst" "$out.tmp" "$@"; then
        mv "$out.tmp" "$out"
    else
        rm -f "$out.tmp"
        status=1
    fi
}
for packed in "$ROOT"/testdata/sound/multisound/traces/*.msc.zst; do
    name="$(basename "$packed" .msc.zst)"
    trace "$name" "${packed%.msc.zst}.rtl"
done
trace ballquest "$ROOT/testdata/sound/multisound/traces/ballquest-classic.rtl" --mask classic --dip ym,gs,sd

if [[ "${1:-}" != "--scenarios-only" ]]; then
    tables="$ROOT/core/tests/emulator/slots/cards/multisound/multisoundrtltables.h"
    if nice -n 10 "$SIM" sweep "$tables.tmp"; then
        mv "$tables.tmp" "$tables"
    else
        rm -f "$tables.tmp"
        status=1
    fi
fi
exit $status
