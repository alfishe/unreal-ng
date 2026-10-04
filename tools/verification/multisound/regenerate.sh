#!/usr/bin/env bash
# Regenerates the RTL-derived test data that core-tests freezes (multisoundlogic_test.cpp):
#   testdata/sound/multisound/scenarios/*.expected        RTL records of every scenario
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
