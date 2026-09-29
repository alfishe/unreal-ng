# zxdlss - ZX DLSS algorithm module and tools

C++ implementation of the ZX DLSS GigaScreen de-flicker algorithms, with a tool
that renders TTD recordings or exported clips through them. Phase 2 of POC 019
([tools/poc/019-zxdlss-gigascreen](../../poc/019-zxdlss-gigascreen/README.md)).

| Part | What it is |
|---|---|
| `algo/include/zxdlss/algorithm.h` | the interface every algorithm implements (`delay()`, `process(frame) -> RGB`), the registry (`createAlgorithm(name)`) |
| `algo/src/mod_tpgw.cpp` | `mod-tpgw`, optimized (NEON / SSE2 kernels in `simd.h`, threads) |
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
# a TTD session, raw left / processed right, 2x, 50 fps H.264 (ffmpeg on PATH)
zxdlss-render --ttd flickering_test.ttd --layout raw-out --video flicker.mp4

# a range of an exported clip (POST /ttd/export-clip with the zxdlss feature on)
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --video spiral.mp4

# the exact output, for comparing implementations
zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --dump out/dump

zxdlss-render --list          # registered algorithms
```

`--model` names the machine a TTD session was recorded on (default `PENTAGON`).
Frames are output for `[--from, --to]`; the input runs `delay()` frames further
(6 for mod-tpgw), so the last 6 frames of a recording have no output.

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
