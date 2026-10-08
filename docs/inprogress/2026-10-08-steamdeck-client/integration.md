# unreal-deck: integration into the unreal-ng code base

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Design, for review |
| **Architecture** | [architecture.md](architecture.md) |

## Contents

- [1. Repository layout](#1-repository-layout)
- [2. Core APIs used](#2-core-apis-used)
- [3. Shared libraries](#3-shared-libraries)
- [4. Core changes](#4-core-changes)
- [5. CMake](#5-cmake)
- [6. Third-party dependencies](#6-third-party-dependencies)
- [7. Build, test and release](#7-build-test-and-release)
- [8. Relation to other in-progress designs](#8-relation-to-other-in-progress-designs)
- [9. Work breakdown for D0–D1](#9-work-breakdown-for-d0d1)

## 1. Repository layout

`unreal-deck/` is a sibling of `unreal-qt/`, `unreal-videowall/` and `unreal-screen-viewer/`.
Front-end-neutral code goes into shared libraries under `core/frontend/`, so `unreal-qt` can use
it too (gamepads, profiles, signatures, the HUD model).

```
unreal-deck/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                    SDL_AppInit / SDL_AppIterate / SDL_AppEvent (SDL3 main callbacks)
│   ├── app/                        DeckApp, screen stack, settings
│   ├── screens/                    library, gamecard, ingame, quickmenu, osk, radial, mapping, settings
│   ├── render/                     IDeckRenderer, SdlGpuRenderer, frame texture, CRT pass, art atlas
│   ├── audio/                      AudioOut (SDL audio stream ↔ SetAudioCallback, DRC occupancy)
│   ├── input/                      SdlGamepadBackend, SteamInputBackend (stub without the module)
│   ├── services/                   LibraryService, ArtService, SessionController, StateStore, PowerMonitor
│   ├── platform/                   linux (logind, XDG), macos, windows
│   └── steam/                      optional module: libunreal-deck-steam (Steamworks behind a C API)
├── shaders/                        HLSL sources → SPIR-V / MSL / DXIL at build time (SDL_shadercross)
├── assets/                         fonts, glyphs, OSK layouts, machine photos, default art
└── install/
    ├── steam/                      game_actions.vdf (IGA), depot layout, launch script
    ├── flatpak/                    org.unrealng.Deck.yml
    └── linux/                      .desktop, icons

core/frontend/                      NEW: front-end-neutral libraries (no SDL, no Qt)
├── input/                          ControlEvent, InputRouter, profile model + JSON, ZxInputSink
├── swsignature/                    media signatures (tape payload, TR-DOS file set, fuzzy, memory)
└── hud/                            HudModel (from the HUD-layer design), when that design lands

data/deck/profiles/                 community control profiles (JSON, reviewed by PR)
core/tests/frontend/                unit tests for core/frontend/*
```

## 2. Core APIs used

All of these exist today. The Deck front-end needs nothing from `unreal-qt`.

| Need | API | Where |
|---|---|---|
| Create / destroy an instance | `EmulatorManager::CreateEmulatorWithModel(id, model, …, configOverride)`, `ShutdownAllEmulators`, `PrepareForShutdown` | `core/src/emulator/emulatormanager.h` |
| Run control | `Emulator::StartAsync`, `Pause`, `Resume`, `Reset(hard)`, `EnableTurboMode` | `core/src/emulator/emulator.h` |
| Load media | `LoadTape`, `LoadDisk`, `AutostartDisk`, snapshot pipeline (`LastSnapshotReport`) | `emulator.h`, `core/src/loaders/` |
| Frame | `NC_VIDEO_FRAME_REFRESH`, `NC_VIDEO_MODE_CHANGED`, `Screen::GetFramebufferDescriptor`, `CopyPresentedFramebuffer`, `GetDisplayViewport`, `SetPresentDelayFrames` | `core/src/emulator/video/screen.h`, `platform.h` |
| Audio | `Emulator::SetAudioCallback(obj, cb, occupancyFrames, deviceDescriptor)`, `SetAudioDeviceSampleRate`, `ClearAudioCallback`; int16 stereo once per frame | `emulatorcontext.h`, `sound/soundmanager.cpp` |
| Keys | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` + `KeyboardEvent(ZXKeysEnum…)` | `io/keyboard/keyboard.h` |
| Kempston joystick | `Joystick::Press / Release / SetState` (atomic) | `io/joystick/joystick.h` |
| Kempston mouse | `MC_MOUSE_MOVE` / `MC_MOUSE_BUTTON` / `MC_MOUSE_WHEEL`, `mousedeltaaccumulator.h` (written to serve Qt and SDL) | `io/mouse/` |
| Catalogues | `TrdosCatalog` / `TrdosFile`, `TapeCatalog` | `io/fdc/trdoscatalog.h`, `io/tape/tapecatalog.h` |
| Media type | `MediaTargets::Classify` (content first, then extension) | `core/src/emulator/media/mediatargets.cpp` |
| Media overlays | media manager slots, change layer, write-back | `core/src/emulator/media/`, [media-multisource](../2026-10-05-media-multisource/README.md) |
| Snapshots | SNA / Z80 / SZX save, `SnapshotCapture::SaveSnapshotFile` | `core/src/loaders/snapshot/` |
| Rewind / replays (D3) | `TimeTravelManager` (`StartRecording`, `StepBackFrame`, `SeekTo`, `SerializeSession` / `DeserializeSession`); v2 engine after its switchover | `core/src/debugger/ttd/` |
| Port statistics (auto profile) | `MemoryAccessTracker` monitored ports | `core/src/emulator/memory/memoryaccesstracker.h` |
| Temporal effects | ZX DLSS de-flicker | `core/src/emulator/video/zxdlss/` |
| Settings per instance | `FeatureManager` (`sound`, `turbo`, `fasttape`, `timetravel`, …) | `core/src/base/featuremanager.h` |
| Headless art capture | instance with video mode `M_NUL`, turbo, `RunNFrames`, then one rendered frame | `emulator.h` |

## 3. Shared libraries

| Library | Target | Content | Also used by |
|---|---|---|---|
| input | `unrealng::frontend-input` | `ControlEvent`, layers, bindings, modifiers (dead-zone, ways, turbo, chords, ballistics), profile JSON (de)serialization and merge, `ZxInputSink` → core | `unreal-qt`, for host gamepad support (it has none today) |
| swsignature | `unrealng::swsignature` | tape payload, TR-DOS file set, fuzzy set, memory signatures; uses the core's loaders to parse | library scanners, metadata manager, zx-meta-db, `unreal-qt` "identify" |
| hud | `unrealng::hud` | `HudModel` + `HudSnapshot` ([HUD layer design](../2026-09-07-hud-layer/design.md)) | `unreal-qt`, TUI |

These libraries are tested in `core-tests` (`core/tests/frontend/`) with fixtures from `testdata/`.

## 4. Core changes

Small and additive. None changes emulation behaviour, TTD files or snapshots.

| ID | Change | Why | Phase |
|----|--------|-----|-------|
| C-1 | **External frame tick.** `MainLoop` pacing mode `External`: wait on a tick (`Emulator::SignalFrameTick()`, a counting semaphore) instead of the deadline; the emergency refill and DRC keep working; `turbo` overrides it; it switches back to `Deadline` on request | display-locked pacing ([rendering.md §4](rendering.md#4-frame-pacing-50-hz-machines-on-a-6090-hz-panel)) | D1 |
| C-2 | **Standalone machine state.** Save / load one complete machine state outside a TTD session, built on the TTD v2 device table and memory regions; versioned; model check on load | the resume container ([architecture.md §6](architecture.md#6-persistence)) | D1 |
| C-3 | **Media state description.** A serializable description of the inserted media (slots, paths, hashes, tape position, motor, overlay ids) and its re-attach | resume / Continue with media | D1 |
| C-4 | **Cheap port-read counters.** A lightweight mode of the monitored ports (counts per port per frame, no per-access record), switchable at run time | auto profile ([input-and-profiles.md §5](input-and-profiles.md#5-heuristic-auto-profile)) | D2 |
| C-5 | **Fuller joystick** (port `#7F`), as a second joystick type next to Kempston (`[INPUT] Joystick=`) | completeness of targets | D2 |
| C-6 | **Audio occupancy from an SDL stream.** No API change: `AudioOut` publishes `SDL_GetAudioStreamQueued` as frames into the `occupancyFrames` atomic. Documented as the second host after miniaudio | DRC on the Deck | D0 |

## 5. CMake

```cmake
# CMakeLists.txt (top level)
option(BUILD_DECK_APP "Build unreal-deck (SDL3 front-end for handhelds / TV)" OFF)
option(DECK_STEAMWORKS "Build the optional Steamworks module for unreal-deck" OFF)

add_subdirectory(core/frontend)             # always: tiny, tested by core-tests

if(BUILD_DECK_APP)
    find_package(SDL3 3.2 CONFIG REQUIRED)  # system / runtime SDL3; FetchContent fallback
    add_subdirectory(unreal-deck)
endif()
```

```cmake
# unreal-deck/CMakeLists.txt (sketch)
add_executable(unreal-deck ${DECK_SOURCES})
target_link_libraries(unreal-deck PRIVATE
    unrealng::core unrealng::frontend-input unrealng::swsignature
    SDL3::SDL3 imgui sqlite3)
deck_compile_shaders(unreal-deck shaders/crt.hlsl shaders/blit.hlsl)  # SDL_shadercross → SPIR-V / MSL / DXIL headers

if(DECK_STEAMWORKS)
    add_library(unreal-deck-steam MODULE src/steam/steambridge.cpp)   # loads libsteam_api at run time
endif()
```

- **Automation is off** in the Deck player build by default (`ENABLE_AUTOMATION=OFF`). That means
  no Drogon and no listening ports on a handheld, and a smaller binary. Companion mode (D4) has its
  own client and needs no server on the Deck. A Deck-side server, e.g. for a Decky plugin, is an
  explicit opt-in.
- **Qt is not needed:** `BUILD_QT_APPS=OFF BUILD_DECK_APP=ON` is a valid configuration, and the CI
  image for it has no Qt.
- **Compiler flags.** The release binary targets `-march=x86-64-v3`, which Zen 2 supports. A
  `-march=znver2` build is only for local experiments.

## 6. Third-party dependencies

| Library | Licence | How |
|---|---|---|
| SDL3 (≥ 3.2) | zlib | from the runtime (steamrt4, freedesktop), or vendored for macOS / Windows |
| SDL_shadercross | zlib | build-time tool only |
| Dear ImGui (≥ 1.91, SDL3 + SDL_GPU back-ends) | MIT | vendored in `lib/imgui` |
| SQLite (amalgamation) | public domain | vendored in `lib/sqlite` |
| stb_image / stb_truetype or FreeType | public domain / FTL | vendored; FreeType from the runtime |
| libdbus-1 | AFL / GPL-2+ | from the runtime, loaded like SDL does; Linux only |
| Steamworks SDK | proprietary | only in the optional module; never in the main binary ([goals Q-1](goals-and-requirements.md#12-open-questions)) |

The core's own dependencies (lzma, zstd, miniz, …) are unchanged. OpenSSL is not needed:
`UNREAL_HOST_TLS` stays off, and HTTPS for online metadata goes through libcurl from the runtime,
loaded only when online features are enabled.

## 7. Build, test and release

| Step | How |
|---|---|
| Local build (macOS / Linux) | `tools/build/build.sh unreal-deck` after one configure with `-DBUILD_DECK_APP=ON` (the wrapper keeps it, like `-DBENCHMARKS=ON`) |
| Unit tests | `tools/build/test.sh --gtest_filter='*Frontend*:*Signature*:*InputRouter*'`; the pacing (C-1) and state (C-2) changes get core tests |
| Linux CI | a new job in the steamrt4 SDK container (`registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk`) building `unreal-deck` with gcc, with zero warnings, as all jobs |
| On the Deck (development) | SteamOS Devkit Client (`devkit-gui`) uploads the build, which then appears as "Devkit Game: unreal-deck"; or `rsync` over SSH + a non-Steam shortcut |
| Debugging on the Deck | `gdbserver` over SSH; MangoHud for frame times; `PROTON_`-free, since the build is native |
| Release: Steam (if Q-1 allows) | depot with `unreal-deck`, `game_actions.vdf`, assets; Linux Runtime set to steamrt4 in the app's Installation settings |
| Release: outside Steam | Flatpak (`org.freedesktop.Platform`) on Flathub, then added as a non-Steam game; a tarball for SteamOS users who prefer it |
| Release: desktop | the same binary runs on desktop Linux (TV / couch mode); macOS and Windows builds are by-products of development, not release targets in D1 |

## 8. Relation to other in-progress designs

| Design | What unreal-deck takes | What it adds |
|---|---|---|
| [HUD layer](../2026-09-07-hud-layer/design.md) | `HudModel`, which already plans an "SDL3 / GPU player" presenter | the first non-Qt presenter |
| [iOS integration](../2026-09-19-ios-integration/ios-host-design.md) | the same embedding (frame copy + audio callback) | an SDL3 host, proving the pattern on a second platform |
| [Metadata manager](../2026-09-27-metadata-manager/design.md) | library index keyed by signature | concrete signature algorithms (`swsignature`) |
| [zx-meta-db](../2026-09-28-zx-meta-db/concept.md) | titles, years, publishers, art links | a consumer with a UI |
| [Debugger family](../2026-09-28-debugger-family/workbench-framework.md) §11 | the companion concept | the Deck as the first companion (D4) |
| [Debugger model: protocol](../2026-09-28-debugger-model/protocol.md) | the data model and command set | a binary transport for the high-rate parts ([companion-and-media.md §4](companion-and-media.md#4-the-companion-protocol)) |
| [Media multisource](../2026-10-05-media-multisource/README.md) | change layer, write-back, host-folder overlays | the overlay policy for library files (FR-44) |
| [Snapshot pipeline](../2026-10-02-snapshot-pipeline/proposal.md) | load policies, reports | the resume container (C-2) as a pipeline client |
| [TTD v2 migration](../2026-09-25-ttd-v2-migration/current-state.md) | engine for rewind and replays | rewind-on-a-button as a product use case |

## 9. Work breakdown for D0–D1

| # | Item | Size |
|---|------|------|
| 1 | CMake option, `unreal-deck` skeleton with SDL3 main callbacks, window, SDL_GPU device, swapchain | S |
| 2 | Frame texture upload + blit shader + Deck-fit crop; `NC_VIDEO_MODE_CHANGED` handling | S |
| 3 | `AudioOut` on SDL audio stream + DRC occupancy (C-6) | S |
| 4 | `SdlGamepadBackend` + minimal router (D-pad → Kempston, buttons → keys) | S |
| 5 | Core C-1 external tick + `SessionController` pacing selection + refresh measurement | M |
| 6 | ImGui integration (SDL3 + SDL_GPU back-ends), UI kit, fonts, glyphs | M |
| 7 | Library scanner + SQLite index + shelves + cards + catalogues | L |
| 8 | Art service (local, headless capture) | M |
| 9 | Quick menu, media swap, display and sound settings | M |
| 10 | OSK 48K / 128K / compact, dual-trackpad cursors, mode-aware legends | M |
| 11 | Core C-2 / C-3 machine and media state; `StateStore`; slots; Continue | L |
| 12 | `PowerMonitor` (logind delay inhibitor) + suspend / resume flow | S |
| 13 | CRT pass port (shared presets with `crtprofiles`) | M |
| 14 | Measurements M-1…M-5 on LCD and OLED Decks; results table | M |
