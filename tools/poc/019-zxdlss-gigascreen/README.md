# 019 - ZX DLSS GigaScreen

Show what GigaScreen software intended - mixed colors - instead of flicker,
while everything that is not part of color mixing stays exactly as sharp as
the raw emulator output. This POC develops the analysis and mixing algorithm
on real material before it goes into the emulator core.

**Status (2026-09-29):** phase 1 (Python POC) done - the final algorithm
`mod-tpgw` is specified in
[algorithm-mod-tpgw.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md);
phase 2 (C++) moves to `tools/verification/zxdlss`.

## Goals

1. **Phase 1 - Python POC.** Detect flicker patterns per 8x1 segment and per
   region (periods 2-5, irregular flicker, border, moving objects over
   flickering backgrounds, full-screen moving flicker), build masks, mix with
   selectable mixers, and measure the result on real frames. Fast iteration,
   side-by-side video output, metrics.
2. **Phase 2 - C++ prototype**, here in the POC: the same algorithm as a
   standalone library, verified frame-exact against the Python reference,
   benchmarked (scalar reference, then SSE/NEON; `SIMD-CANDIDATE` tags).
3. **Phase 3 - final design and integration** into unreal-ng as the `dlss`
   mode of the Temporal Effects Manager.

The requirements and the target design already exist - this POC is where
they get validated and adjusted:

| Document | Content |
|---|---|
| [requirements.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/requirements.md) | R-1..R-30 |
| [design-analysis.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/design-analysis.md) | case catalogue C0-C9 / B0-B4, segment classifier, motion, border, stability, composition |
| [design-mixers.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/design-mixers.md) | mixer store, formula language, calibration |
| [test-plan.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/test-plan.md) | clip format, generators, quality gates, goldens |
| [temporal-effects-manager.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/temporal-effects-manager.md) | where the result lives in the emulator |
| [optimization-ideas.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/optimization-ideas.md) | backlog + SIMD candidates |
| [prior-art.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/prior-art.md) | other emulators' de-flicker |
| [reference-across-the-edge.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/reference-across-the-edge.md) | the reference recording and its effect map |

## Reference material

*Across the Edge* by Demarche - every case of the catalogue occurs in it.

- Disk: [testdata/loaders/trd/across_the_edge_by_demarche.trd](../../../testdata/loaders/trd/across_the_edge_by_demarche.trd)
- Storyboard (parts, frame ranges, flicker per part): [docs/disasm/demo/across-the-edge/storyboard.md](../../../docs/disasm/demo/across-the-edge/storyboard.md)
- Border synchronization disassembly: [docs/disasm/demo/across-the-edge/README.md](../../../docs/disasm/demo/across-the-edge/README.md)

Recordings and clips are **not committed** (84 MB TTD, 13 MB clip). They are
regenerated into `data/` (git-ignored) - see [walkthrough.md](walkthrough.md).

## Layout

```
019-zxdlss-gigascreen/
├── README.md             # this file
├── walkthrough.md        # how the material was produced, step by step, pitfalls
├── requirements.txt      # numpy, pillow, zstandard
├── common/
│   ├── webapi.py         # minimal unreal-ng WebAPI client
│   └── clip.py           # lossless clip format: writer + reader
├── capture/
│   ├── record_ttd.py     # record a whole program run into a .ttd
│   ├── extract_clip.py   # TTD -> clip (every frame's final beam-rendered picture)
│   ├── verify_stepping.py# frame step == direct seek, no drift, border stripes present
│   └── add_palette16.py  # v2 clips exported before the core wrote "palette16": recover it into clip.json
├── analysis/
│   ├── effect_map.py     # per-pixel period indicators, segments, thumbnails, table
│   └── contact_sheet.py  # thumbnails of every Nth frame
├── python/               # phase 1 POC
│   ├── dlss_v1.py, dlss_v2.py   # algorithm versions (v2 current)
│   ├── mixers.py         # linear-mean (default), srgb-mean
│   ├── video.py          # side-by-side video writer (ffmpeg)
│   ├── run.py            # one clip range -> video + metrics
│   └── run_set.py        # all golden clip candidates
├── results.md            # per-version metrics and findings
├── data/                 # (ignored) .ttd recordings, clips
└── out/                  # (ignored) generated reports, videos
```

## Quick start

```bash
cd tools/poc/019-zxdlss-gigascreen
pip3 install -r requirements.txt

# 1. emulator with WebAPI (build from the repo root first)
../../../cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &

# 2. record the demo (real time, ~6 minutes)
python3 capture/record_ttd.py --disk ../../../testdata/loaders/trd/across_the_edge_by_demarche.trd \
    --out data/across_the_edge_full.ttd --min-frame 15500 --max-frame 17500

# 3. extract every frame (~10 minutes), check stepping first
python3 capture/extract_clip.py --ttd data/across_the_edge_full.ttd --out data/clip_full

# 4. effect map
python3 analysis/effect_map.py data/clip_full --out out/effect_map
```

### The palette

There is no ZX palette table in the POC. A core-exported (v2) clip carries the 16
colors its plane B color indices were drawn in - the emulator's live palette -
as `palette16` in `clip.json`, and `ClipV2.zx_palette` serves it to the mixers,
the detectors' luma and the oracles; the C++ algorithms get the same from the
emulator with every frame. A v2 clip exported before the core wrote it gets it
once with `python3 capture/add_palette16.py data/clip_v2`, which recovers it from
the clip's own RGBA frames.

## Golden clip candidates

| Clip | Frames | Why |
|---|---|---|
| `ate-pageflip-static` | 2700–2900 | page-flip picture, nearly static |
| `ate-hiphop-border` | 4600–4800 | page-flip picture + striped border flicker |
| `ate-border-only` | 5300–5500 | border flicker, no page flips |
| `ate-balls-floor` | 8000–8200 | moving objects over a flickering floor |
| `ate-tunnel` | 10000–10200 | full-screen moving flicker (worst case) |
| `ate-irregular` | 12100–12300 | irregular page flipping (C3) + moving stripe |
| `ate-raster-negative` | 3500–3700, 7300–7500 | pure motion - output must equal raw |

Frame numbers refer to the recording described in the walkthrough (session
starts at frame 25). A new recording can shift them slightly; the effect map
shows the new ranges.

## Results

- Reference material: [walkthrough.md](walkthrough.md) §7.
- Algorithm: [results.md](results.md) - v2 leaves pure motion untouched
  (no ghosting), removes static page-flip and border flicker; moving flicker
  needs region-level motion handling (v3).
