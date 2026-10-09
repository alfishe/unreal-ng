# 023 steamdeck-client: Steam Deck client proofs of concept

The experiments of the [POC plan](../../../docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md) for the
native Steam Deck front-end ([design](../../../docs/inprogress/2026-10-08-steamdeck-client/README.md)).
There is one folder per experiment, `pNN-<name>/`, numbered as in the plan. Each folder's README holds
the goal, how to run it, and the measured results. The verdicts are copied into the plan's results table.

## Build, deploy, run

```bash
# 1. Build in the Steam Linux Runtime 4.0 SDK container (amd64, gcc 14, SDL3 3.4)
tools/poc/023-steamdeck-client/build.sh                       # every POC
tools/poc/023-steamdeck-client/build.sh p08-input-inspector   # one target

# 2. Copy to the Deck (SSH enabled in Desktop Mode)
DECK=deck@steamdeck.local tools/poc/023-steamdeck-client/deploy.sh

# 3. Fetch the logs back (~/steamdeck-poc-logs on the Deck -> build/deck-logs/)
DECK=deck@steamdeck.local tools/poc/023-steamdeck-client/deploy.sh --logs
```

- Build output stays inside this folder, in `build/` (git-ignored), one directory per variant. The
  experiments share one CMake tree with the core, so there is one build per variant, not one per
  experiment:

  | Directory | Built by | Content |
  |---|---|---|
  | `build/deck/` | `build.sh` | steamrt4 SDK, `-march=x86-64-v3`: the binaries for the Deck |
  | `build/container/` | `build.sh --march x86-64` | the same at baseline ISA, to smoke-run under Docker's amd64 emulation |
  | `build/macos/` | host CMake (below) | native macOS build, SDL3 from Homebrew |
  | `build/deck-logs/` | `deploy.sh --logs` | logs fetched back from the Deck |

  Each `bin/` has `configs/` and `rom/` staged next to the binaries.
- The root CMake option `BUILD_STEAMDECK_POC=ON` adds this folder. `build.sh` sets it together with
  `BUILD_QT_APPS=OFF ENABLE_AUTOMATION=OFF -march=x86-64-v3`.
- On Apple Silicon the amd64 container runs emulated. The first build of the core is slow; later
  builds are incremental. x86-64-v3 binaries do not run under Docker's amd64 emulation (no AVX2): use
  `--march x86-64` for a copy to smoke-run in the container.
- **Other hosts.** The POCs are SDL3 code, not Linux code. With SDL3 installed (e.g. Homebrew), configure
  once with `-DBUILD_STEAMDECK_POC=ON -DBUILD_QT_APPS=OFF -DENABLE_AUTOMATION=OFF -DTESTS=OFF` into its own
  build directory (`tools/poc/023-steamdeck-client/build/macos`), then
  `BUILD_DIR=tools/poc/023-steamdeck-client/build/macos tools/build/build.sh steamdeck-poc-all`. P-01 / P-03 / P-08 build
  and run on macOS (Metal); `--windowed` opens a window instead of going fullscreen. P-18 (logind) is
  Linux only and skipped elsewhere.

### Running on the Deck

| How | When |
|---|---|
| **Game Mode, non-Steam game.** In Desktop Mode, Steam → *Add a Non-Steam Game* → browse to `~/steamdeck-poc/<binary>`, put arguments in *Launch Options*, then switch to Game Mode and start it from the library | every measurement: this is the real environment (gamescope, Steam Input) |
| Game Mode, inside the runtime: the non-Steam game's target is `~/steamdeck-poc/run-in-slr.sh` and its Launch Options are `./<binary> [args]` (see [p19](p19-runtime-packaging/README.md)) | P-19 |
| Desktop Mode, terminal | quick checks only (KWin, not gamescope) |

Every POC writes its logs to `~/steamdeck-poc-logs/` (Game Mode has no visible stdout).
**View + Menu held** (or Esc) quits every POC.

