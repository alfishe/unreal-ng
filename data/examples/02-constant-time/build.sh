#!/bin/sh
# Rebuild constant.sna / .sym (with the two timing bugs) and constant-fixed.sna / .sym (corrected) from
# constant.asm. Needs sjasmplus 1.20+: on PATH, or its path in SJASMPLUS.
set -e
cd "$(dirname "$0")"
"${SJASMPLUS:-sjasmplus}" --nologo --sym=constant.sym constant.asm
"${SJASMPLUS:-sjasmplus}" --nologo -DFIXED --sym=constant-fixed.sym constant.asm
