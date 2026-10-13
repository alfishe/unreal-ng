#!/bin/bash
# POC 028, second set: the differential scenarios 28-53 (LoRes scroll, sprites, palette values, tilemap control bits).
# Each scenario is run twice, the second time with the value never changed (g_ref=1): the first output sample that differs after the
# force is the register's latency (analyze.py diff). Scenarios 28-29 need a bank 5 whose bytes all differ (POC_PATTERN=lores), 35 and 38-43 a dense one (POC_PATTERN=dense).
# Usage: run-diff.sh <cores/zxnext/src> [out dir (default scratch/2026-10-12-poc-028-diff)]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?path of cores/zxnext/src}
OUT=${2:-scratch/2026-10-12-poc-028-diff}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FLAGS="--std=08 -fsynopsys -frelaxed"
build() {   # build <variant dir> <POC_PATTERN>
  local d=$OUT/$1
  mkdir -p "$d" && cd "$d"
  POC_PATTERN=$2 python3 "$HERE/patch_rtl.py" "$SRC" "$d/rtl" >/dev/null
  cp "$HERE/tb_zxnext.vhd" "$HERE/analyze.py" .
  cp -r rtl/init .
  rm -rf work && mkdir work
  ghdl -i $FLAGS --workdir=work $(cat rtl/files.txt) >/dev/null 2>&1
  ghdl -a $FLAGS --workdir=work rtl/tbh.vhd
  ghdl -m $FLAGS --workdir=work zxnext >/dev/null 2>&1
  ghdl -a $FLAGS --workdir=work tb_zxnext.vhd
  ghdl -e $FLAGS --workdir=work tb_zxnext
}
build std none &
build lores lores &
build dense dense &
wait
run() {   # run <variant> <scenario>
  local v=$1 s=$2 h=200 c=0
  case $s in 31) h=24;; esac
  [ "$s" = 36 ] && c=5
  cd "$OUT/$v"
  ./tb_zxnext -gg_scenario=$s -gg_h0=$h -gg_l2const=$c -gg_ref=0 -gg_log=d$s.txt -gg_events=de$s.txt >/dev/null 2>&1 &
  ./tb_zxnext -gg_scenario=$s -gg_h0=$h -gg_l2const=$c -gg_ref=1 -gg_log=r$s.txt -gg_events=re$s.txt >/dev/null 2>&1 &
}
variant() { case $1 in 28|29|49|50|51) echo lores;; 35|38|39|40|41|42|43|53) echo dense;; *) echo std;; esac; }
n=0
for s in $(seq ${FIRST:-28} ${LAST:-53}); do
  v=$(variant $s)
  run $v $s
  n=$((n+1)); if (( n % 4 == 0 )); then wait; fi
done
wait
for s in $(seq ${FIRST:-28} ${LAST:-53}); do
  v=$(variant $s)
  (cd "$OUT/$v" && python3 - <<PY
import analyze
r = analyze.diff("d$s.txt", "de$s.txt", "r$s.txt")
ds = [f[0] for k, l, hc, cf, f in r if f]
print("scenario %2d: n=%2d changed: min %s max %s clocks; first at %s" % ($s, len(ds), min(ds) if ds else None, max(ds) if ds else None,
      [(f[1], f[2]) for k, l, hc, cf, f in r if f][:3]))
PY
  )
done
