#!/bin/bash
# Assembles the smooth-motion test scenes (src/smooth-*.a80) into smooth-<scene>.sna / .tap / .trd here.
# sjasmplus 1.20+ (SJASMPLUS=<path> or on PATH).
set -e
cd "$(dirname "$0")"
SJ=${SJASMPLUS:-sjasmplus}
for f in src/smooth-*.a80; do
    "$SJ" --nologo --msg=war -Isrc "$f"
done
ls -l smooth-*.sna smooth-*.tap smooth-*.trd
