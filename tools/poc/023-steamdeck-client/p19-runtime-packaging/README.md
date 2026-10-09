# P-19 runtime and packaging (gate)

**Question.** Does a binary built in the steamrt4 SDK container run on the Deck in these three ways?

1. as a plain non-Steam game;
2. inside the Steam Linux Runtime;
3. as a Flatpak.

Which SDL3 does each environment bring?
([integration.md §7](../../../../docs/inprogress/2026-10-08-steamdeck-client/integration.md))

**Pass.** All three run, with no host-library leaks.

## Parts

| File | Use |
|---|---|
| [../build.sh](../build.sh) | the build itself: `registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk`, amd64, gcc 14, SDL3 from the runtime |
| [check-deps.sh](check-deps.sh) | `ldd` of every POC binary inside the SDK container: lists unresolved libraries and the runtime's SDL3 version |
| [run-in-slr.sh](run-in-slr.sh) | on the Deck: runs a binary through the installed `SteamLinuxRuntime_4*` (or `--runtime sniper`) `run` entry point |
| `flatpak/` | (next) manifest on `org.freedesktop.Platform` |

## Run

```bash
tools/poc/023-steamdeck-client/build.sh
tools/poc/023-steamdeck-client/p19-runtime-packaging/check-deps.sh
DECK=deck@steamdeck.local tools/poc/023-steamdeck-client/deploy.sh
# On the Deck, in Game Mode:
#   (a) non-Steam game: ~/steamdeck-poc/p08-input-inspector
#   (b) non-Steam game: ~/steamdeck-poc/run-in-slr.sh, Launch Options: ./p08-input-inspector
```

## Results

| Date | Check | Result |
|------|-------|--------|
| 2026-10-08 | build in the steamrt4 SDK container (amd64 emulated on an M1 Mac; gcc 14.2, cmake 3.31, SDL3 3.4.14) | all four POCs build with 0 warnings at `-march=x86-64-v3` and at `-march=x86-64`. A cold core build takes ~10 min under emulation. Two core fixes were needed for x86-64-v3 (see below) |
| 2026-10-08 | `check-deps.sh` | every library resolves inside the runtime. With `UNREAL_HOST_TLS` on, the core pulled in libssl / libcrypto, so the POC build turns it off. The runtime's libSDL3 brings X11, Wayland, PulseAudio, ALSA and dbus |
| 2026-10-08 | smoke run in the container (baseline ISA, `SDL_VIDEO_DRIVER=offscreen`) | p08 runs and writes its logs. p01 creates and starts a 48K instance (configs and ROMs found next to the binary), then stops at "No supported SDL_GPU backend" (the container has no Vulkan driver). p18 reports no system bus (expected in a container). x86-64-v3 binaries die with SIGILL under Docker's amd64 emulation (no AVX2), which is why there is a baseline build. The containers have no outbound network, so lavapipe could not be installed: the GPU path is only testable on the Deck |
| 2026-10-08 | core fixes for `-march=x86-64-v3` | `ttdcompression.h`: the SSE4.2 CRC32C path and the table fallback were compiled into one scope ("redeclaration of crc") as soon as `__SSE4_2__` is defined. Made it one `#if / #elif / #else` chain. `z80textassembler.cpp`: a GCC 14 `-O3` false `-Warray-bounds` on `std::string(...) + char`, rewritten as appends |
| — | (a) plain, Game Mode | |
| — | (b) inside SLR 4.0 | |
| — | (c) Flatpak | |
| — | SDL3 version: SDK container / Deck host / Flatpak | |
