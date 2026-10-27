#!/usr/bin/env bash
# Ratchet on unprotected instance reads in the Qt frontend (BUGS.md #11, design in
# docs/inprogress/2026-10-10-gui-instance-lifetime). Counts in unreal-qt/src:
#   getcontext  Emulator::GetContext() calls outside EmulatorBinding (GUI code gets the instance from the binding);
#               a call through a shared_ptr<Emulator> the class holds is marked 'lifetime-ok: <why>' on its line
#   members     stored Emulator* / EmulatorContext* members (a raw pointer outlives a removal); a member marked
#               'lifetime-ok: <why>' on its line is a reviewed exception
# Fails when a count is above tools/verification/gui-instance-reads.baseline; says so when one is below, so the
# baseline goes down with each migration step and never up.
#
# Usage: tools/verification/gui-instance-reads.sh            check
#        tools/verification/gui-instance-reads.sh --update   write the current counts as the baseline
#        tools/verification/gui-instance-reads.sh --list     print every counted site
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
BASELINE=tools/verification/gui-instance-reads.baseline
SRC=unreal-qt/src
BINDING='^unreal-qt/src/emulator/emulatorbinding\.(h|cpp):'

getcontext_sites()
{
    grep -rnE 'GetContext\(\)' "$SRC" | grep -E '\.(cpp|h):' | grep -vE "$BINDING" | grep -v 'lifetime-ok:'
}
member_sites()
{
    grep -rnE '^[[:space:]]+(const[[:space:]]+)?(Emulator|EmulatorContext)[[:space:]]*\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(=|;|\{)' "$SRC" \
        | grep -E '\.h:' | grep -v 'lifetime-ok:'
}

if [ "${1:-}" = "--list" ]; then
    echo "== getcontext"; getcontext_sites
    echo "== members"; member_sites
    exit 0
fi

getcontext=$(getcontext_sites | wc -l | tr -d ' ')
members=$(member_sites | wc -l | tr -d ' ')

if [ "${1:-}" = "--update" ]; then
    printf 'getcontext=%s\nmembers=%s\n' "$getcontext" "$members" > "$BASELINE"
    echo "baseline: getcontext=$getcontext members=$members"
    exit 0
fi

[ -f "$BASELINE" ] || { echo "no baseline: $BASELINE (run with --update)"; exit 2; }
status=0
for name in getcontext members; do
    current=${!name}
    allowed=$(grep "^$name=" "$BASELINE" | cut -d= -f2)
    if [ "$current" -gt "$allowed" ]; then
        echo "FAIL $name: $current > baseline $allowed. New GUI code must get the instance from EmulatorBinding"
        echo "     and must not store Emulator* / EmulatorContext* (see --list)"
        status=1
    elif [ "$current" -lt "$allowed" ]; then
        echo "ok   $name: $current < baseline $allowed - lower the baseline (--update) in this change"
    else
        echo "ok   $name: $current"
    fi
done
exit $status
