# Stream Deck integration: import from the prototype

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Decision** | L-5 (owner, 2026-10-19): the code comes in **as new code**: no history, no reference to where the prototype lived, **only what is needed** |

The prototype is two directories outside the repository: `streamdeck-bridge/` (library `libsdb`, CLI `sdctl`, tests,
examples) and `streamdeck-emulator/` (software deck, editor), plus a protocol reference `docs/hid/`. Nothing else of
the repository they sat in is used.

## Contents

- [1. Manifest](#1-manifest)
- [2. Cleaning on import](#2-cleaning-on-import)
- [3. What changes after the import](#3-what-changes-after-the-import)
- [4. REST paths to action ids](#4-rest-paths-to-action-ids)
- [5. Order](#5-order)

## 1. Manifest

Tracked files only (`build/`, `.cache/`, `__pycache__/`, `.DS_Store` are never copied). Targets per
[source-layout.md §3](source-layout.md#3-the-stream-deck-companion).

### 1.1 Copied

| From | To | Note |
|:--|:--|:--|
| `streamdeck-bridge/include/sdb/*.hpp` (21) | `companions/streamdeck/lib/include/sdb/` | |
| `streamdeck-bridge/src/*.cpp` (21) | `companions/streamdeck/lib/src/` | `actions.cpp` unreal-ng part and `transport_tcp.cpp` are rewritten later (§3) |
| `streamdeck-bridge/tools/sdctl.cpp`, `font5x7.hpp`, `setcmds.cpp`, `scenecmd.cpp`, `bench.cpp`, `hidtest.cpp`, `lcdtest.cpp` | `companions/streamdeck/sdctl/` | developer CLI, **SD1, done**; `run`, `pair`, `scenario`, `bridge`, `ttd-touch` left it |
| `streamdeck-bridge/tools/runcmd.cpp`, the pairing command of `bridge.cpp` | `companions/streamdeck/daemon/` (`runcommand.cpp`, `pair.cpp`, a new `main.cpp`) | `unreal-streamdeck run \| pair \| scenario`, **SD1, done** |
| `streamdeck-bridge/tools/cli.hpp`, device selection of `sdctl.cpp` | `companions/streamdeck/cli/` | shared by sdctl and the daemon, **SD1, done** |
| `streamdeck-bridge/tests/*` (9) | `companions/streamdeck/tests/` | the 838 checks |
| `streamdeck-bridge/third_party/` httplib, fkYAML, nanosvg, nanosvgrast, rapidcsv, stb_image, README | `companions/streamdeck/3rdparty/` | rows in `THIRD_PARTY_NOTICES.md` |
| `streamdeck-bridge/examples/themes/unrealng/` (30 files) | `data/streamdeck/themes/unrealng/` | boot / idle theme |
| — | — | the example scenarios are **not** needed as fixtures: the tests write their own files |
| `streamdeck-bridge/examples/make_unrealng_control.py`, `capture_zx.py` | `companions/streamdeck/tools/` | the default-scenario generator and its thumbnail capture: **SD5**, rewritten for v2 when the catalogue exists (they read the REST presets, which are not imported) |
| `streamdeck-emulator/src/` main, mainwindow, deckwidget, virtualdeck, emulatorapi, recorder, gui.h, textedit.h | `companions/streamdeck/studio/` | **SD3**: imported when rewritten as a wire v2 client (without `server` and `usblistener` it does not build) |
| `streamdeck-emulator/src/editor/*` (14) | `companions/streamdeck/studio/editor/` | **SD3**; `presets.cpp`: generic presets only |
| `docs/hid/*` (8) | `docs/companions/streamdeck/hid/` | the protocol reference, checked against Elgato's docs and hardware |
| `streamdeck-bridge/docs/PERFORMANCE.md` | `docs/companions/streamdeck/performance.md` | device measurements only |
| `streamdeck-emulator/docs/scenario-editor/01-04` | `docs/companions/streamdeck/scenario-editor/` | **SD3**, rewritten to this design (daemon, catalogue) |
| `streamdeck-bridge/README.md`, `streamdeck-emulator/README.md` | `docs/companions/streamdeck/` (`sdctl.md`, `studio.md`) | rewritten |

### 1.2 Not copied

| What | Why |
|:--|:--|
| `docs/01-08`, `docs/README.md` | Notes on the other application's internals (architecture, image pipeline, animation, presets); the protocol parts are superseded by `docs/hid/` |
| `streamdeck-bridge/tools/bridge.cpp` | Legacy 500 ms poller (`sdctl bridge`); pairing moves into the daemon |
| `streamdeck-bridge/tools/ttdtouch.cpp` | TTD touch-strip experiment over REST polling; the TTD strip comes from readouts |
| `streamdeck-bridge/tools/mcp-bridge/` | Duplicates `unreal-mcp-bridge` |
| `streamdeck-bridge/examples/*.py` demos (`unrealng_demo`, `unrealng_effects_demo`, `capture_screens`, `navigation_tour`, `mcp_editor_demo`, `make_bridge_config`, `make_scenario`, `_deck.py`) | Demos of the REST flows and the legacy bridge; screenshots for the old README |
| `streamdeck-bridge/examples/screens/*.png` (17) | Screenshots for the old README; the new docs take new ones |
| `streamdeck-bridge/examples/scenarios/unrealng-control.deck.json` | Generated, 98 absolute paths, REST steps; regenerated as v2 into `data/streamdeck/scenarios/` |
| `streamdeck-emulator/src/recording/` (33) | Duplicates `core/recording`; the studio links it |
| `streamdeck-emulator/src/usblistener.*` | The daemon is the only USB owner |
| `streamdeck-emulator/src/server.*` | The studio becomes a client (wire v2); kept only as the test `--listen` mode if needed, written new |
| `streamdeck-emulator/presets/` (`unrealng.json`, `unrealng-source.json`, `gen_unrealng.py`) | 405 REST presets; the palette comes from the catalogue |
| `streamdeck-emulator/docs/scenario-editor/05-unrealng-actions.md` | Generated from the REST presets |
| `.gitignore` files, the two `CMakeLists.txt` | The project's build replaces them |

## 2. Cleaning on import

| What | Rule |
|:--|:--|
| Where it came from | No mention of the prototype's location, the application it sat beside, its commits or a source SHA, in code, comments, docs or the commit message. |
| Comparisons with other software | Performance notes keep device facts (report cost, frame rates by content); the comparison with another application's frame rate goes. "Quit other Stream Deck software first" replaces named applications. |
| Absolute paths | None: `UNREAL_NG` defaults, `TESTDATA` constants, `SDECK_SOURCE_DIR` go; scenario paths use roots ([scenarios.md §3](scenarios.md#3-paths)). |
| Duplicated unreal-ng code | Not imported (recording, MCP bridge); the new code links the originals. |
| Names | `streamdeck-emulator` → Stream Deck Studio (`unreal-streamdeck-studio`), bundle id `dev.unrealng.streamdeck-studio`; config dirs under the unreal-ng config dir, not `~/.config/streamdeck-bridge` / `streamdeck-emulator`. |
| Generated data | Not imported; regenerated by the moved generators from the catalogue. |
| Style | The project's style on import (L-4): coding-guidelines naming, file names, `.clang-format`; one renaming pass verified by the moved tests. Done in SD1 with clang-tidy `readability-identifier-naming` (functions and methods PascalCase, fields and variables camelCase, private fields `_x`; constants `kX` and enum values kept, as in the core), names that then collided with a type renamed by meaning (`GetModel()`, `ParseSlot()`, `StatusScene()`, ...), then clang-format. |
| Build settings | The project's: standard, build type, LTO, the macOS 12.0 deployment target from the root, the core's warnings (`-Wall -Wextra -Werror`) on our sources. `std::format` (its floating point formatter needs macOS 13.3) became `sdb::Format` (`lib/include/sdb/format.h`, the same `{}` syntax, the specs the code uses). |
| Headers | The project's file header convention, if any, on every file. |

A check before the import commit: `git grep -n -i` over the new files for the prototype's location, the other
application's name, `/Volumes/`, `/Users/`, `/tmp/` must return nothing (plus `tools/fix-absolute-paths.py`).

## 3. What changes after the import

| Area | Change | Doc |
|:--|:--|:--|
| unreal-ng calls | `webapi` REST steps → `action` ids; the `UnrealHost` on the surface socket | [daemon.md §5](daemon.md#5-runtime-changes), [scenarios.md §2](scenarios.md#2-the-action-kind) |
| State | Polling and local toggles → readouts and state-bound faces | [scenarios.md §4](scenarios.md#4-state-bound-faces) |
| Instance choice | `GET /api/v1/emulator` + `symbolic_id == "unreal-qt"` → host targeting | [targeting.md](targeting.md) |
| Device ownership | Shared open + `/tmp` lock + the studio's own USB reader → the multiplexer, exclusive open | [daemon.md §3](daemon.md#3-device-multiplexer) |
| Wire | The studio listens, v1 → the daemon listens, v2 | [device-wire.md](device-wire.md) |
| Dependencies | FetchContent hidapi + json, Homebrew turbojpeg → vendored | [source-layout.md §5](source-layout.md#5-third-party) |
| Portability | POSIX sockets, `getppid`, `flock` → Winsock, process watches, no lock | [daemon.md §12](daemon.md#12-platforms) |
| Build | The prototype's CMake and flags → the project's flags, zero warnings, all compilers | [source-layout.md §6](source-layout.md#6-build-options) |
| Docs drift | The old automation doc misses 10 routes; the old technical design names editor classes that do not exist; the old README says "200+ checks" (838) and its wire table misses 0x06-0x09 | fixed in the rewritten docs |

## 4. REST paths to action ids

The prototype's control scenario uses 123 distinct REST paths in 598 steps. By group (step counts):

| Group (steps) | Today | Then |
|:--|:--|:--|
| Machine start / replace (90) | `POST /api/v1/emulator/start {replace}` + delays | `mach.start` ([protocol.md §6](protocol.md#6-machine-lifecycle-without-rest)) |
| Keyboard (66) | `/keyboard/*` (type, press keys, ZX and PC keys) | `input.key` `{key, hold?}`, `input.type` `{text}` |
| Features (56) | `/feature/<name>` | `feature.toggle` `{name}` with `toggle.feature.<name>` readouts |
| TTD (55) | `/ttd/*` | `ttd.*` (seek_by, steps, bookmarks, record, resume_live) |
| Tape (52) | `/tape/*` | `media.tape` `{op}`, `media.open` |
| Audio (44) | `/audio/mixer/*` | `audio.mute_toggle`, `audio.volume_by` (delta), `audio.source_mute` `{source}` |
| Settings (43) | `PUT /settings/<name>` | `set.speed` `{value}`, `set.toggle` `{name}` (turbo, fast tape, turbo tape, fast disk) |
| BASIC (36) | `/basic/*` | `basic.*` (enter, run, list) |
| Joystick (28) | `/joystick/*` | `input.joystick` `{dir, fire, hold}` |
| Snapshot (26) | `/snapshot/*` | `snapshot.quick`, `media.open` |
| Run (≈ 40) | `/pause`, `/resume`, `/reset`, `/nmi`, `/run_frames`, `/run_scanlines`, `/run_to_interrupt`, `/run_tstates`, `/skip_until`, `/step*`, `/stop`, `/start`, `/state` | `run.*`, `mach.reset`, `mach.nmi`, `run.frames` `{count}`, `run.tstates` `{count}` |
| Media (16), Disk (10) | `/media/X/*`, `/disk/X/insert` | `media.open`, `media.eject` `{drive}` |
| Video (15) | `/video/*` | `video.*` (filters, overscan, fullscreen) |
| Debug (≈ 12) | `/debugmode`, `/breakpoints`, `/debug`, `/coverage`, `/profiler`, `/memory` | `dbg.mode_toggle`, `dbg.*` |
| Capture (2) | `/capture/screen` | `cap.screenshot` |
| Control (1) | `/control/session` | gone (companion token) |

- The exact list is written with the registry work (control surfaces S1); this table sizes it: about 40 new actions,
  most with a value, many thin wrappers over a control object the route already calls.
- Every route keeps working for automation; the actions call the same control objects.

## 5. Order

1. Import the manifest (§1.1) into `companions/streamdeck/`, cleaned (§2), under `UNREAL_STREAMDECK=OFF` by default;
   its tests pass; nothing else in the tree changes.
2. Vendor the dependencies, build with the project's flags (zero warnings) on macOS; then Windows and Linux.
3. Device multiplexer + wire v2 + the studio as a client.
4. Surface client (needs the core's `SurfaceHub`: control surfaces S1-S4 plus [protocol.md](protocol.md)); the
   `action` kind; readouts; default scenario v2.
5. Supervision in unreal-qt, settings page, packaging; `UNREAL_STREAMDECK=ON`.
