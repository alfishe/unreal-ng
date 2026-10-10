#!/bin/bash
# usage: nexaudio.sh <file.nex> <frames> <out-dir>  -> ours.wav and jnext.wav (the final mix) in out-dir, then a spectrum summary
nex="$1"; F="${2:-300}"; out="$3"; mkdir -p "$out"
B=$(cd "$(dirname "$0")/../../.." && pwd)
J=/Volumes/TB4-4Tb/Projects/emulators/build/jnext/bld/jnext
IMG=/Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full.img
rm -f "$out/ours.wav" "$out/jnext.wav"
timeout 120 $J --headless --sdcard $IMG --sdcard-readonly --wav-record "$out/jnext.wav" --delayed-automatic-exit-frames $F "$nex" >/dev/null 2>&1
UNREAL_NEX="$nex" UNREAL_NEX_FRAMES=$F UNREAL_NEX_WAV="$out/ours.wav" $B/cmake-build-agent-release/bin/core-tests --gtest_filter='LoaderNexRun*' >/dev/null 2>&1
python3 "$(dirname "$0")/wavspectrum.py" "$out/ours.wav" "$out/jnext.wav"
