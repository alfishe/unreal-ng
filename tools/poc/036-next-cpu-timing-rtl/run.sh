#!/bin/bash
# POC 036: build the testbench (the RTL patched by POC 028's patch_rtl.py) and run the test ROMs on the three timings.
# Usage: run.sh <cores/zxnext/src of the Next FPGA repository> [out dir (default scratch/2026-10-26-poc-036)] [program ...]
# Needs GHDL (VHDL-2008). One run (5 INTs) takes a few minutes.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?path of cores/zxnext/src}
OUT=${2:-scratch/2026-10-26-poc-036}
shift 2 || true
PROGS=${*:-mix mix5 mixir nreg5 nreg2 dmaburst dmacont zdmacont dmapaper nr08}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$OUT"
python3 "$HERE/gen.py" roms
if [ ! -x tb_cpu ] || [ "$HERE/tb_cpu.vhd" -nt tb_cpu ]; then
  python3 "$HERE/../028-next-video-pipeline-alignment/patch_rtl.py" "$SRC" "$OUT/rtl"
  python3 "$HERE/patch_probes.py" "$OUT/rtl"
  cp "$HERE/tb_cpu.vhd" .
  cp -r rtl/init .
  rm -rf work && mkdir work
  FLAGS="--std=08 -fsynopsys -frelaxed --workdir=work"
  ghdl -i $FLAGS $(cat rtl/files.txt) >/dev/null 2>&1
  ghdl -a $FLAGS rtl/tbh.vhd rtl/tbp.vhd
  ghdl -m $FLAGS zxnext >/dev/null 2>&1
  ghdl -a $FLAGS tb_cpu.vhd
  ghdl -e $FLAGS tb_cpu
fi
# NR #03: bit 7 = write the timing (bits 6:4), bits 2:0 the machine type: 48K #91, 128K #A2, +3 #B3
for p in $PROGS; do
  for m in 48:145 128:162 p3:179; do
    ./tb_cpu -gg_nr03=${m#*:} -gg_rom=roms/$p.hex -gg_log=rtl-$p-${m%%:*}.txt >/dev/null 2>&1 &
  done
  wait
done
