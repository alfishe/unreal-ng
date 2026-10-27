# unreal-deck: references

| | |
|---|---|
| **Date** | 2026-10-08 (every URL checked on this date) |
| **Used by** | all documents in this folder |

Valve moved the Steam Deck developer pages from `partner.steamgames.com/doc/steamdeck/*` to
`/doc/steamhardware/*` ("Steam Deck and Steam Machine"). The old URLs redirect, and the old
"Developing for Steam Deck without a Dev-Kit" page now lands on the documentation home. Some
functions in the current SDK headers are not on the web reference yet. Those rows cite the
headers.

## Contents

- [Steamworks SDK and API](#steamworks-sdk-and-api)
- [Steam Input](#steam-input)
- [Steam Deck: hardware and OS](#steam-deck-hardware-and-os)
- [Steam features](#steam-features)
- [Steam Linux Runtime and packaging](#steam-linux-runtime-and-packaging)
- [Gamescope and power events](#gamescope-and-power-events)
- [SDL3](#sdl3)
- [Dear ImGui](#dear-imgui)
- [Libraries, shortcuts, scene databases](#libraries-shortcuts-scene-databases)
- [In this repository](#in-this-repository)

## Steamworks SDK and API

| Title | URL | What we use |
|---|---|---|
| Steamworks SDK | <https://partner.steamgames.com/doc/sdk> | SDK download and layout |
| Steamworks API Overview | <https://partner.steamgames.com/doc/sdk/api> | init / shutdown, `SteamAPI_RunCallbacks`, `SteamAPI_ManualDispatch`, the flat C API (`steam_api_flat.h`), which fits loading the library at run time ([goals Q-1](goals-and-requirements.md#12-open-questions)) |
| steam_api.h | <https://partner.steamgames.com/doc/api/steam_api> | `SteamAPI_Init`, `SteamAPI_RestartAppIfNecessary`, `SteamAPI_RunCallbacks`, `SteamAPI_Shutdown` |
| steam_api.h, SDK headers (mirror) | <https://github.com/Facepunch/Facepunch.Steamworks/blob/master/Generator/steam_sdk/steam_api.h> | `SteamAPI_InitEx(SteamErrMsg*)` → `ESteamAPIInitResult` (`_OK`, `_NoSteamClient`, `_VersionMismatch`) and `SteamAPI_InitFlat()` for dynamically loaded `libsteam_api.so` (SDK ≥ 1.58); not on the web pages yet |
| ISteamUtils | <https://partner.steamgames.com/doc/api/ISteamUtils> | `IsSteamRunningOnSteamDeck`, `ShowFloatingGamepadTextInput` (`k_EFloatingGamepadTextInputModeModeSingleLine` / `…MultipleLines`), `ShowGamepadTextInput`, `IsSteamInBigPictureMode` |

## Steam Input

| Title | URL | What we use |
|---|---|---|
| Steam Input | <https://partner.steamgames.com/doc/features/steam_controller> | overview |
| Getting Started for Developers | <https://partner.steamgames.com/doc/features/steam_controller/getting_started_for_devs> | the Action Manifest / IGA workflow, `game_actions_<appid>.vdf` |
| In-Game Actions File | <https://partner.steamgames.com/doc/features/steam_controller/iga_file> | VDF format: action sets, `StickPadGyro` (`absolute_mouse`, `joystick_move`), `AnalogTrigger`, `Button`, localisation ([input-and-profiles.md §2.2](input-and-profiles.md#22-steam-input-back-end-appid-build-d3)) |
| ISteamInput Interface | <https://partner.steamgames.com/doc/api/ISteamInput> | `Init`, `RunFrame`, `GetActionSetHandle`, `ActivateActionSet`, `ActivateActionSetLayer`, `GetDigitalActionData`, `GetAnalogActionData`, `TriggerVibration`, `ShowBindingPanel`, `GetInputTypeForHandle` → `k_ESteamInputType_SteamDeckController`, `GetGlyphForActionOrigin` |
| ISteamInput, SDK header (`SteamInput006`) | same mirror as above, `isteaminput.h` | `TriggerSimpleHapticEvent` (trackpad haptics), `SetInputActionManifestFilePath` (ship the manifest with the app); not on the web page yet |
| Steam Input Gamepad Emulation – Best Practices | <https://partner.steamgames.com/doc/features/steam_controller/steam_input_gamepad_emulation_bestpractices> | what a non-Steam shortcut sees (an XInput-style virtual pad), glyph handling |

## Steam Deck: hardware and OS

| Title | URL | What we use |
|---|---|---|
| Steam Hardware | <https://partner.steamgames.com/doc/steamhardware> | new home of the Deck pages |
| Steam Deck | <https://partner.steamgames.com/doc/steamhardware/steamdeck> | the Deck developer overview |
| Getting your game ready for Steam Deck and Steam Machine | <https://partner.steamgames.com/doc/steamhardware/recommendations> | resolution and aspect, offline play, controller defaults, suspend |
| Steam Deck and Steam Machine Compatibility Review | <https://partner.steamgames.com/doc/steamhardware/compat> | the **Verified** criteria: the default controller config reaches all content, glyphs match the device, text entry uses a Steamworks on-screen keyboard API, legible text, no launcher that needs a mouse ([ux.md §10](ux.md#10-deck-verified-checklist)) |
| Steam Deck FAQ | <https://partner.steamgames.com/doc/steamhardware/steamdeck/faq> | general developer FAQ |
| Steam Deck Developer Kits | <https://partner.steamgames.com/doc/steamhardware/steamdeck/devkits> | devkit programme |
| How to load and run games on Steam Deck and Steam Machine | <https://partner.steamgames.com/doc/steamhardware/loadgames> | SteamOS Devkit Client (`devkit-gui`) + Devkit Service, discovery over mDNS; builds appear as "Devkit Game: …" ([integration.md §7](integration.md#7-build-test-and-release)) |
| How to debug Windows games on Steam Deck and Steam Machine | <https://partner.steamgames.com/doc/steamhardware/debugging> | remote debugging (mostly for Proton; the native build uses gdbserver) |
| Steam Hardware and Proton | <https://partner.steamgames.com/doc/steamhardware/proton> | why we ship a native Linux build, not a Windows build under Proton |
| Steam Deck Tech Specs (LCD) | <https://www.steamdeck.com/en/tech/deck> | 7" 1280×800 IPS, 60 Hz, 16 GB LPDDR5 |
| Steam Deck Tech Specs (OLED) | <https://www.steamdeck.com/en/tech/oled> | 7.4" 1280×800 HDR OLED, up to 90 Hz, 1000 nits peak (HDR), 110 % P3 |

## Steam features

Only when shipped with a Steam AppID (phase D3, [goals Q-1](goals-and-requirements.md#12-open-questions)).

| Title | URL | What we use |
|---|---|---|
| Steam Timelines | <https://partner.steamgames.com/doc/features/timeline> | Game Recording markers: rewinds, achievements, demo parts |
| ISteamTimeline | <https://partner.steamgames.com/doc/api/ISteamTimeline> | `AddInstantaneousTimelineEvent`, `AddRangeTimelineEvent`, `SetTimelineTooltip`, `SetTimelineGameMode`, `StartGamePhase` |
| Steam Cloud | <https://partner.steamgames.com/doc/features/cloud> | Auto-Cloud for profiles, resume states and slots |
| ISteamRemoteStorage | <https://partner.steamgames.com/doc/api/ISteamRemoteStorage> | API alternative to Auto-Cloud |
| Enhanced Rich Presence | <https://partner.steamgames.com/doc/features/enhancedrichpresence> | "Playing Elite on Pentagon 128" |
| ISteamFriends | <https://partner.steamgames.com/doc/api/ISteamFriends> | `SetRichPresence`, `ClearRichPresence` |
| Steam Workshop | <https://partner.steamgames.com/doc/features/workshop> | optional: sharing control profiles and CRT presets |
| ISteamUGC | <https://partner.steamgames.com/doc/api/ISteamUGC> | `CreateItem`, `SubmitItemUpdate` |

## Steam Linux Runtime and packaging

| Title | URL | What we use |
|---|---|---|
| Developing for SteamOS and Linux | <https://partner.steamgames.com/doc/store/application/platforms/linux> | Valve's pointer to the Steam Linux Runtime for native builds |
| Steam Linux Runtime – guide for game developers | <https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/slr-for-game-developers.md> | recommends **steamrt4** (SLR 4.0, Debian 13, newer SDL / Vulkan loader) over sniper (3.0); the runtime is selected in the app's Installation → Linux Runtime settings; SDK images run with podman / docker; `steam://install/1628350` installs sniper for non-Steam use |
| steamrt/sniper/sdk | <https://gitlab.steamos.cloud/steamrt/sniper/sdk/-/blob/steamrt/sniper/README.md> | SLR 3.0 SDK container |
| steamrt/steamrt4/sdk | <https://gitlab.steamos.cloud/steamrt/steamrt4/sdk/-/blob/steamrt/steamrt4/README.md> | SLR 4.0 SDK container (`registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk`) |
| pressure-vessel | <https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/pressure-vessel.md> | the container launcher the runtime uses |
| ValveSoftware/steam-runtime | <https://github.com/ValveSoftware/steam-runtime> | issue tracker |
| sniper images | <https://repo.steampowered.com/steamrt3/images/latest-public-beta/> | prebuilt runtime images |
| Flatpak documentation | <https://docs.flatpak.org/en/latest/> | packaging outside Steam |
| Flatpak: Available Runtimes | <https://docs.flatpak.org/en/latest/available-runtimes.html> | `org.freedesktop.Platform` / `.Sdk` |
| Decky Loader | <https://github.com/SteamDeckHomebrew/decky-loader> | optional later: a Quick Access Menu plugin (TypeScript / React + Python) that talks to the app over the local API |
| Decky plugin development | <https://wiki.deckbrew.xyz/en/plugin-dev/getting-started> | plugin template |

## Gamescope and power events

| Title | URL | What we use |
|---|---|---|
| ValveSoftware/gamescope | <https://github.com/ValveSoftware/gamescope> | the SteamOS compositor: Xwayland / Wayland clients, DRM / KMS direct flip, FSR / NIS / integer scaling (`-F`, `-S`), frame limiter (`-r`); [rendering.md §8](rendering.md#8-gamescope-specifics) |
| org.freedesktop.login1 | <https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html> | `PrepareForSleep(b start)`, `Inhibit(what, who, why, mode)` |
| org.freedesktop.login1 (man source) | <https://github.com/systemd/systemd/blob/main/man/org.freedesktop.login1.xml> | the same, as source |
| Inhibitor Locks | <https://systemd.io/INHIBITOR_LOCKS/> | take a `sleep` **delay** lock, save state on `PrepareForSleep(true)`, release the lock ([architecture.md §7](architecture.md#7-suspend-resume-and-exit)) |

## SDL3

| Title | URL | What we use |
|---|---|---|
| CategoryGPU | <https://wiki.libsdl.org/SDL3/CategoryGPU> | `SDL_CreateGPUDevice`, `SDL_ClaimWindowForGPUDevice`, `SDL_SetGPUSwapchainParameters` (present mode VSYNC / MAILBOX / IMMEDIATE, SDR / HDR composition), `SDL_SetGPUAllowedFramesInFlight`, `SDL_WaitAndAcquireGPUSwapchainTexture`, transfer buffers, `SDL_UploadToGPUTexture`, `SDL_SubmitGPUCommandBuffer`; Vulkan / Metal / D3D12 |
| SDL_shadercross | <https://github.com/libsdl-org/SDL_shadercross> | offline shader compilation to SPIR-V / MSL / DXIL |
| CategoryGamepad | <https://wiki.libsdl.org/SDL3/CategoryGamepad> | touchpads (`SDL_GetNumGamepadTouchpads`, `SDL_GetGamepadTouchpadFinger`), sensors (`SDL_SetGamepadSensorEnabled`, `SDL_GetGamepadSensorData`), `SDL_RumbleGamepad`, `SDL_GetGamepadSteamHandle` (bridge to `ISteamInput`), `SDL_GetGamepadButtonLabel` |
| SDL_GamepadType | <https://wiki.libsdl.org/SDL3/SDL_GamepadType> | `SDL_GAMEPAD_TYPE_STEAM`; there is **no** Deck-specific type, so the Deck is told apart by VID / PID or `IsSteamRunningOnSteamDeck` |
| SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK | <https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK> | the HIDAPI Deck driver (since 3.2.0) |
| SDL_HINT_JOYSTICK_HIDAPI_STEAM | <https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_HIDAPI_STEAM> | Steam Controller driver |
| SDL_hidapi_steamdeck.c | <https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_steamdeck.c> | what the Deck driver exposes (gyro, accelerometer, trackpads, back buttons) |
| CategoryHints | <https://wiki.libsdl.org/SDL3/CategoryHints> | all hints |
| CategoryAudio | <https://wiki.libsdl.org/SDL3/CategoryAudio> | `SDL_OpenAudioDeviceStream`, `SDL_PutAudioStreamData`, `SDL_GetAudioStreamQueued`, `SDL_SetAudioStreamFrequencyRatio` (DRC trim), `SDL_ResumeAudioStreamDevice` |
| SDL_CreateAudioStream | <https://wiki.libsdl.org/SDL3/SDL_CreateAudioStream> | format and rate conversion |

## Dear ImGui

| Title | URL | What we use |
|---|---|---|
| ocornut/imgui | <https://github.com/ocornut/imgui> | UI toolkit |
| imgui_impl_sdl3.cpp | <https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_sdl3.cpp> | platform back-end, gamepad feed |
| imgui_impl_sdlgpu3.cpp | <https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_sdlgpu3.cpp> | renderer back-end: `ImGui_ImplSDLGPU3_PrepareDrawData` (before the render pass), `ImGui_ImplSDLGPU3_RenderDrawData` (inside it) |
| example_sdl3_sdlgpu3 | <https://github.com/ocornut/imgui/blob/master/examples/example_sdl3_sdlgpu3/main.cpp> | reference loop |
| FAQ: gamepad controls | <https://github.com/ocornut/imgui/blob/master/docs/FAQ.md> | `ImGuiConfigFlags_NavEnableGamepad`, `ImGuiBackendFlags_HasGamepad` |

## Libraries, shortcuts, scene databases

| Title | URL | What we use |
|---|---|---|
| SteamGridDB/steam-rom-manager | <https://github.com/SteamGridDB/steam-rom-manager> | reference for writing non-Steam shortcuts |
| vdf-shortcuts-file.ts | <https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/vdf-shortcuts-file.ts> | `shortcuts.vdf` fields: `appid`, `AppName`, `Exe`, `StartDir`, `icon`, `LaunchOptions`, `tags` |
| SteamGridDB API v2 | <https://www.steamgriddb.com/api/v2> | grids / heroes / logos / icons for "Add to Steam" (API key required) |
| ZXDB | <https://github.com/zxdb/ZXDB> | the open Sinclair software database (MySQL dump, SQLite conversion script) |
| ZXInfo API v3 | <https://api.zxinfo.dk/v3/> | search, entries, `/filecheck/{hash}` (identify a file by hash); Spectrum Computing has no public API of its own |
| zxinfo-api-v3 | <https://github.com/thomasheckmann/zxinfo-api-v3> | API source |
| Spectrum Computing | <https://spectrumcomputing.co.uk/> | ZXDB front-end; links for the user |
| ZX-Art API | <https://zxart.ee/eng/api/> | pictures and music metadata (e.g. `https://zxart.ee/api/export:zxProd/language:eng/limit:1`) |
| RetroAchievements rcheevos | <https://github.com/RetroAchievements/rcheevos> | `rc_client`; `RC_CONSOLE_ZX_SPECTRUM = 59` exists |
| rc_client integration | <https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration> | integration steps |
| RA game identification | <https://docs.retroachievements.org/developer-docs/game-identification.html> | per-console hashing rules |
| Libretro: Playlists and Thumbnails | <https://docs.libretro.com/guides/roms-playlists-thumbnails/> | compared with: `.lpl` JSON, `Named_Boxarts` / `Named_Snaps` / `Named_Titles` |
| libretro-thumbnails, ZX Spectrum | <https://github.com/libretro-thumbnails/Sinclair_-_ZX_Spectrum> | an optional art source |

## In this repository

| Document | Relation |
|---|---|
| [HUD layer](../2026-09-07-hud-layer/design.md) | `HudModel` shared with a future SDL3 client: indicators and toasts |
| [iOS integration](../2026-09-19-ios-integration/ios-host-design.md) | the same embedding pattern (C facade over `CopyPresentedFramebuffer` / `SetAudioCallback`) on Metal |
| [Debugger family](../2026-09-28-debugger-family/workbench-framework.md) | §11 mobile and tablet companions; widget catalogue |
| [Debugger model: protocol](../2026-09-28-debugger-model/protocol.md) | one data model for every surface; the companion uses it |
| [Metadata manager](../2026-09-27-metadata-manager/design.md) | signature → packs library index; per-software knowledge |
| [zx-meta-db](../2026-09-28-zx-meta-db/concept.md) | the software metadata database the library cards read |
| [Media multisource](../2026-10-05-media-multisource/README.md) | host-folder overlays, write-back |
| [Snapshot pipeline](../2026-10-02-snapshot-pipeline/proposal.md) | load policies, `LastSnapshotReport` |
| [TTD v2 migration](../2026-09-25-ttd-v2-migration/current-state.md) | the engine rewind and replays use |
| [Kempston joystick TDD](../2026-09-15-atm-baseconf-highres-ports/tdd-kempston-joystick.md) | joystick model and its TTD journal |
