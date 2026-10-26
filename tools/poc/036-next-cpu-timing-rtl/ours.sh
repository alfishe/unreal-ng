#!/bin/bash
# POC 036: run the programs the RTL ran in this emulator (NextRtlTiming_Test) and diff the instruction durations.
# Usage: ours.sh <out dir of run.sh> <core-tests binary> <program>-<48|128|p3> ...
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$1" && pwd)
BIN=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
shift 2
cd "$OUT"
for run in "$@"; do
  p=${run%-*}; m=${run##*-}
  case $m in 48) nr=91;; 128) nr=A2;; p3) nr=B3;; esac
  case $p in mix5|nreg5) body=6000;; *) body=8100;; esac
  start=$(python3 "$HERE/compare.py" start "rtl-$run.txt" $body)
  UNREAL_NEXT_RTL_ROM=roms/$p.bin UNREAL_NEXT_RTL_TRACE=ours-$run.txt UNREAL_NEXT_RTL_NR03=$nr UNREAL_NEXT_RTL_START=$start \
    UNREAL_NEXT_RTL_FRAMES=3 "$BIN" --gtest_filter='NextRtlTiming_Test.TraceOfATestRom' > /dev/null 2>&1
  echo "== $run (start $start)"
  python3 "$HERE/dur.py" "rtl-$run.txt" "ours-$run.txt" $body 12
done
