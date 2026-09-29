#!/bin/bash
# SZX interop check: the files unreal-ng writes, read by libspectrum (the
# library Fuse uses), must match what libspectrum read from the reference
# files they were loaded from. Needs libspectrum (brew install libspectrum,
# apt install libspectrum-dev) and a built core-tests.
#
#   tools/verification/szx/check-interop.sh [build-dir]
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=${1:-"$ROOT/cmake-build-agent-release"}
OUT="$ROOT/scratch/szx-interop"
mkdir -p "$OUT"
# libspectrum.h includes gcrypt.h, whose path its .pc file does not carry
FLAGS="$(pkg-config --cflags --libs libspectrum 2>/dev/null || echo "-L/opt/homebrew/lib -lspectrum") \
$(pkg-config --cflags libgcrypt 2>/dev/null || libgcrypt-config --cflags 2>/dev/null || echo "-I/opt/homebrew/include")"
# shellcheck disable=SC2086
cc -std=c11 -Wall -Wextra -O1 -o "$OUT/szxtool" "$ROOT/tools/verification/szx/szxtool.c" $FLAGS
UNREALNG_SZX_EXPORT_DIR="$OUT" "$BUILD/bin/core-tests" \
    --gtest_filter='*/LoaderSZX_Test.ExportForTheLibspectrumCheck/*:*/LoaderZ80Models_Test.*' > "$OUT/export.log"
status=0
for ours in "$OUT"/synth-*.szx; do
    name=$(basename "$ours" .szx)
    reference="$ROOT/testdata/loaders/szx/libspectrum/$name.libspectrum.txt"
    # A machine fitted with a Beta 128 (48K, 128K, +2 in the shipped configs)
    # writes B128, which the libspectrum-made reference does not have
    if diff <("$OUT/szxtool" dump "$ours" | grep -v '^beta') <(grep -v '^beta' "$reference") > /dev/null; then
        echo "ok   $name"
    else
        echo "DIFF $name"
        diff <("$OUT/szxtool" dump "$ours") "$reference" || true
        status=1
    fi
done
# .z80 saved by unreal-ng from libspectrum's .z80 (testdata/loaders/z80/libspectrum):
# the same state again, except what .z80 cannot hold (MEMPTR, the frame
# position, the interrupt shadow and FSET) and the Beta 128 line
for ours in "$OUT"/synth-*.z80; do
    name=$(basename "$ours" .z80)
    reference="$ROOT/testdata/loaders/z80/libspectrum/$name.libspectrum.txt"
    filter='^beta|^iff1|^halted'
    if diff <("$OUT/szxtool" dump "$ours" | grep -Ev "$filter") <(grep -Ev "$filter" "$reference") > /dev/null; then
        echo "ok   $name.z80"
    else
        echo "DIFF $name.z80"
        diff <("$OUT/szxtool" dump "$ours") "$reference" || true
        status=1
    fi
done
exit $status
