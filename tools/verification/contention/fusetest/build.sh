#!/bin/sh
# Assemble FUSE's "fusetest" timing test program into a tape file (.tap),
# the same way its own Makefile does:  pasmo --alocal --tapbas fusetest.asm fusetest.tap
#
# This script downloads nothing. Point it at a copy of the source and an assembler:
#   FUSETEST_SRC  folder with fusetest.asm and its include files (required)
#   PASMO         path to the pasmo 0.5.5 binary (default: "pasmo" on PATH)
#   OUT           output tape (default: fusetest.tap next to this script)
#
# See README.md in this folder for where to get both.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
: "${FUSETEST_SRC:?set FUSETEST_SRC to the fusetest source folder}"
PASMO=${PASMO:-pasmo}
OUT=${OUT:-$here/fusetest.tap}

if [ ! -f "$FUSETEST_SRC/fusetest.asm" ]; then
    echo "error: $FUSETEST_SRC/fusetest.asm not found" >&2
    exit 1
fi

# pasmo names the tape blocks after the output file (path included), so assemble to a bare
# "fusetest.tap" in a temporary folder, exactly as the Makefile would, then copy it out.
src=$(cd "$FUSETEST_SRC" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
(cd "$tmp" && "$PASMO" --alocal -I "$src" --tapbas "$src/fusetest.asm" fusetest.tap)
cp "$tmp/fusetest.tap" "$OUT"
echo "built $OUT"