## Shared code

| File | Content |
|---|---|
| [common/emuhost.h](common/emuhost.h) | the minimum a front-end needs from the core: create a model, load a file, poke a test program, SDL3 audio stream into the core's audio callback (DRC occupancy), zero-copy `CopyPresentedFramebuffer`, Kempston bits |
| [common/timinglog.h](common/timinglog.h) | CSV logs under `~/steamdeck-poc-logs/`, interval and sample statistics |
| [common/embed.cmake](common/embed.cmake) | embeds compiled SPIR-V shaders as C arrays (`steamdeck_poc_shaders()`, used from P-07) |

## Index

Status: **planned** · **built** (compiles; not yet measured on a Deck) · **measured** · verdict
**confirmed** / **changed the design** / **blocked**. "Gate" means a failure changes the architecture.

| ID | Folder | Question | Gate | Status |
|----|--------|----------|------|--------|
| P-01 | [p01-sdl-gpu-latency](p01-sdl-gpu-latency/README.md) | SDL_GPU render path latency, core-clocked | ✓ | built |
| P-02 | `p02-vulkan-present-wait` | raw Vulkan + `present_wait`, only if P-01 fails | | planned |
| P-03 | [p03-vblank-pacing](p03-vblank-pacing/README.md) | vblank at the Quick Access refresh (50 / 49 Hz), change detection | ✓ | built |
| P-04 | `p04-external-tick` | external frame tick in `MainLoop` (C-1), DRC, turbo, TTD | ✓ | planned |
| P-05 | `p05-audio-latency` | SDL3 audio stream to PipeWire: latency, underruns, A/V offset | | planned |
| P-06 | `p06-late-start` | late tick ("frame delay") gain and stability | | planned |
| P-07 | `p07-crt-shader` | CRT pass ported to SDL_GPU (GLSL → SPIR-V), cost, parity | | planned |
| P-08 | [p08-input-inspector](p08-input-inspector/README.md) | what SDL3 sees of the Deck controller with Steam Input on / off | ✓ | built |
| P-09 | `p09-steamworks-module` | Steam Input / keyboard / haptics from a run-time-loaded module (AppID 480) | | planned |
| P-10 | `p10-auto-profile` | heuristic control scheme from port reads | | planned |
| P-11 | `p11-osk` | dual-trackpad OSK typing speed, ROM cursor mode | | planned |
| P-12 | `p12-signatures` | tape / TR-DOS / fuzzy signatures: recall, false matches, speed | ✓ | planned |
| P-13 | `p13-online-lookup` | ZXInfo / ZX-Art lookups by hash | | planned |
| P-14 | `p14-machine-state` | standalone machine state (C-2) for every model | ✓ | planned |
| P-15 | `p15-media-state` | media state re-attach (C-3) | | planned |
| P-16 | `p16-library-scale` | 10 000-card library in ImGui | | planned |
| P-17 | `p17-title-capture` | headless title-screen capture | | planned |
| P-18 | [p18-logind-sleep](p18-logind-sleep/README.md) | logind delay inhibitor in Game Mode, audio across sleep | ✓ | built |
| P-19 | [p19-runtime-packaging](p19-runtime-packaging/README.md) | steamrt4 build as non-Steam game, under SLR, as Flatpak | ✓ | built (container build works) |
| P-20 | `p20-add-to-steam` | writing `shortcuts.vdf` while Steam runs | | planned |
| P-21 | `p21-power` | power draw | | planned |
| P-22 | `p22-companion-stream` | binary WebSocket stream over Wi-Fi | | planned |
| P-23 | `p23-mirror-determinism` | lockstep mirror across x86-64 / arm64 | | planned |
| P-24 | `p24-music-engine` | emulated player vs ZXTune | | planned |
| P-25 | `p25-reference-track` | reference TTD track on several clients | | planned |

A folder is created when its experiment starts. Names in `code` are reserved.
