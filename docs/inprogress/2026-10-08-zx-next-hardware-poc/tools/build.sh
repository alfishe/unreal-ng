#!/bin/sh
# Build the probe programs into ../out/*.nex (and .tap for the tape route).
# Needs z88dk (z88dk-z80asm). Z88DK_BIN may name its bin directory.
set -e
cd "$(dirname "$0")"
BIN="${Z88DK_BIN:-}"
ASM="${BIN:+$BIN/}z88dk-z80asm"
mkdir -p ../out
for p in h1timing h2dma h3sprites h4z80n; do
    "$ASM" -mz80n -b -I../src -o../out/$p.bin ../src/$p.asm
    python3 mknex.py ../out/$p.bin ../out/$p.nex
    echo "$p: $(wc -c < ../out/$p.bin) bytes of code -> ../out/$p.nex"
done
