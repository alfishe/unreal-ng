#!/bin/bash
# POC 028, third set: where in the line a write to a sprite fill-stage input (pattern byte, attribute, NR #4B) stops showing in the next
# line and starts showing in the one after. Scenarios 44 (attribute 0), 45 (attribute 3), 46 (pattern byte), 47 (NR #4B), each at eight
# positions of the line, the row / sprite chosen for the line 1 or 2 after the force. Needs the build of run-diff.sh (the std variant).
# Usage: run-sprite-fill.sh [out dir of run-diff.sh]
set -euo pipefail
OUT=${1:-scratch/2026-10-12-poc-028-diff}
cd "$OUT/std"
[ -f rf.txt ] || ./tb_zxnext -gg_scenario=47 -gg_ref=1 -gg_log=rf.txt -gg_events=ref.txt >/dev/null 2>&1 &
n=1
for s in ${SCENARIOS:-44 45 46 47}; do
  for a in 1 2; do
    for h in 20 60 100 140 180 220 260 300; do
      ./tb_zxnext -gg_scenario=$s -gg_h0=$h -gg_ahead=$a -gg_ref=0 -gg_log=f${s}_${a}_$h.txt -gg_events=fe${s}_${a}_$h.txt >/dev/null 2>&1 &
      n=$((n+1)); if (( n % 8 == 0 )); then wait; fi
    done
  done
done
wait
python3 - <<'PY'
import analyze
import os
for s in [int(x) for x in os.environ.get('SCENARIOS', '44 45 46 47').split()]:
    for a in (1, 2):
        row = []
        for h in (20, 60, 100, 140, 180, 220, 260, 300):
            r = analyze.diff("f%d_%d_%d.txt" % (s, a, h), "fe%d_%d_%d.txt" % (s, a, h), "rf.txt")
            hit = [f for k, l, hc, cf, f in r if f]
            row.append("h%d:%d" % (h, len(hit)))
        print("scenario %d ahead %d: %s" % (s, a, " ".join(row)))
PY
