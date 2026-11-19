#!/bin/zsh
# POC 050: master against the patched gaps, interleaved per round (the host is shared: both configurations see the
# same load). Configurations: master (GC step per call, no HALT idle with exec taps) and patched (generational GC, no
# step per call; HALT idle unless a tap is at the HALT). ab.sh UNREAL_QT OUT ROUNDS FRAMES
here=${0:A:h}
binary=$1 out=$2 rounds=${3:-7} frames=${4:-1000}
for r in {1..$rounds}; do
    configs=(master patched)
    (( r % 2 == 0 )) && configs=(patched master)
    for c in $configs; do
        env=(UNREAL_POC050_HOOK_GC=step)
        [[ $c == patched ]] && env=(UNREAL_POC050_HOOK_GC=gen UNREAL_POC050_TAP_IDLE=1)
        mkdir -p $out/$c
        env $env VARIANTS="none count sync" EXTRA=Plasma.cell python3 $here/launch.py $binary 28700 \
            $here/bench.sh $out/$c/r$r 1 $frames 48K >> $out/$c/log.txt 2>&1
    done
done
