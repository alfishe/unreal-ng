#!/bin/zsh
# Frame-exact comparison of the C++ mod-tpgw with the Python reference on every
# golden scene (algorithm-mod-tpgw.md section 10.1).
#
#   scripts/parity.sh POC_DIR DATA_DIR BUILD_BIN OUT_DIR [ALG]
#     POC_DIR    tools/poc/019-zxdlss-gigascreen (the Python reference)
#     DATA_DIR   the POC's data folder with clip_v2, clip_flicker_v2
#     BUILD_BIN  directory holding zxdlss-render
#     OUT_DIR    where the dumps and logs go (a scratch folder)
#
# The Python dumps are reused when present (they do not change with the C++ code).
set -u
POC=$1; DATA=$2; BIN=$3; OUT=$4; ALG=${5:-mod-tpgw}
HERE=${0:A:h}
mkdir -p $OUT
scenes=(
  "ate-pageflip-static clip_v2 2700 2900"
  "ate-hiphop-border clip_v2 4600 4800"
  "ate-border-only clip_v2 5300 5500"
  "ate-balls-floor clip_v2 8000 8200"
  "ate-tunnel clip_v2 10000 10200"
  "ate-irregular clip_v2 12100 12300"
  "ate-raster-negative clip_v2 3500 3700"
  "ate-raster-negative-2 clip_v2 7300 7500"
  "flicker-test clip_flicker_v2 1501 3405"
)
# Python references, in parallel
for s in $scenes; do
  parts=(${=s})
  name=$parts[1]; clip=$parts[2]; a=$parts[3]; b=$parts[4]
  if [[ ! -f $OUT/py/$name/dump.json ]]; then
    (cd $POC && python3 python/run.py --clip $DATA/$clip --clip-v2 --alg mod-tpgw --from $a --to $b \
      --name $name --out $OUT/py_json --video none --dump $OUT/py/$name > $OUT/py_$name.log 2>&1) &
  fi
done
wait
fail=0
for s in $scenes; do
  parts=(${=s})
  name=$parts[1]; clip=$parts[2]; a=$parts[3]; b=$parts[4]
  $BIN/zxdlss-render --clip $DATA/$clip --from $a --to $b --alg $ALG --dump $OUT/cpp/$name --quiet > $OUT/cpp_$name.log 2>&1
  printf "%-22s " $name
  python3 $HERE/compare_dump.py $OUT/py/$name $OUT/cpp/$name || fail=1
done
exit $fail
