# Profile-guided optimisation (PGO)

The release packages are built with profile-guided optimisation. The compiler gets a profile of a real run, so it can
lay out the hot paths of `unreal-qt` (the CPU cores, the renderers, the sound chips, the VDAC4 ESP32) and inline them.
It also moves code the run never reached out of their way.

| Part | What it does |
|------|--------------|
| [`cmake/Pgo.cmake`](../../cmake/Pgo.cmake) | `-DUNREAL_PGO=GENERATE` instruments the Release build; `USE` builds with the profile; `UNREAL_PGO_DIR` (default `<build>/pgo`) holds the profile |
| [`train.py`](train.py) | the training run: drives the instrumented `unreal-qt` headless through the workload, lets it exit cleanly, merges Clang's raw profile |
| [`.github/workflows/release.yml`](../../.github/workflows/release.yml) | every build job: configure `GENERATE`, build `unreal-qt`, `train.py`, reconfigure `USE`, build everything, package |

## Measured gain

The figures were measured with MSVC x86_64 on a Threadripper 3975WX (Zen 2) on 2026-11-19. Each row compares the
best of three interleaved rounds with LTO, with and without PGO.

| Workload | Without PGO | With PGO | Change |
|----------|-------------|----------|--------|
| DOOM on VDAC4, `unreal-qt`, JIT on; the profile from R-Type VDAC4 only (DOOM held out) | 5.06 ms/frame | 3.86 ms/frame | -21 % |
| Frame benchmark Pentagon | 448 us | 364 us | -19 % |
| Frame benchmark TS-Conf | 5614 us | 3939 us | -30 % |
| Frame benchmark Sprinter | 3160 us | 2102 us | -33 % |
| Frame benchmark Next | 10297 us | 7439 us | -28 % |

The pictures are byte-identical with and without PGO: PGO changes the layout and the inlining of the code, never
what the code computes.

## The workload

All of it comes from the repository, and the run is deterministic:

- every creatable model (`GET /api/v1/emulator/models`) boots for 300 frames;
- the TS-Conf sprite demo (`testdata/machines/tsconf/spg/sprites.spg`) and a TurboSound FM tune on the Pentagon
  (`testdata/sound/tsfm/tech_support.sna`) run for 600 frames each;
- DOOM on TS-Conf + VDAC4 (`testdata/vdac4`: `_DOOM.SPG`, `DOOM.ELF`, the shareware `DOOM1.WAD`) runs for 6600 frames
  with the ESP32 JIT. That covers loading, the title and the demo game. A further 200 frames run in the ESP32
  interpreter.

Code the training never reaches is still built: GCC keeps its normal optimisation (`-fprofile-partial-training`),
while MSVC and Clang optimise it for size. When a hot path lacks a workload, add one to `train.py` rather than
leave it to chance.

## How it ends: the profile is written at exit

Every toolchain writes the profile when the process exits normally: MSVC `.pgc`, GCC `.gcda`, Clang `.profraw`.
A killed process leaves none. `train.py` therefore starts `unreal-qt --headless --script wait.lua --exit-on-script`
with a script that waits for one line on stdin, and drives the machines through the WebAPI meanwhile. Closing stdin
ends the script, and the application quits through its own shutdown.

## Running it locally

```bash
cmake -S . -B scratch/build-pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DTESTS=OFF -DUNREAL_PGO=GENERATE
cmake --build scratch/build-pgo --target unreal-qt
python3 tools/pgo/train.py scratch/build-pgo/bin/unreal-qt.app/Contents/MacOS/unreal-qt \
    --profile-dir scratch/build-pgo/pgo          # Linux: bin/unreal-qt, Windows: bin/unreal-qt.exe
cmake -S . -B scratch/build-pgo -DUNREAL_PGO=USE
cmake --build scratch/build-pgo
```

- Use one build directory for both builds: GCC names its `.gcda` files after the object paths.
- MSVC needs LTO (the default), because the profile applies at the LTCG link. Run the instrumented binary in a
  developer shell, where `pgort140.dll` is on `PATH`.
- `train.py --quick` runs a tenth of the frames, for checking the script itself.
- The instrumented run is several times slower than a normal build. The full training takes a few minutes on
  the CI runners.
