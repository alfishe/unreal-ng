#!/usr/bin/env bash
# Provision the co-simulation cards outside the repository (firmware / OS files are not ours to commit).
#
#   provision-cards.sh <tbblue-distribution> [min-card-source]
#
#   <tbblue-distribution>  a checkout of the Next system distribution (gitlab.com/thesmog358/tbblue): TBBLUE.FW,
#                          machines/next/, nextzxos/, sys/, dot/, home/
#   [min-card-source]      the card folder our firmware test uses (TBBLUE.FW, machines/next/{config.ini,menu.def,
#                          enNextZX.rom, enNxtmmc.rom, enNextMf.rom, keymap.bin}); default: the full card's subset
#
# Creates $NEXT_COSIM_CARDS/full (default /Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full) and .../min.
# machines/next/config.ini ("timing=0") is not in the distribution; it is written here.
set -euo pipefail
dist=${1:?tbblue distribution folder}
cards=${NEXT_COSIM_CARDS:-/Volumes/TB4-4Tb/Projects/emulators/cosim-cards}
[ -f "$dist/TBBLUE.FW" ] || { echo "$dist has no TBBLUE.FW" >&2; exit 1; }

mkdir -p "$cards/full"
for d in machines/next nextzxos sys dot home; do
    mkdir -p "$cards/full/$d"
    rsync -a "$dist/$d/" "$cards/full/$d/"
done
cp "$dist/TBBLUE.FW" "$cards/full/"
printf 'timing=0\n' >"$cards/full/machines/next/config.ini"

mkdir -p "$cards/min/machines/next"
if [ -n "${2:-}" ]; then
    rsync -a "$2/" "$cards/min/"
else
    cp "$dist/TBBLUE.FW" "$cards/min/"
    for f in menu.def enNextZX.rom enNxtmmc.rom enNextMf.rom keymap.bin config.ini; do
        cp "$cards/full/machines/next/$f" "$cards/min/machines/next/$f"
    done
fi
du -sh "$cards/full" "$cards/min"
