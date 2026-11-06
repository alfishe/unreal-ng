#!/bin/zsh
# Scores POC 047 smoothing filters on the synthetic scenes against the scenes' own truth (README.md).
#
#   truth.sh <filters bin> [filters] [scenes...]     (filters: comma list, default off,blend,layers)
#
# Work files go to $TRUTH_DIR (default scratch/smoothmotion-truth in the repo; ~2 GB per scene, kept between runs:
# the truth is made once per scene, the renders every run). Prints pursuit / fixation per scene and filter.
# TRUTH_TAG=<name>: the renders and scores of this run apart (variants of one filter side by side).
set -e
here=${0:A:h}
root=${here:h:h:h:h}
bin=${1:?filters binary (tools/poc/047-smoothing-filters/run.sh builds it)}; shift
F=${1:-off,blend,layers}; (( $# )) && shift
S=($@); (( $#S )) || S=(strips vertical diagonal overlay textlines parallax sprites cadence dotmatrix thin flux animated)
D=${TRUTH_DIR:-$root/scratch/smoothmotion-truth}
O=$D/scores${TRUTH_TAG:+-$TRUTH_TAG}
mkdir -p $O
[[ $D/truthscore -nt $here/truthscore.cpp ]] || clang++ -std=c++20 -O2 -Wall -Wextra $here/truthscore.cpp -o $D/truthscore
# Scenes whose content changes shape between frames: a second truth set (frame k + 1's shapes), the better counts
aliased=(animated flux)
for s in $S; do
  (
    sets=(0)
    (( ${aliased[(Ie)$s]} )) && sets+=(1)
    for a in $sets; do
      n=$s; (( a )) && n=$s.a$a
      [[ -f $D/$n.truth ]] || python3 $here/truthdump.py $D $s $a
      [[ -f $D/$n-truth60.rgba ]] || python3 $here/truth60.py $D $s $a
    done
    r=$D/$s${TRUTH_TAG:+-$TRUTH_TAG}
    mkdir -p $r
    FILTERS_RAW=1 FILTERS_ONLY=$F $bin $D/$s.rgba 352 288 300 $r $root/data/rom/48.rom 50 60 >/dev/null 2>&1
    R=($D/$s-truth60.rgba $D/$s.rgba@50@0)
    for f in ${(s:,:)F}; do R+=($r/$f.rgba); done
    files=()
    for a in $sets; do
      n=$s; (( a )) && n=$s.a$a
      TRUTH_ELEMENTS=1 TRUTH_REF=$D/$n-truth60.rgba $D/truthscore $D $n $R > $O/$n.txt
      files+=($O/$n.txt)
    done
    python3 $here/aliasmin.py $s $files > $O/$s.min
  ) &
done
wait
python3 $here/table.py $F $O/${^S}.min
