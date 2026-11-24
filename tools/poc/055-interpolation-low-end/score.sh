#!/bin/zsh
# POC 055: the suite's 12 scenes (POC 047) for some filters of a lite binary, all scenes at once, then the table.
#   score.sh <binary> <filter,...> <work dir> <dir with the scenes' <scene>.rgba sources>
# The sources are what a first full run of 047's suite.py leaves beside its results.
set -eu
here=${0:A:h}
bin=${1:A}; filters=$2; dir=$3; sources=${4:A}
scenes=(strips vertical diagonal overlay textlines parallax sprites cadence dotmatrix thin flux animated)
mkdir -p $dir
for sc in $scenes; do
  ln -sf $sources/$sc.rgba $dir/$sc.rgba
  (python3 $here/../047-smoothing-filters/suite.py $bin $filters $dir $sc > $dir/$sc.txt 2>&1 &)
done
until [ $(grep -l "mean over the elements" $dir/*.txt 2>/dev/null | wc -l) -eq ${#scenes} ]; do sleep 15; done
for sc in $scenes; do tail -1 $dir/$sc.txt; done |
  awk '{ printf "%-10s", $1; for (i = 2; i <= NF; i++) { sum[i] += $i; printf " %7s", $i }; print "" }
       END { printf "%-10s", "sum"; for (i = 2; i <= NF; i++) printf " %7.2f", sum[i]; print "" }'
