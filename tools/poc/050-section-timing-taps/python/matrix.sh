#!/bin/zsh
# POC 050: the gap experiments. For each hook GC mode (step: master, none, gen) and HALT idle with exec taps (0:
# master, 1: patched), the cost rounds of bench.sh plus one exactness run of `sync` against the reference.
# matrix.sh UNREAL_QT OUT ROUNDS FRAMES
here=${0:A:h}
binary=$1 out=$2 rounds=${3:-3} frames=${4:-500}
for gc in step none gen; do
    for idle in 0 1; do
        dir=$out/gc-$gc-idle-$idle
        mkdir -p $dir
        env=(UNREAL_POC050_HOOK_GC=$gc)
        [[ $idle == 1 ]] && env+=(UNREAL_POC050_TAP_IDLE=1)
        variants="none count sync-empty sync deferred"
        [[ $gc != step ]] && variants="$variants sync-grow"
        env $env VARIANTS=$variants EXTRA=Plasma.cell python3 $here/launch.py $binary 28700 \
            $here/bench.sh $dir $rounds $frames 48K > $dir/log.txt 2>&1
        env $env python3 $here/launch.py $binary 28700 python3 $here/run_taps.py --model 48K --frames 200 \
            --out $dir/exact --variant sync >> $dir/log.txt 2>&1
        echo "== gc $gc idle $idle"
        python3 $here/compare.py $out/../r1/ref-48K.json $dir/exact/sync-48K.json
    done
done
