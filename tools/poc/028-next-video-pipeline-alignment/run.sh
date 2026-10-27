#!/bin/bash
# POC 028: build the testbench and sweep the registers.
# Usage: run.sh <cores/zxnext/src of the Next FPGA repository> [out dir (default scratch/2026-10-12-poc-028)]
# Needs GHDL (the llvm / mcode build, VHDL-2008). Sweeps 27 scenarios x 32 clock offsets, a few minutes each.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?path of cores/zxnext/src}
OUT=${2:-scratch/2026-10-12-poc-028}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$OUT"
python3 "$HERE/patch_rtl.py" "$SRC" "$OUT/rtl"
cp "$HERE/tb_zxnext.vhd" "$HERE/analyze.py" .
cp -r rtl/init .
rm -rf work && mkdir work
FLAGS="--std=08 -fsynopsys -frelaxed --workdir=work"
ghdl -i $FLAGS $(cat rtl/files.txt) >/dev/null 2>&1
ghdl -a $FLAGS rtl/tbh.vhd
ghdl -m $FLAGS zxnext >/dev/null 2>&1
ghdl -a $FLAGS tb_zxnext.vhd
ghdl -e $FLAGS tb_zxnext
# scenario = the register changed (see tb_zxnext.vhd); 4, 8, 16 sweep near the line start so that the pixels left of the clip are still ahead
for s in $(seq 1 27); do
  h=200; case $s in 4|8|16) h=24;; esac
  ./tb_zxnext -gg_scenario=$s -gg_h0=$h -gg_log=s$s.txt -gg_events=e$s.txt >/dev/null 2>&1 &
  if (( s % 8 == 0 )); then wait; fi
done
wait
for s in $(seq 1 27); do
  python3 - <<PY
import analyze
r = analyze.analyze("s$s.txt", "e$s.txt")
ds = [f[0] - cf for k, hc, cf, f in r if f and f[0] - cf > 0]
print("scenario %2d: n=%2d first change after the force: min %s max %s clocks" % ($s, len(ds), min(ds) if ds else None, max(ds) if ds else None))
PY
done
