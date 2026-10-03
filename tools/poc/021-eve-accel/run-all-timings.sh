#!/bin/sh
# Every timing of the experiments, in sequence (each waits up to 10 minutes for the
# 1-minute load to drop to 12, then measures anyway and records the load).
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1
./01-baseline/run.sh
./01-baseline/run.sh profile
./03-line-parallel-cpu/run.sh scale
./04c-hybrid-change-points/run.sh time
./04b-line-per-dispatch/run.sh
./04-metal-compute/run.sh metal
./05-opengl/run.sh
./06-gpu-aa-256/run.sh gpu
./08-hardware-timing-model/run.sh
./11-cheap-profile/run.sh check
./11-cheap-profile/run.sh time
echo "all timings done: $(date)"
