#!/bin/sh
# Rebuild demo.sna and demo.sym from demo.asm. Needs sjasmplus 1.20+ with Lua (the default build):
# on PATH, or its path in SJASMPLUS.
set -e
cd "$(dirname "$0")"
"${SJASMPLUS:-sjasmplus}" --nologo --sym=demo.sym demo.asm
