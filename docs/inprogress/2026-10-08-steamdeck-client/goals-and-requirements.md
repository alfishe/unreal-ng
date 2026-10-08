# unreal-deck: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Design, for review. No code |
| **Branch** | `steamdeck-design` (worktree `scratch/wt-steamdeck-design`) |
| **Next** | [ux.md](ux.md) · [architecture.md](architecture.md) · [rendering.md](rendering.md) |

## Contents

- [1. Problem](#1-problem)
- [2. What exists today](#2-what-exists-today)
- [3. Target hardware](#3-target-hardware)
- [4. Goals](#4-goals)
- [5. Non-goals](#5-non-goals)
- [6. Personas and use cases](#6-personas-and-use-cases)
- [7. Functional requirements](#7-functional-requirements)
- [8. Non-functional requirements](#8-non-functional-requirements)
- [9. Phases](#9-phases)
- [10. Acceptance](#10-acceptance)
- [11. Corrections to the source discussion](#11-corrections-to-the-source-discussion)
- [12. Open questions](#12-open-questions)

## 1. Problem

unreal-ng has a desktop Qt front-end (`unreal-qt`) built for a mouse, a keyboard and a large
monitor. On a Steam Deck in Game Mode it is unusable: there is no keyboard, the screen is 7"
at 1280×800, the controls are two sticks, two trackpads, a gyro and four back buttons, and
gamescope expects one fullscreen surface.

A ZX Spectrum is a keyboard computer. Most software needs keys (`LOAD ""`, menus, redefine
keys, text adventures), and every game picks its own control scheme (Kempston, Sinclair, cursor,
QAOP+M, its own keys). Without per-game mapping the user spends the first minute of every game
setting up controls.

## 2. What exists today

On the Deck, ZX Spectrum and Amiga emulation is usually set up through EmuDeck or RetroDeck. Under
the hood that is RetroArch with the Fuse or PUAE core, or standalone ports (Amiberry, FS-UAE,
ZEsarUX). Steam ROM Manager adds the games to Steam. The common problems:

| Problem | Where |
|---|---|
| The virtual keyboard is a generic grid, not the Spectrum's keyword keyboard | RetroArch overlay |
| Library scanning by whole-file CRC ignores re-packed or saved-to TRD / SCL images | RetroArch `.rdb` scanner |
| A disk is an opaque ROM: you cannot see the TR-DOS catalogue, or change the disk in B: from the UI | RetroArch |
| A desktop GUI with nested menus driven by a stick | ZEsarUX, Fuse, Amiberry's GUI |
| No clones beyond 128K / Pentagon in practice (TS-Conf, ZX-Evo BaseConf, ATM, Sprinter) | everything except ZEsarUX |
| One Steam shortcut per game: thousands of entries slow the Steam library | Steam ROM Manager on large collections |

No emulator offers a controller-first Spectrum experience with clone coverage. unreal-ng already
has the clones, the media layer, TTD, snapshots and an automation API. What is missing is a
front-end built for the device.

## 3. Target hardware

| | Steam Deck LCD | Steam Deck OLED |
|---|---|---|
| Display | 7", 1280×800, 16:10, 60 Hz; refresh slider 40–60 Hz | 7.4", 1280×800, HDR, up to 90 Hz; refresh slider 45–90 Hz |
| APU | AMD Zen 2 4C/8T + RDNA 2, 8 CU | same (6 nm) |
| RAM | 16 GB LPDDR5 | 16 GB LPDDR5 (6400 MT/s) |
| GPU API | Vulkan 1.3 (Mesa RADV), OpenGL 4.6 | same |
| Controls | D-pad, ABXY, L1/R1, analog L2/R2, two sticks (capacitive), two square trackpads (pressure click, haptics), 6-axis gyro, L4/L5/R4/R5 back buttons, View, Menu; **Steam** and **Quick Access (…)** are reserved by the system | same |
| OS | SteamOS 3 (Arch-based, read-only root, Flatpak for user apps), gamescope in Game Mode, KDE Plasma in Desktop Mode | same |

The panel refresh range matters for the Spectrum. A 128K frame is ≈ 50.02 Hz and a Pentagon frame
is ≈ 48.83 Hz, and **both panels can run at 50 Hz**. That gives one display refresh per
emulated frame, with no judder (see [rendering.md §4](rendering.md#4-frame-pacing-50-hz-machines-on-a-6090-hz-panel)).

## 4. Goals

| ID | Goal |
|----|------|
| G-1 | **Native and fast.** One process with the core linked in, one GPU texture upload and one draw per frame, input-to-photon latency of at most two panel refreshes at 50 Hz (≤ 40 ms; target ~25 ms). |
| G-2 | **Controller first.** Every function is reachable with D-pad + A/B and never needs the touchscreen. Touch and trackpads are shortcuts, not requirements. |
| G-3 | **Zero-setup controls.** A game that is recognised starts with the right profile, and an unknown game gets a heuristic profile. The user can edit any binding in two screens. |
| G-4 | **Keyboard when needed.** A Spectrum on-screen keyboard (48K rubber or 128K layout, keyword legends) plus a radial menu for frequent commands (`LOAD ""`, `RUN`, `CAT`, BREAK, NMI). The system keyboard is used for host-side text (search, file names). |
| G-5 | **Instant suspend / resume.** The Power button and quitting the app never lose state. Re-opening a game offers "Continue". |
| G-6 | **A library, not a file dialog.** Cards for games, cassettes, disks and saves. Disk cards show the TR-DOS catalogue. Machine profiles cover HDD and SD images. |
| G-7 | **All machines of the core.** 48K / 128K / Pentagon / Scorpion / Profi / ATM / ZX-Evo BaseConf / TS-Conf / Sprinter / ZX-Poly, with their sound devices, as far as the core supports them. |
| G-8 | **Reuse, do not fork.** No emulation code in the front-end. Front-end-neutral pieces (HUD model, keyboard map, profile store, metadata) live in shared libraries usable by `unreal-qt` as well. |
| G-9 | **Steam-native where it helps.** It is Deck Verified-ready (default controller config, legible text, system keyboard, no launcher), and uses Steam Input / Cloud / Timeline / Rich Presence when shipped with a Steam AppID. It still works fully as a non-Steam shortcut or Flatpak. |

## 5. Non-goals

- **Replacing `unreal-qt`.** The desktop debugger stays in Qt. The Deck gets a companion view of it
  (phase D4), not a port.
- **Qt on the Deck.** No Qt Widgets or QML in `unreal-deck` (see [architecture.md §2](architecture.md#2-why-not-qt-why-sdl3)).
- **Other systems (Amiga, C64).** This is a Spectrum-family front-end.
- **Scraping, hosting or redistributing copyrighted software.** Online catalogues (ZXDB / Spectrum
  Computing, ZXArt, Pouët) provide metadata and links to downloads that their sites permit. The app
  ships no games.
- **Network play and a cloud compute node** in the first phases (see [companion-and-media.md](companion-and-media.md)).

## 6. Personas and use cases

| Persona | Wants |
|---|---|
| **Player** | Pick a game from covers, play with sensible controls, put the Deck to sleep mid-game, continue tomorrow. |
| **Collector** | Browse thousands of TAP / TZX / TRD / SCL files, see what is on a disk, find versions, favourites. |
| **Scener** | Watch demos and listen to chip music on every sound device, follow a demoparty live. |
| **Developer** | Use the Deck as a second screen for `unreal-qt` on a PC: TTD scrubbing, two screen pages, Gigascreen phases, remote settings, video-wall control. |

| ID | Use case | Phase |
|----|----------|-------|
| UC-1 | Launch the app from Steam; the library opens on the last shelf; A on a card starts the game with its profile. | D1 |
| UC-2 | An unknown `.tap` is loaded; the auto profile maps the D-pad to Kempston if the game reads port `#1F`, else to QAOP / Space. A toast says which. | D2 |
| UC-3 | A text adventure: the OSK slides up with the 48K layout; the two trackpads drive two cursors. | D1 |
| UC-4 | Press Power mid-game; wake 8 hours later; the game is where it was, with no audio click and no lost frames. | D1 |
| UC-5 | Quit to the library; the card shows "Continue" with a thumbnail from the moment of exit. | D1 |
| UC-6 | Disk game: the card opens the TR-DOS catalogue; the user picks a file to run, or boots the disk, and swaps disk B: later from the quick menu. | D1 |
| UC-7 | Edit a profile: hold the back button, pick a Deck control, pick a ZX target (key, Kempston fire, mouse). The edited profile is saved locally under the game's signature. | D2 |
| UC-8 | Hold L4 to rewind the last seconds; release to play on. | D3 |
| UC-9 | Save a speed-run replay (TTD session) and share it. | D3 |
| UC-10 | Music hub: a playlist of AY / TurboSound / GS / SAA tracks and demo parts, with a register / scope view. | D5 |
| UC-11 | Demoparty mode: the compo runs locally in sync with the stream; a demo that fails validation is played from its reference TTD track. | D6 |
| UC-12 | Companion: the Deck shows the TTD timeline, both screen pages, Gigascreen phases and controls of a PC instance. | D4 |

## 7. Functional requirements

### Application shell

| ID | Requirement |
|----|-------------|
| FR-1 | One fullscreen window at the panel's native resolution, with no window decorations and no launcher. It works under gamescope (Game Mode) and in a desktop session (Plasma, other Linux desktops; macOS / Windows for development). |
| FR-2 | Start in the library, or directly into a file passed on the command line (`unreal-deck <file> [--profile <id>] [--model <name>]`), so that a Steam shortcut per game is possible. |
| FR-3 | B goes back on every screen. Menu (☰) opens the in-game quick menu. View (⧉) opens the keyboard. Nothing depends on the reserved Steam and Quick Access buttons. |

### Library

| ID | Requirement |
|----|-------------|
| FR-10 | Library folders are configured in the app (defaults: `~/ZX`, `~/Emulation/roms/zxspectrum`). Scanning runs in the background and is incremental (path + size + mtime). |
| FR-11 | Shelves: Games, Cassettes, Disks, Saves / Continue, Favourites, Recent. Filters on L1 / R1: All / Favourites / per machine. Search through the system keyboard. |
| FR-12 | A card shows title, year, publisher and machine (from the metadata DB when known, cleaned file name otherwise), with art (local, then cached online, then the first frame captured headless). |
| FR-13 | A disk card shows the TR-DOS catalogue (`TrdosCatalog`). A tape card shows the block list (`TapeCatalog`). |
| FR-14 | Machine profiles (model + ROM set + HDD / SD / CD images + sound devices) are separate items, not games; a game may name a machine profile. |
| FR-15 | "Add to Steam" for a single game writes a non-Steam shortcut and its art (phase D3; see [architecture.md §8](architecture.md#8-steam-integration)). |

### Running a game

| ID | Requirement |
|----|-------------|
| FR-20 | Choose the model from (1) the profile, (2) the metadata DB, (3) the file type (SPG → TS-Conf, ZXP → ZX-Poly), (4) the default (Pentagon 128 for disks, 128K for tapes). |
| FR-21 | Tapes load with fast loading by default; disks boot or run the selected file. |
| FR-22 | A quick menu (in-game overlay): resume, save / load state slots, swap tape / disk, controls, display (scale, crop, CRT), sound, machine (reset, NMI, turbo), quit. |
| FR-23 | Screen fit: integer scale (default), fit with aspect, 1:1, border crop (full / small / none), CRT pass on / off with presets. |
| FR-24 | Haptic click on OSK key presses; optional haptic tape-loading buzz. |

### Controls and profiles

| ID | Requirement |
|----|-------------|
| FR-30 | Every Deck control can be bound to: a ZX key or key chord, a Kempston / Sinclair 1 / Sinclair 2 / cursor / Fuller direction or fire, Kempston mouse (axes, buttons, wheel), an app action (OSK, radial menu, quick menu, rewind, fast-forward, screenshot, save / load slot). |
| FR-31 | Trackpads: mouse (relative, with ballistics), absolute touch (OSK cursors), radial menu, D-pad emulation. Sticks: digital with a dead-zone and 4 / 8-way options, or mouse. Gyro: mouse, only while a chosen button is held. |
| FR-32 | Profiles: built-in (defaults), community (bundled DB), user (local). Scope: game signature, else the machine, else global. User profiles override the others field by field. |
| FR-33 | Signature detection for TAP / TZX / TRD / SCL / FDI / UDI / SNA / Z80 / SZX / SPG ([input-and-profiles.md §4](input-and-profiles.md#4-signatures)). It is robust to re-packing a disk and to saves written to it. |
| FR-34 | Heuristic auto profile for unknown software: watch port reads (`#1F` Kempston, keyboard half-rows) during the first seconds and choose a scheme. |
| FR-35 | OSK: Spectrum 48K, Spectrum 128K / +2 and a compact layout. It has sticky CAPS / SYMBOL SHIFT, a keyword legend that follows the K / L / E / G mode, and two-trackpad and touch input. Host text input (search, names) uses the Steam keyboard where available. |
| FR-36 | Radial menu on a held trackpad or back button, with per-profile items. |

### State

| ID | Requirement |
|----|-------------|
| FR-40 | System suspend: on `PrepareForSleep(true)`, pause emulation, stop the audio device and flush the resume state. On resume, restart audio, then emulation. |
| FR-41 | On quit or game switch, write a resume state plus a thumbnail per game. "Continue" restores it, including the media state (inserted tape position, disks, dirty sectors). |
| FR-42 | Manual state slots (8 per game) with thumbnails. |
| FR-43 | Rewind (TTD) on a held button, with a timeline strip (D3). Replays: save and load a TTD session file (D3). |
| FR-44 | Guest writes to disk images go to an overlay, not to the library file. The user can "Write back" or "Export" explicitly. |

### Later phases

The music and video hubs, demoparty live mode, the debugger companion and the dashboards are
specified in [companion-and-media.md](companion-and-media.md).

## 8. Non-functional requirements

| ID | Requirement | Target |
|----|-------------|--------|
| NFR-1 | Input-to-photon latency (button → first changed pixel on the panel), 50 Hz panel | ≤ 40 ms, target ≈ 25 ms |
| NFR-2 | Frame pacing at a 50 Hz panel with a 50.02 Hz machine | no repeated or dropped frame in 10 min, audio drift corrected by DRC |
| NFR-3 | Cold start to library | < 1.5 s (library index cached) |
| NFR-4 | Library scroll | 60 fps with 10 000 items; covers decoded off the UI thread |
| NFR-5 | Power draw, 128K game, LCD Deck, 50 Hz | ≤ 1 W above idle desktop (Spectrum emulation is ≈ 2–3 % of one Zen 2 core) |
| NFR-6 | Suspend handling | state flushed in < 100 ms of `PrepareForSleep` |
| NFR-7 | Resume state write | < 50 ms for a 128K machine, < 300 ms for TS-Conf / Sprinter |
| NFR-8 | Legibility | smallest UI text ≥ 9 px tall at 1280×800 (Valve's Deck compatibility criterion), default 14–18 px |
| NFR-9 | Binary portability | runs under Steam Linux Runtime 4.0 (steamrt4, Valve's current recommendation) or 3.0 (sniper), and as a Flatpak; no host-library dependency beyond the runtime |
| NFR-10 | No warnings | gcc / clang / MSVC, like the rest of the repository |
| NFR-11 | Licensing | GPL-3.0 code base; any Steamworks use must be compatible with it ([§12 Q-1](#12-open-questions)) |

## 9. Phases

| Phase | Content | Depends on |
|-------|---------|------------|
| **D0** | Skeleton: SDL3 window + SDL_GPU, core linked, one frame on screen, audio, D-pad → Kempston. Linux and macOS builds. | — |
| **D1** | Player MVP: display-locked pacing, library with cards and catalogues, quick menu, OSK, suspend / resume state, state slots, default profiles. Deck Verified criteria. | D0 |
| **D2** | Profiles: mapping editor, signature DB, heuristics, radial menu, gyro, community DB import. | D1, signature library |
| **D3** | Time: rewind on a button, replays, "Add to Steam", Steam Input / Cloud / Timeline (if Q-1 allows). | D1, TTD v2 |
| **D4** | Companion: Deck as a second screen for a PC instance (TTD scrub, screen pages, Gigascreen phases, remote settings, video-wall control). | binary stream protocol |
| **D5** | Media hubs: music (all sound devices, playlists, demo-part snapshots), video / slideshow. | D1, metadata DB |
| **D6** | Demoparty live mode, validated submissions, reference TTD tracks. | D3, D5, headless validation service |

The proofs of concept that must pass before each phase are in [poc-plan.md](poc-plan.md).

## 10. Acceptance

| Phase | Accepted when |
|-------|---------------|
| D0 | A 128K game runs on a Deck in Game Mode from a non-Steam shortcut; frame time < 2 ms GPU, < 1 ms CPU besides emulation. |
| D1 | UC-1, 3, 4, 5, 6 pass on an LCD and an OLED Deck. NFR-1, 2, 3, 6, 8 are measured and recorded in a results table. |
| D2 | UC-2 and UC-7 pass; 50 known games start with correct controls from the bundled DB. |
| D3 | UC-8 and UC-9 pass; a replay recorded on a Deck plays identically in `unreal-qt`. |
| D4–D6 | See [companion-and-media.md](companion-and-media.md). |

## 11. Corrections to the source discussion

The discussion that started this design was with a general-purpose chatbot. These points were checked
against the code and the vendor documentation:

| Claim | Fact |
|---|---|
| "UNS snapshots" for exit and standby | unreal-ng has no `.uns`. It has SNA / Z80 / SZX save, SPG / ZXP load, the `.ttd` session file and the in-memory `MachineStateTransfer`. The resume state is a new container ([architecture.md §6](architecture.md#6-persistence)). On system suspend the process stays in RAM, so it does not need a snapshot at all, only a pause plus a safety flush. |
| Steam Input lets the user rebind "anything" | Only for a game with a Steam AppID that calls `ISteamInput`, or through Steam Input's gamepad / keyboard emulation for a non-Steam shortcut. A non-Steam shortcut gets no action sets. Hence two input back-ends ([input-and-profiles.md §2](input-and-profiles.md#2-two-back-ends)). |
| Shared memory / IPC between the front-end and the core | Not for the player. The core is linked in-process. A second process only costs latency. IPC belongs to companion mode. |
| `-march=znver2` | Allowed for a Deck-only build. The shipped binary targets `x86-64-v3` (AVX2 / FMA / BMI2), which Zen 2 supports, so the same binary runs on recent desktop CPUs. |
| ZEsarUX supports FT812 / VDAC2 | Not relied on; irrelevant here. |
| Gamescope FSR upscales a 256×192 output | It does. It is not used: integer scaling plus our own CRT pass is sharper and costs < 0.2 ms ([rendering.md §6](rendering.md#6-scaling-and-the-crt-pass)). FSR / NIS stay available to the user from the Quick Access menu. |
| Valve's Deck pages live under `/doc/steamdeck/` | They moved to `partner.steamgames.com/doc/steamhardware/` ("Steam Deck and Steam Machine"); [references.md](references.md) uses the new URLs. |
| Build in the sniper container | sniper (SLR 3.0) still works, but Valve's guide now recommends steamrt4 (SLR 4.0, Debian 13, newer SDL and Vulkan loader). |
| Haptics through `SDL_PlayHapticEffect` | In SDL3 the Deck's trackpad haptics are reached through `SDL_RumbleGamepad` (and Steam Input's `TriggerSimpleHapticEvent` when on Steam), not through the legacy haptic API. |

## 12. Open questions

| ID | Question | Recommendation |
|----|----------|----------------|
| Q-1 | Ship on the Steam store (AppID, Steamworks) or only as a Flatpak / non-Steam shortcut? Steamworks is proprietary; unreal-ng is GPL-3.0. | D0–D2 need no Steamworks. For D3, either add a GPL linking exception for `libsteam_api` (needs agreement from the copyright holders), or load it at run time from a separate optional module behind a narrow C interface. Decide before D3. |
| Q-2 | Resume-state format: extend SZX with unreal-ng chunks, or a new container? | A new container that embeds SZX where the model supports it, plus unreal-ng chunks and media references ([architecture.md §6](architecture.md#6-persistence)). |
| Q-3 | Community profile DB hosting and moderation | Start with a JSON file in the repository (`data/deck/profiles/`), reviewed by PR. |
| Q-4 | Name: `unreal-deck` (Deck-specific) vs `unreal-play` (any handheld / TV)? | Code and CMake target `unreal-deck`. Nothing in the design is Deck-only, and ROG Ally / Legion Go run the same binary. |
