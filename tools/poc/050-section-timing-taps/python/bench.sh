#!/bin/zsh
# POC 050: the cost runs. Interleaved rounds (the variant order rotates), the demo's markers and the demo's markers
# plus the dense one (Sparkles.inside). Run under launch.py: bench.sh OUT ROUNDS FRAMES MODEL
here=${0:A:h}
out=$1 rounds=${2:-5} frames=${3:-2000} model=${4:-48K}
variants=(${=VARIANTS:-none count sync frame deferred})
extra=${EXTRA:-Sparkles.inside}
for r in {1..$rounds}; do
    order=(${variants[@]:$((r - 1))} ${variants[@]:0:$((r - 1))})
    args=()
    for v in $order; do args+=(--variant $v); done
    python3 $here/run_taps.py --model $model --frames $frames --out $out --tag=_r$r $args
    python3 $here/run_taps.py --model $model --frames $frames --out $out --tag=_dense_r$r --extra $extra $args
done
