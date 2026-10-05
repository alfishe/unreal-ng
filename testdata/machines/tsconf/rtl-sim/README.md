# TS-Conf timing captured from the Verilog

Reference data produced by [`tools/machines/tsconf/rtl-sim`](../../../../tools/machines/tsconf/rtl-sim/README.md),
a Verilator run of the real TS-Conf Verilog: raster lines of the video, the TSU's latch timing, the CPU's DRAM
waits at 14 MHz, and the CPU cache's fill and retention. The test
`ScreenTSConf_Test.GX1_GxOffsMatchesTheRtl` fills memory as the harness does and compares the emulator's line with each
of the raster lines, index for index.

| File | Lines | What |
|:--|--:|:--|
| `zx-gxoffs.txt` | 58 | ZX mode (`V_CONFIG` 00h, `V_PAGE` 05h) at `G_X_OFFS` 0..40, 64..68, 128..131, 255..257, 300, 508..511 |
| `txt-gxoffs.txt` | 116 | text mode at the same offsets, `V_CONFIG` 03h (256x192) and 83h (320x240), `V_PAGE` 10h |
| `tsu-latch.txt` | 12 cases | TSU latch timing (`tsconf-video-sim tsulatch`): a busy TS line whose tile registers change between `ts_start` and `line_start`, with the objects of the pass and the TSU DRAM cycles before `line_start`. `TsConfEngine_Test.TSU9_ObjectsAfterLineStartTakeTheNewLatch` builds each case and compares the emulator's TSU line index for index |
| `cpu-waits.txt` | 1170 tests | CPU DRAM waits at 14 MHz (`tsconf-cpu-sim all`, pin delay 1): the 14 MHz tests of the tool's `results/cpu-waits.txt` (its 3.5 / 7 MHz tests have no waits and are left out). `TsConfArbiter_Test.ARB6_CpuWaitsMatchTheRtl` replays each through the emulated CPU's bus cycles and compares every machine cycle's length |
| `cache-retention.txt` | 10 cases | CPU cache fill and retention (`tsconf-cpu-sim cache`): reads with the cache off and on, DRAM changed behind the CPU (as a DMA write), CPU writes with the cache off and on, reset. `TsConfMemory_Test.CCH3_CacheFillAndRetentionMatchTheRtl` replays each and compares every read's byte and whether it took a DRAM cycle |

Line format (see the tool's README): `<V_CONFIG hex> <G_X_OFFS decimal> <pixels per dot> <index> ...` - window line 9,
8 border dots on each side, one 8-bit palette index per pixel. Regenerate with the tool's `all` command; the files
here are copies of its `results/`.

`cpu-waits.txt` format: the tool's README, section "Result format: cpu-waits.txt"; `tsu-latch.txt`: section "Result
format: tsu-latch.txt"; `cache-retention.txt`: section "Result format: cache-retention.txt".
