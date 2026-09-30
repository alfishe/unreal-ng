#!/bin/sh
# Builds the Verilator bench around the released BaseConf RTL (zclock/zmem/arbiter/video_sync_h/v).
# Usage: RTL=/path/to/pentevo/fpga/base_trdemu/trunk ./build.sh
# Run:   ./obj_dir/simtb rand turbo=2 cycles=300000 (with CSTATS=1 WITHIO=1)   - 14 MHz per-cycle wait table
#        PROBE=m1|mr|mw|int|io_fe|io_40ff|io_40fe|c000 ./obj_dir/simtb c_probe turbo=0 raster=2 cycles=300000
#        (RASTER128=1 ... raster=3 [p7ffd=N]) - 3.5 MHz contention delay per T offset from 14335/14361
set -e
cd "$(dirname "$0")"
RTL=${RTL:?set RTL to fpga/base_trdemu/trunk of a pentevo checkout}
mkdir -p rtl/z80 rtl/include rtl/dram rtl/video
for f in z80/zclock.v z80/zmem.v dram/arbiter.v video/video_sync_h.v video/video_sync_v.v include/tune.v; do
  LC_ALL=C tr -d '\r' < "$RTL/$f" > "rtl/$f"
done
verilator --cc --exe --build -j 8 -Wno-fatal -Wno-lint -Wno-style -Wno-COMBDLY -Wno-LATCH -Wno-MULTIDRIVEN -Wno-IMPLICIT \
  -DSIMULATE -Irtl -Irtl/z80 --top-module tb tb.v rtl/z80/zclock.v rtl/z80/zmem.v rtl/dram/arbiter.v \
  rtl/video/video_sync_h.v rtl/video/video_sync_v.v main.cpp -o simtb
