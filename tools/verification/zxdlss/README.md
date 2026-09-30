# zxdlss - ZX DLSS algorithm module and tools

C++ implementation of the ZX DLSS GigaScreen de-flicker algorithms, with a tool
that renders TTD recordings or exported clips through them. Phase 2 of POC 019
([tools/poc/019-zxdlss-gigascreen](../../poc/019-zxdlss-gigascreen/README.md)).

| Part | What it is |
|---|---|
| `algo/include/zxdlss/algorithm.h` | the interface every algorithm implements (`delay()`, `process(frame) -> RGB`), the registry (`createAlgorithm(name)`) |
| `algo/src/mod_tpgw.cpp` | `mod-tpgw`, optimized (NEON / SSE2 kernels in `simd.h`, threads); `mod-tpgwa` = the same + the scene stage (spec section 7.8); `mod-tpgwaf` + flash veto, periods 2..4, detail-based scene trigger; `mod-tpgwafs` + step-aware scene render; `mod-tpgwafsd` + no border field seeds on stripe tiles (the baseline) |
| `algo/src/mod_tpgw_ref.cpp` | `mod-tpgw-ref`, the literal scalar implementation of the spec |
| `algo/src/registry.cpp` | registry, plane B decoding, `raw` (no processing) |
| `tool/main.cpp` | `zxdlss-render`: TTD file or clip -> algorithm -> video / exact RGB dump |
| `tool/bench.cpp` | `zxdlss-bench`: per-frame and per-stage cost |
| `scripts/compare_dump.py` | frame-exact comparison of two dumps |
| `scripts/parity.sh` | C++ vs the Python reference on every golden scene |

The algorithm is specified in
[algorithm-mod-tpgw.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/algorithm-mod-tpgw.md).
How the C++ prototype was built, verified and optimized, round by round with
all measurements:
[cpp-prototype-walkthrough.md](../../../docs/inprogress/2026-09-27-zxdlss-gigascreen/cpp-prototype-walkthrough.md).

## Build

```
cmake -S . -B build -DBUILD_ZXDLSS_TOOLS=ON
ninja -C build zxdlss-render zxdlss-bench
```

With the emulator core in the build (the default root build) `zxdlss-render`
also reads TTD files; the machine configs and ROMs are copied next to the binary.

## Render

```
# a TTD session, raw left / processed right, 2x, H.264 + AAC sound (ffmpeg on PATH)
zxdlss-render --ttd flickering_test.ttd --layout raw-out --video flicker.mp4

# a range of an exported clip (POST /ttd/export-clip with the zxdlss feature on)
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --video spiral.mp4

# the exact output, for comparing implementations
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --dump out/dump

zxdlss-render --list          # registered algorithms

# the current baseline, with the per-detector usage and the scene-stage runs
zxdlss-render --ttd across_the_edge_full.ttd --alg mod-tpgwafsd --layout raw-out --video ate.mp4 --stats

# Pentagon overscan: 352 x 304, the paper centered horizontally (the emulator's
# Symmetric Horizontal viewport) - demos that draw into the extra border lines
zxdlss-render --ttd across_the_edge_full.ttd --overscan --alg mod-tpgwafsd --layout raw-out --video ate-osc.mp4
```

The machine comes from the file: the tool reads the session's recorded machine
from its headers (`ttd::ReadTTDFileInfo`, the same as `ttd info <path>`) and
creates the emulator of that model with that General Sound card fitted before
the load - the loader refuses another card (the Pentagon model now fits NeoGS;
Across the Edge was recorded with the classic GS). `--model` is optional: when
given it must name the recorded model. The frame range also comes from the
header, without an emulator.
Frames are output for `[--from, --to]`; the input runs `delay()` frames further
(6 for mod-tpgw), so the last 6 frames of a recording have no output.

### Sound (TTD input)

A TTD render carries the machine's sound by default (`--no-audio` for a silent
video; `--audio FILE.wav` also keeps the WAV). The sound is rendered first, in
one continuous run from `--from` (the frame-by-frame picture walk restarts the
sound path at every checkpoint, which clicks at frame boundaries). The video is
written at the machine's frame rate, measured from the sound: 44100 x frames /
samples, i.e. 48.83 fps on a Pentagon (903.2 samples per frame), so picture and
sound stay in sync over the whole file.

The sound ends up inside the mp4 (an AAC track next to the H.264 video). On the
way it passes through a temporary WAV next to the video (`<video>.wav`, removed
once ffmpeg exits unless `--audio` names it): the whole sound is ready before the
first picture (its own continuous pass), and ffmpeg's stdin carries the RGB
frames, so the sound comes in as a second input file (`-i <video>.wav -map 0:v
-map 1:a -c:a aac -shortest`). A named pipe fed by a writer thread would avoid
the file (POSIX only; Windows would need its own pipe) - not done, the file is
small (~60 MB for 6 minutes) and short-lived.

The run steps with `Emulator::RunFrame` (it keeps the position inside the frame;
`RunNFrames(1)` drifts by the last instruction's overrun and after ~10 000
frames gave one step two frames of sound). The journaled input of the session
is played back by the run. A recording can still hold outside changes it did
not journal, and the run then leaves the recording - Across the Edge's session
(2026-09-28): the TR-DOS autostart hook rewrote `RUN "boot"` into
`RUN "ACROSS"` in RAM, and the disk image is not stored in the session, so a
run from the start boots another program and stays silent. Every 25 frames a
second emulator seeks to the same position and compares RAM and PC / SP; on a
mismatch the run is put back onto the recording (one discontinuity in the
sound) and the frame is printed (`resynced at frame ...`). Clip input
(`--clip`) has no sound and stays at 50 fps.

## Add an algorithm

One `.cpp` in `algo/src/` implementing `zxdlss::Algorithm`, registered with

```cpp
const Registration kRegistration("my-alg", [] { return std::make_unique<MyAlgorithm>(); });
```

and listed in `CMakeLists.txt` (`zxdlss_algo`). The tools pick it up by name.

## Verify

```
# optimized vs the scalar reference (C++ only, fast)
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --alg mod-tpgw-ref --dump /tmp/ref
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --alg mod-tpgw     --dump /tmp/opt
python3 scripts/compare_dump.py /tmp/ref /tmp/opt --tolerance 0 --max-share 0

# C++ vs the Python reference on all nine golden scenes
scripts/parity.sh <poc dir> <poc data dir> <build bin> <scratch dir>
```

Status (2026-09-29): `mod-tpgw` matches the Python reference bit for bit on all
nine golden scenes (3 513 frames), and the TTD path matches the clip path.
`mod-tpgwa` matches the Python reference on the DJ scene (201 frames) and equals
`mod-tpgw` bit for bit on the other nine scenes (its scene stage stays off there).
`mod-tpgw-ref` has no scene stage.

## Performance

`zxdlss-bench --clip data/clip_v2 --from 12100 --to 12300` (the irregular spiral:
the two-page field render runs on most frames - the heaviest scene), Apple M1
Ultra, Release:

| Implementation | ms/frame |
|---|---|
| Python reference (NumPy) | ~300-400 |
| `mod-tpgw-ref` (scalar C++) | ~170 |
| `mod-tpgw`, 1 thread (`ZXDLSS_THREADS=1`), NEON | 5.9 |
| `mod-tpgw`, 8 threads, NEON | 2.85 |

A 50 Hz frame is 20 ms. Stages (1 thread): translation + masks 1.5 ms, pixel
stage 1.2 ms, two-page render 2.4 ms, the rest < 1 ms.
