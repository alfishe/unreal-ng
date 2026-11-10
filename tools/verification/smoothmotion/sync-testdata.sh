#!/bin/bash
# Copies the smooth-motion test programs from their own repo (zx-smooth-motion-tests: the Z80 sources, the build,
# the docs) into testdata/video/smoothmotion: the built snapshots (bin/smooth-*.sna) and the truth files
# (truth/*.json). Only the .sna files: the tools load snapshots; the repo's .tap / .trd hold the same code.
#
#   sync-testdata.sh <path to zx-smooth-motion-tests>
#
# Checks the copied snapshots against the repo's bin/SHA256SUMS and writes bin/SHA256SUMS here (the .sna lines).
# Build the programs in their repo first (its build.sh), then sync, then re-run run_scenes.sh --update.
set -euo pipefail
cd "$(dirname "$0")/../../.."
SRC=${1:?path to zx-smooth-motion-tests}
[ -f "$SRC/bin/SHA256SUMS" ] || { echo "not a built zx-smooth-motion-tests: $SRC (no bin/SHA256SUMS)" >&2; exit 1; }
DST=testdata/video/smoothmotion
mkdir -p $DST/bin $DST/truth
rm -f $DST/bin/smooth-*.sna $DST/truth/*.json
cp "$SRC"/bin/smooth-*.sna $DST/bin/
cp "$SRC"/truth/*.json $DST/truth/
grep '\.sna$' "$SRC/bin/SHA256SUMS" > $DST/bin/SHA256SUMS
(cd $DST/bin && shasum -a 256 -c --quiet SHA256SUMS)
ls -l $DST/bin $DST/truth
