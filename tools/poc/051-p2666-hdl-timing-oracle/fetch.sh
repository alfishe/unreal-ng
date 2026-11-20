#!/usr/bin/env bash
# Clone the Pentagon 2.666 LE sources at the pinned revision into the POC's scratch folder.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEST="$ROOT/scratch/2026-11-11-poc-051/src"
REV=e808402ff193491b94cfd3733f3e1f045c2aca6c
if [ ! -d "$DEST/.git" ]; then
    mkdir -p "$(dirname "$DEST")"
    git clone --quiet https://github.com/koe1234/pentagon_2_666_le.git "$DEST"
fi
git -C "$DEST" checkout --quiet "$REV"
echo "$DEST at $(git -C "$DEST" rev-parse --short HEAD)"
