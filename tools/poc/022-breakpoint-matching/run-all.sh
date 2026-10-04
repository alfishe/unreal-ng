#!/bin/zsh
# PoC 022: run every experiment once, the way the published results were made (data/README.md).
# The dev machine is shared, so the numbers are relative: within one experiment the benchmarks run in random
# interleaved order, three repetitions each, and report.py takes the minimum.
#
# Usage (from the repository root):
#   tools/poc/022-breakpoint-matching/run-all.sh [build-dir] [traces-dir] [results-dir]
# Defaults: scratch/poc-022/build, scratch/poc-022/traces, scratch/poc-022/results
# Output: <results-dir>/<experiment>/<date>_<time> - full run/pass1.{json,txt}
set -u
BUILD=${1:-scratch/poc-022/build}
export POC022_TRACES=${2:-scratch/poc-022/traces}
RESULTS=${3:-scratch/poc-022/results}
STAMP=$(date +%Y-%m-%d_%H%M)
for e in 01-baseline:e01-baseline 02-exact-lookup:e02-exact-lookup 03-ranges:e03-ranges 04-physical:e04-physical \
         05-ports:e05-ports 06-combined:e06-combined; do
  dir=${e%%:*}; bin=${e##*:}
  OUT="$RESULTS/$dir/$STAMP - full run"
  mkdir -p "$OUT"
  echo "$(date +%T) $bin start (load $(sysctl -n vm.loadavg 2>/dev/null))"
  UNREAL_NICE=0 "$BUILD/$bin" --benchmark_repetitions=3 --benchmark_enable_random_interleaving=true \
    --benchmark_out="$OUT/pass1.json" --benchmark_out_format=json > "$OUT/pass1.txt" 2>&1
  echo "$(date +%T) $bin exit $?"
done
echo DONE
