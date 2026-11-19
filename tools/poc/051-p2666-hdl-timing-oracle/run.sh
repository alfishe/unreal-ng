#!/usr/bin/env bash
# Build the oracle and run every program; traces in scratch/2026-11-11-poc-051/traces,
# golden files (program + trace) in testdata/machines/p2666/timing when --golden is given.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WORK="$ROOT/scratch/2026-11-11-poc-051"
SRC="$WORK/src"
[ -d "$SRC/.git" ] || "$HERE/fetch.sh"
mkdir -p "$WORK/build" "$WORK/traces" "$WORK/programs"
cd "$WORK/build"
T80="$SRC/common/T80"
FLAGS=(--std=08 -fsynopsys -frelaxed --workdir="$WORK/build")
for f in T80_Pack.vhd T80_ALU.VHD T80_Reg.vhd T80_MCode.vhd T80.VHD T80a.vhd; do
    ghdl -a "${FLAGS[@]}" "$T80/$f"
done
ghdl -a "${FLAGS[@]}" "$HERE/tb/p2666_timing_tb.vhd"
ghdl -e "${FLAGS[@]}" p2666_timing_tb

python3 -I "$HERE/programs.py" "$WORK/programs"
for bin in "$WORK"/programs/*.bin; do
    name="$(basename "$bin" .bin)"
    frames="$(cat "$WORK/programs/$name.frames")"
    edges=$(( (frames + 1) * 2293760 ))
    ghdl -r "${FLAGS[@]}" p2666_timing_tb -gPROG="$bin" -gTRACE="$WORK/traces/$name.txt" \
        -gMAX_MAINCLK="$edges" --stop-time=$(( edges * 2 + 100 ))ns --ieee-asserts=disable >/dev/null
    echo "$name: $(grep -c '^M1' "$WORK/traces/$name.txt") fetches, $(grep -c '^INTA' "$WORK/traces/$name.txt") INTA"
    if [ "${1:-}" = "--golden" ]; then
        G="$ROOT/testdata/machines/p2666/timing"
        mkdir -p "$G"
        cp "$bin" "$G/$name.bin"
        cp "$WORK/traces/$name.txt" "$G/$name.trace"
    fi
done
