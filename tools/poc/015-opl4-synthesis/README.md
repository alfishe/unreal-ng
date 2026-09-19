# 015 — libopl4: YMF278B (OPL4) synthesis library PoC

Standalone implementation of the chip library specified in
[2026-09-13-0217-opl4-core-tdd.md](../../docs/inprogress/2026-09-13-moonsound/2026-09-13-0217-opl4-core-tdd.md)
(Revision 1, 2026-09-13). No dependency beyond the C++ standard library; no
allocation on the audio path after `configure()`; deterministic and fully
serialisable chip state.

Host integration (port decoding, mixer, TTD tiers) is **out of scope** here —
see `2026-09-13-0217-opl4-unreal-ng-integration.md` and `2026-09-13-0217-opl4-ttd-integration-tdd.md`.

## Layout

```
015-opl4-synthesis/
├── include/opl4/       # public API headers
│   ├── opl4config.h    # Opl4Config, RenderMode, Quality enums
│   ├── iwavememory.h   # wave-memory abstraction (§6)
│   ├── wavememory.h    # default ROM+SRAM implementation
│   └── opl4.h          # public Opl4 class (§10)
├── src/
│   ├── opl4tables.h    # silicon tables: power, pan, mix scale, rates
│   ├── opl4fm.h/.cpp   # FM engine (OPL3-class, 49516.4 Hz grid)
│   ├── opl4pcm.h/.cpp  # PCM engine (24 slots, 44100 Hz grid)
│   ├── opl4render.h/.cpp # render layer (§8): resampler, analog, chain, DC
│   └── opl4.cpp        # top level: time model, bus, reducer, mix, state
├── tests/
│   ├── testfw.h        # test framework (CHECK, chip fixtures/helpers)
│   ├── opl4tests.cpp   # unit + DSP tests (§12.1) + main()
│   └── opl4vectors.cpp # golden/behaviour vectors (§12.1/12.2)
└── cosim/              # co-simulation harnesses (see cosim/README.md)
    ├── cosim-ymfm.cpp  # differential vs ymfm ymf278b (pinned ref)
    ├── cosim-oracle.cpp # golden self-oracle (committed digests)
    ├── cosimdrv.h      # shared driver + register scripts
    ├── fetch-refs.sh   # fetch pinned ymfm into refs/ (gitignored)
    └── golden/         # oracle digests + generate.py
```

## Building and running

Fully standalone — one compiler invocation, no third-party code:

```bash
cd tools/poc/015-opl4-synthesis
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude -Isrc \
    src/*.cpp tests/opl4tests.cpp tests/opl4vectors.cpp -o bin/opl4tests
./bin/opl4tests   # 1404 checks, 0 failures
```

Or through CMake (standalone, inside the PoC folder):

```bash
cmake -S . -B build -G Ninja && ninja -C build && ./bin/opl4tests
```

Co-simulation harnesses are opt-in (`-DCOSIM=ON`, one network fetch of the
pinned ymfm revision): see [cosim/README.md](cosim/README.md).

Or through the project build (opt-in, like every PoC):

```bash
cmake -S . -B cmake-build-release -G Ninja -DBUILD_POC=ON
ninja -C cmake-build-release opl4tests
./tools/poc/015-opl4-synthesis/bin/opl4tests
```

## What is implemented (spec cross-reference)

| Spec item | Where |
|---|---|
| D1 two clock domains (÷684 FM, ÷768 output) | `Opl4::syncTo` in `opl4.cpp` |
| D2/D13 `HoldDrop` reducer, `Authentic`/`HiFi` | `Opl4::advanceOutput` |
| D4/D5 10-bit attenuation, silicon power table, separate clipping | `src/opl4tables.h`, PCM slot loop |
| D6 TL interpolation (27 / 13.5 cadence) | `PcmSlot::advanceTlInterp` |
| D7 tone header fetch rewriting banks 5–9 | `Opl4Pcm::writeReg` bank 0 |
| D8 16-entry 3 dB pan table | `kPanTable` |
| D9 block mix 0xF8/0xF9, reset 0x1B/0x00, `s_mix_scale` | `kMixScale` |
| D10 16-bit adder + clip at output pair | `Opl4::advanceOutput` |
| §3.3 BUSY/LD durations (56/88/28/38, LD 9600–10000) | `Opl4` timing fields |
| §5.1 register 0x02 `MA` bit + header base | `Opl4Pcm::writeReg` |
| §5.4 loop overrun (wrap by `end+loop`, not clamp) | PCM slot advance |
| §8.2 runtime Kaiser polyphase, unity bypass at 44100 | `Opl4Render` |
| §8.3 YAC513+LF347 board analog (RC pole + Sallen-Key) | `BoardAnalog` |
| §8.4 punch/room presets with rate normalisation | `CharacterChain` |
| §8.6 DC blocker | `Opl4Render` |
| §9 determinism: POD `saveState`/`loadState`, no floats in chip state | `Opl4State` |
| §12.1/12.2 unit tests: tables, header decode, bypass, save-neutrality, restore | `tests/` |
| §12.2/12.3/12.6 cosim: ymfm differential (6 scenarios) + golden self-oracle (15 digests) | `cosim/` |

Known PoC-level simplifications (documented deviations, all chip-visible
behaviour kept): FM rhythm-mode noise uses a compact LFSR model; music-corpus
VGM replay (§12.3 corpora), HDL co-simulation (§12.4) and hardware recordings
remain future work — the co-simulation tier status (including openMSX) is
documented in [cosim/README.md](cosim/README.md).
