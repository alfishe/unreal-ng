#!/usr/bin/env bash
# PlaneScaler A/B: core/recording/src/common/framescaler.{h,cpp} at a git revision (A) against the working tree (B).
#   ab.sh [REV] [ROUNDS]     REV: default HEAD; ROUNDS: default 6
# Builds bench and exact for both with c++ -O2 (the build uses -O2 / -O3) in <repo>/scratch/recording-planescaler,
# checks that both write the same bytes, then runs the benches interleaved, the order swapped every round.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(git -C "$here" rev-parse --show-toplevel)"
rev="${1:-HEAD}"
rounds="${2:-6}"
out="$repo/scratch/recording-planescaler"
src=core/recording/src/common
mkdir -p "$out/a" "$out/b"
git -C "$repo" show "$rev:$src/framescaler.h" > "$out/a/framescaler.h"
git -C "$repo" show "$rev:$src/framescaler.cpp" > "$out/a/framescaler.cpp"
cp "$repo/$src/framescaler.h" "$repo/$src/framescaler.cpp" "$out/b/"
for v in a b; do
  c++ -std=c++20 -O2 -I"$out/$v" "$here/bench.cpp" "$out/$v/framescaler.cpp" -o "$out/bench-$v" -lpthread
  c++ -std=c++20 -O2 -I"$out/$v" "$here/exact.cpp" "$out/$v/framescaler.cpp" -o "$out/exact-$v"
  "$out/exact-$v" > "$out/exact-$v.bin"
done
if cmp -s "$out/exact-a.bin" "$out/exact-b.bin"; then
  echo "EXACT: same bytes ($(wc -c < "$out/exact-b.bin" | tr -d ' ') bytes)"
else
  echo "EXACT: the bytes differ"
  exit 1
fi
for r in $(seq 1 "$rounds"); do
  if (( r % 2 )); then order="a b"; else order="b a"; fi
  for v in $order; do
    echo "== r$r $v"
    "$out/bench-$v"
  done
done
