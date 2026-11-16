#!/bin/zsh
# POC 050, the native analyzer against the Lua timer: exactness (the reference, sync, native on a 48K and a Pentagon,
# 200 frames), then the cost (none, sync, native interleaved, the demo's markers and the dense Plasma.cell).
# Run under launch.py: native.sh OUT ROUNDS FRAMES
here=${0:A:h}
out=$1 rounds=${2:-5} frames=${3:-1000}
for model in 48K PENTAGON; do
    python3 $here/run_taps.py --model $model --frames 200 --out $out/exact --variant ref --variant sync --variant native
    python3 $here/compare.py $out/exact/ref-$model.json $out/exact/sync-$model.json $out/exact/native-$model.json
done
VARIANTS="none sync native" EXTRA=Plasma.cell $here/bench.sh $out/bench $rounds $frames 48K
python3 $here/summary.py $out/bench
