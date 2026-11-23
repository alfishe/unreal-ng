#!/bin/sh
# POC 054: interleaved rounds of every transport. Run from the repo's scratch/: run.sh BINARY ROUNDS
BIN=$1; ROUNDS=${2:-3}
uname -sm; uptime
for r in $(seq 1 "$ROUNDS"); do
  for m in fifo fifobig unix tcp; do
    "$BIN" $m 3840 2160 4 100
    "$BIN" $m 3840 2160 2 150
    "$BIN" $m 352 288 4 2000
  done
done
