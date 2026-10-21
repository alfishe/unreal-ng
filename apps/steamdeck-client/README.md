# steamdeck-client: Steam Deck client proofs of concept

The experiments of the [POC plan](../../docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md) for the
native Steam Deck front-end ([design](../../docs/inprogress/2026-10-08-steamdeck-client/README.md)).
There is one folder per experiment, `pNN-<name>/`, numbered as in the plan. Each folder's README holds
the goal, how to run it, and the measured results. The verdicts are copied into the plan's results table.

## Build, deploy, run

```bash
# 1. Build in the Steam Linux Runtime 4.0 SDK container (amd64, gcc 14, SDL3 3.4)
apps/steamdeck-client/build.sh                       # every POC
apps/steamdeck-client/build.sh p08-input-inspector   # one target

# 2. Copy to the Deck (SSH enabled in Desktop Mode) and install "Unreal NG" (menu, desktop, Steam library)
DECK=deck@steamdeck.local apps/steamdeck-client/deploy.sh

# 3. Fetch the logs back (~/steamdeck-poc-logs on the Deck -> build/deck-logs/)
DECK=deck@steamdeck.local apps/steamdeck-client/deploy.sh --logs
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

  Each `bin/` has the runtime data (`configs/`, `rom/`, `symbols/`, ...) staged next to the binaries.
- The root CMake option `BUILD_STEAMDECK_POC=ON` adds this folder. `build.sh` sets it together with
  `BUILD_QT_APPS=OFF ENABLE_AUTOMATION=ON UNREAL_HOST_TLS=OFF -march=x86-64-v3`; `--no-automation` builds without
  the servers.
- On Apple Silicon the amd64 container runs emulated. The first build of the core is slow; later
  builds are incremental. x86-64-v3 binaries do not run under Docker's amd64 emulation (no AVX2): use
  `--march x86-64` for a copy to smoke-run in the container.
- **Other hosts.** The POCs are SDL3 code, not Linux code. With SDL3 installed (e.g. Homebrew), configure
  once with `-DBUILD_STEAMDECK_POC=ON -DBUILD_QT_APPS=OFF -DUNREAL_HOST_TLS=OFF -DTESTS=OFF` into its own
  build directory (`apps/steamdeck-client/build/macos`), then
  `BUILD_DIR=apps/steamdeck-client/build/macos tools/build/build.sh steamdeck-poc-all`. P-01 / P-03 / P-08 build
  and run on macOS (Metal); `--windowed` opens a window instead of going fullscreen. P-18 (logind) is
  Linux only and skipped elsewhere.

### Running on the Deck

| How | When |
|---|---|
| **The "Unreal NG" app.** `deploy.sh` runs `deck-install.sh` on the Deck: a launcher in the KDE menu and on the desktop, and a non-Steam game in the Steam library (Game Mode: Library → Non-Steam). It starts p01 with `--boot` fullscreen: the ROM boots, the WebAPI drives it. Steam has to be running for the library entry (Desktop Mode or Game Mode); otherwise run `~/steamdeck-poc/deck-install.sh` again later. `--remove` takes the launchers away | using the emulator on the Deck |
| **Game Mode, non-Steam game.** In Desktop Mode, Steam → *Add a Non-Steam Game* → browse to `~/steamdeck-poc/<binary>`, put arguments in *Launch Options*, then switch to Game Mode and start it from the library | every measurement: this is the real environment (gamescope, Steam Input) |
| Game Mode, inside the runtime: the non-Steam game's target is `~/steamdeck-poc/run-in-slr.sh` and its Launch Options are `./<binary> [args]` (see [p19](p19-runtime-packaging/README.md)) | P-19 |
| Desktop Mode, terminal | quick checks only (KWin, not gamescope) |

Every POC writes its logs to `~/steamdeck-poc-logs/` (Game Mode has no visible stdout).
**View + Menu held** (or Esc) quits every POC.

### Getting the build onto the Deck

| Way | How |
|---|---|
| **SSH over Wi-Fi** (`deploy.sh`) | Once, in Desktop Mode (Steam button → Power → Switch to Desktop), Konsole: `passwd` (the `deck` user has no password at first), `sudo systemctl enable --now sshd`, `ip -4 addr` for the address. Then from the Mac: `DECK=deck@<address> apps/steamdeck-client/deploy.sh`. `steamdeck.local` works where mDNS does. It copies the binaries without debug info (p01 ~37 MB instead of ~530 MB) and the runtime data, ~110 MB the first time; after a rebuild rsync sends only what changed |
| USB-C stick or microSD card | Run `apps/steamdeck-client/deploy.sh --stage` and copy `build/deck/deploy/` (the stripped copy, ~110 MB) to it, plug it into the Deck, in Desktop Mode copy the folder to `~/steamdeck-poc/` with Dolphin. Make the binaries executable: `chmod +x ~/steamdeck-poc/p*` |
| Steam Devkit Management Tool | Valve's tool for Steamworks titles; needs an app ID. Not used here |

`deploy.sh` then installs the app (table above); from a stick, run `~/steamdeck-poc/deck-install.sh` once.

### Remote control (WebAPI, MCP)

Built with automation (the default), the emulator inside `p01-sdl-gpu-latency` serves the WebAPI on
`http://<deck>:8090/api/v1` (all interfaces, plain HTTP) and MCP on `:8092/mcp`; the CLI (`:8765`) and the debug
servers listen on the Deck's loopback only (reach them over SSH). The ports move with `UNREAL_WEBAPI_PORT`,
`UNREAL_MCP_PORT`, `UNREAL_CLI_PORT`
([environment-variables.md](../../docs/emulator/environment-variables.md)). The instance is `deck-poc` in
`GET /api/v1/emulator`; the routes take its `id`. Anyone on the network can drive it: use a trusted network.

```bash
DECK=http://<deck>:8090/api/v1/emulator
ID=$(curl -s $DECK | python3 -c 'import json,sys; print(json.load(sys.stdin)["emulators"][0]["id"])')
curl -X PUT -H 'Content-Type: application/json' -d '{"muted":true}' $DECK/$ID/audio/mixer/master
curl -X POST -H 'Content-Type: application/octet-stream' -H 'X-Filename: game.sna' --data-binary @game.sna \
     $DECK/$ID/snapshot/load                                  # upload and start a snapshot from the Mac
curl "$DECK/$ID/capture/screen?format=png"                    # the screen (PNG, base64 in JSON)
```

`--headless` runs the emulator and the servers without a window or GPU (an SSH session, a container): Ctrl+C,
SIGTERM or `--seconds N` ends it. Over SSH: `cd ~/steamdeck-poc && ./p01-sdl-gpu-latency --headless`.

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

## Status (added 2026-10-10)

**running (pre-guideline)** — five of 25 experiments exist (P-01, P-03, P-08, P-18, P-19), all "built", none yet measured
on a Deck ("Index" above); the rest are planned. Verdicts go to the results table of
`docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md`.

Serves: `docs/inprogress/2026-10-08-steamdeck-client/`. Started as POC 023 in `tools/poc/`; moved to `apps/` on 2026-10-20.
