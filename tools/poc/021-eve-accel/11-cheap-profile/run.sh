#!/bin/sh
# 11: the CHEAP profile - how little drawing still shows a picture that changes, and which
# shortcuts keep every chip answer exact.
#   11-cheap-profile/run.sh check   one run per variant with every check on -> out/check.txt
#   11-cheap-profile/run.sh time    CPU time per second of chip time, twice -> out/time.txt
HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
OUT=$HERE/out
mkdir -p "$OUT"
R=$POC/common/replay.sh

variants()
{
    # $1: play|zuma|boot, $2: extra options (--no-hash or empty)
    t=$1
    x=$2
    f=""
    [ "$t" = play ] && f="--frames 4000"
    echo "## full"; $R threads $t $f $x --stats
    echo "## skip unchanged"; EVE_POC_SKIP_UNCHANGED=1 $R threads $t $f $x --stats
    echo "## skip unchanged + read set"; EVE_POC_SKIP_UNCHANGED=1 EVE_POC_DEFER=1 $R threads $t $f $x --stats
    for n in 2 4 8; do
        echo "## draw every $n"; $R threads $t $f $x --draw-every $n --stats
    done
    for n in 2 4; do
        echo "## line step $n"; EVE_POC_LINE_STEP=$n $R threads $t $f $x --stats
    done
    echo "## skip unchanged + draw every 4 + line step 2"
    EVE_POC_SKIP_UNCHANGED=1 EVE_POC_DEFER=1 EVE_POC_LINE_STEP=2 $R threads $t $f $x --draw-every 4 --stats
    echo "## no drawing"; $R threads $t $f $x --no-draw --stats
}

case "$1" in
check)
    for t in play zuma boot; do variants $t ""; done 2>&1 | tee "$OUT/check.txt"
    ;;
time)
    for run in 1 2; do
        "$POC/common/wait-load.sh" 600 12
        for t in play zuma boot; do variants $t --no-hash; done
    done 2>&1 | tee "$OUT/time.txt"
    ;;
*)
    echo "usage: $0 check|time"; exit 2 ;;
esac
