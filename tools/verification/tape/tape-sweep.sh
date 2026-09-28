#!/usr/bin/env bash
# Tape loading sweep: every tape in testdata/loaders (tap, tzx) on 48K and Pentagon, with fast loading
# on and off, loaded with LOAD "" the way a person does it, prompt keys pressed, then a liveness check.
# Headless (core-tests), no running emulator needed. The cases exist only while UNREAL_TAPE_SWEEP is
# set, so a normal core-tests run never sees them.
#
# Usage: tools/verification/tape/tape-sweep.sh [build-dir] [extra gtest filter, e.g. '*EMELYANOV*']
# Report: scratch/tape-sweep-report.txt (one line per case: verdict, tape state, cursor, frames)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="${1:-$ROOT/cmake-build-agent-release}"
FILTER="${2:-*}"
REPORT="$ROOT/scratch/tape-sweep-report.txt"

cmake --build "$BUILD" --target core-tests
mkdir -p "$ROOT/scratch/tape-sweep-screens"
rm -f "$REPORT" "$ROOT"/scratch/tape-sweep-screens/*.scr

status=0
UNREAL_TAPE_SWEEP=1 "$BUILD/bin/core-tests" --gtest_filter="Tapes/TapeLoadingSweep_Test.LoadsAndRuns/$FILTER" \
    > "$ROOT/scratch/tape-sweep.log" 2>&1 || status=$?

sort "$REPORT"
echo

# Contact sheet of the final screens (needs Python 3 with Pillow)
python3 - "$ROOT/scratch/tape-sweep-screens" "$ROOT/scratch/tape-sweep-sheet.png" <<'PY' || echo "contact sheet skipped (python3 + Pillow needed)"
import glob, os, sys
from PIL import Image, ImageDraw
files = sorted(glob.glob(os.path.join(sys.argv[1], "*.scr")))
pal = [(0,0,0),(0,0,215),(215,0,0),(215,0,215),(0,215,0),(0,215,215),(215,215,0),(215,215,215)]
cols = 4
sheet = Image.new("RGB", (cols * 256, ((len(files) + cols - 1) // cols) * 206), (40, 40, 40))
for n, f in enumerate(files):
    d = open(f, "rb").read()
    im = Image.new("RGB", (256, 192))
    for y in range(192):
        for x in range(256):
            bit = (d[((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | (x >> 3)] >> (7 - (x & 7))) & 1
            a = d[6144 + (y >> 3) * 32 + (x >> 3)]
            im.putpixel((x, y), pal[(a & 7) if bit else ((a >> 3) & 7)])
    sheet.paste(im, ((n % cols) * 256, (n // cols) * 206 + 14))
    ImageDraw.Draw(sheet).text(((n % cols) * 256 + 2, (n // cols) * 206), os.path.basename(f)[:-4], fill=(255, 255, 255))
sheet.save(sys.argv[2])
print("contact sheet:", sys.argv[2])
PY
echo
grep -E "^\[  (PASSED|FAILED)  \]" "$ROOT/scratch/tape-sweep.log" | sort -u || true
exit $status
