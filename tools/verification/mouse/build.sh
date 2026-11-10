#!/usr/bin/env bash
# Builds build/cursor.sna (48K snapshot, starts at #8000) from cursor.asm with sjasmplus.
# sjasmplus: on PATH, or SJASMPLUS=/path/to/sjasmplus (https://github.com/z00m128/sjasmplus)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build"
SJASMPLUS="${SJASMPLUS:-$(command -v sjasmplus || true)}"
if [[ -z "$SJASMPLUS" || ! -x "$SJASMPLUS" ]]; then
    echo "sjasmplus not found: put it on PATH or set SJASMPLUS=/path/to/sjasmplus" >&2
    exit 1
fi

mkdir -p "$BUILD"
cd "$BUILD"
"$SJASMPLUS" --nologo "$HERE/cursor.asm"
echo "built $BUILD/cursor.sna"
