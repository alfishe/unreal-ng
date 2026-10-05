# TS-Conf video lines captured from the Verilog

Reference raster lines produced by [`tools/machines/tsconf/rtl-sim`](../../../../tools/machines/tsconf/rtl-sim/README.md),
a Verilator run of the real TS-Conf video Verilog. The test `ScreenTSConf_Test.GX1_GxOffsMatchesTheRtl` fills memory as
the harness does and compares the emulator's line with each of them, index for index.

| File | Lines | What |
|:--|--:|:--|
| `zx-gxoffs.txt` | 58 | ZX mode (`V_CONFIG` 00h, `V_PAGE` 05h) at `G_X_OFFS` 0..40, 64..68, 128..131, 255..257, 300, 508..511 |
| `txt-gxoffs.txt` | 116 | text mode at the same offsets, `V_CONFIG` 03h (256x192) and 83h (320x240), `V_PAGE` 10h |

Format (see the tool's README): `<V_CONFIG hex> <G_X_OFFS decimal> <pixels per dot> <index> ...` - window line 9,
8 border dots on each side, one 8-bit palette index per pixel. Regenerate with the tool's `all` command; the files
here are copies of its `results/`.
