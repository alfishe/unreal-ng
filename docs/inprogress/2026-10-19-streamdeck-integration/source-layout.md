# Stream Deck integration: source layout, names, dependencies

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | **Proposal, open**: the owner chooses the paths and names (D6 fixes only the grouping by role) |
| **Decision** | D6 in [README.md](README.md#decisions-owner-2026-10-19): clients, companions, plugins, utilities apart; `tools/` internal |

## Contents

- [1. Today](#1-today)
- [2. Grouping by role](#2-grouping-by-role)
- [3. The Stream Deck companion](#3-the-stream-deck-companion)
- [4. Names](#4-names)
- [5. Third party](#5-third-party)
- [6. Build options](#6-build-options)
- [7. Moving existing code](#7-moving-existing-code)
- [8. Open points](#8-open-points)

## 1. Today

| Path | Role |
|:--|:--|
| `core/` (`src`, `automation`, `controllers`, `recording`, `tests`, `benchmarks`) | Engine and host libraries |
| `unreal-qt/`, `unreal-screen-viewer/`, `unreal-videowall/` | Clients, at the root |
| `testclient/` | Developer console client (`BUILD_TESTCLIENT`, not built by default) |
| `unreal-ios/`, `unreal-deck/` (Steam Deck) | Planned clients at the root (`docs/inprogress/2026-09-19-ios-integration`, `2026-10-08-steamdeck-client`) |
| `core/automation/mcp/bridge` (`unreal-mcp-bridge`) | The stdio MCP helper: a separate executable shipped beside the apps |
| `tools/` | Mixed: internal (build, sanitize, verification, poc, profiling, bench, icons, controllers scripts, cd / chd fixture makers, ide), user utilities (diskconverter, diskinfo, porttrace, python monitors, neogs image tools, workload-monitor), plugins for other programs (wireshark extcap, ImHex patterns, IDA script), per-machine research (`tools/machines/*`) |
| `data/` | Runtime data shipped with the apps |
| `lib/` | Two git submodules (googletest, benchmark) |

## 2. Grouping by role

| Group | Holds | Test of membership |
|:--|:--|:--|
| `core/` | Engine and host libraries | Linked by clients; no `main` |
| clients, **at the root** (`unreal-*/`) | Programs that show and run emulation | Has instances or shows them |
| `companions/` | Paired processes that control a client through the WebAPI, and their own apps | Talks to a host as a companion; never runs an instance |
| `plugins/` | Extensions loaded **by other programs** | Their host is Wireshark, ImHex, IDA, Ghidra, an IDE |
| `utilities/` | Stand-alone programs and scripts for users, shipped or documented for users | A user runs it on their files or against a running emulator |
| `tools/` | Internal: build, test, verification, sanitize, POCs, profiling, generators | Only developers and CI run it |

```text
core/                      engine + host libraries (unchanged)
unreal-qt/                 clients stay at the root (owner, 2026-10-19)
unreal-screen-viewer/
unreal-videowall/
unreal-deck/               Steam Deck client (planned)
unreal-ios/                (planned)
testclient/
companions/
  streamdeck/              §3
  README.md                what a companion is, the pairing and surface protocols, the list
plugins/
  wireshark/               extcap (from tools/wireshark)
  imhex/                   patterns (from tools/imhex)
  idapro/                  (from tools/idapro)
utilities/
  diskconverter/  diskinfo/  porttrace/  workload-monitor/  python/  neogs/
tools/                     internal only
  build/ sanitize/ verification/ poc/ profiling/ bench/ icons/ controllers/ cd/ chd/ ide/ machines/
```

- Clients stay where they are **for now** (owner, 2026-10-19: moving them would take the build processes away for a
  while); they move later, all at once, into a clients group in one centralised change (CMake, CI, packaging, docs,
  recipes). Until then new clients go to the root as `unreal-<name>/`.
- `unreal-mcp-bridge` stays in `core/automation/mcp/bridge`: it is part of the MCP plane, not a companion (it relays
  an agent, never pairs).
- `tools/machines/*` (FPGA extraction, cosim, real-board scripts) is research: stays in `tools/`.
- The classification of each `tools/` entry is in [§7](#7-moving-existing-code); every row is a proposal.

## 3. The Stream Deck companion

```text
companions/streamdeck/
  CMakeLists.txt           option UNREAL_STREAMDECK (§6)
  README.md
  lib/                     libsdb: models, protocol, input, image, device, transports, wire, theme, draw, icons,
    include/sdb/  src/       svg, scenes, scenario (+ io, ops, render), navigator, actions, runtime, automation
  daemon/                  the daemon executable: multiplexer, surface client, UnrealHost, config, pairing
  studio/                  the Qt app: software deck, editor, recording glue
  sdctl/                   developer CLI (info, events, set-*, bench, hid-test, probe, scenes)
  3rdparty/                hidapi, JPEG encoder, cpp-httplib, nlohmann/json, fkYAML, rapidcsv, nanosvg, stb (§5)
  tests/                   unit tests (the 838 checks), wire and multiplexer tests, conformance against a host
  examples/                demos (python), generators
data/streamdeck/           shipped: scenarios/, themes/, catalogue.json (generated), thumbnails/
docs/companions/streamdeck/  user and protocol docs, from streamdeck-bridge/docs, streamdeck-emulator/docs, ../docs/hid
```

- The library split the daemon needs (decks over USB) from what the studio needs (scenario, render, wire) by
  targets, not by directories: `sdb-core` (no hidapi: models, protocol, image, scenario, navigator, render, wire),
  `sdb-hid` (USB transport, hidapi), `sdb-runtime` (runtime, actions, automation). The studio links `sdb-core` only.
- The code takes the project's style **on import** (owner, 2026-10-19): `docs/guidelines/coding-guidelines.md` naming
  (PascalCase methods, camelCase fields, no underscores in names), the project's file naming and `.clang-format`;
  one renaming pass, checked by the moved tests. No second style enters the repository.

## 4. Names

`unreal-deck` is taken by the Steam Deck client (`docs/inprogress/2026-10-08-steamdeck-client/README.md:1`, its
`BUILD_DECK_APP`, `org.unrealng.Deck`, `~/.config/unreal-deck/`). Free: `unreal-surface*`, `unreal-stream*`,
`unreal-companion*`.

| Part | Proposal | Alternatives |
|:--|:--|:--|
| Directory | `companions/streamdeck/` | `companions/elgato/` |
| Daemon executable | `unreal-streamdeck` | `unreal-sdd`, `unreal-surfaces` (if later it drives other process-side surfaces) |
| Studio app | `unreal-streamdeck-studio`, shown as "Stream Deck Studio" | "Deck Studio" (confusable with the Steam Deck client) |
| Developer CLI | `sdctl` (unchanged) | `unreal-sdctl` |
| Library targets | `sdb-core`, `sdb-hid`, `sdb-runtime` | `unreal-streamdeck-lib` |
| CMake option | `UNREAL_STREAMDECK` | `BUILD_STREAMDECK` (matches `BUILD_QT_APPS`, `BUILD_POC`) |

## 5. Third party

The project embeds sources and downloads nothing at configure time (`CMakeLists.txt:270-284`); `libsdb` fetches
hidapi and nlohmann/json (FetchContent) and needs libjpeg-turbo through pkg-config (Homebrew). All of them stay
**companion-only** (NFR-5: the core gets none).

| Dependency | Today | Proposal | Alternative |
|:--|:--|:--|:--|
| hidapi 0.15.0 | FetchContent | Vendor the source (`3rdparty/hidapi`: mac, windows, linux hidraw backends) | System package on Linux |
| libjpeg-turbo | pkg-config, Homebrew dylib | **Done (POC 032):** our encoder `sdb::jpeg` (from TooJpeg + libjpeg's arithmetic), baseline 4:4:4 (the firmware takes baseline 4:4:4 only; keys are ≤ 144 px, the strip 1200×100); decoding through stb_image | Vendor libjpeg-turbo without SIMD (large, CMake-heavy) |
| nlohmann/json 3.11.3 | FetchContent | Vendor (single header) | Port to jsoncpp (already in `core/automation/webapi/lib`): large change in `libsdb` |
| cpp-httplib 0.61.0 | vendored | Keep (automation server) | drogon: too heavy for a companion |
| fkYAML 0.5.0 | vendored | Keep, or port to rapidyaml (in `core/src/3rdparty`) to have one YAML library | |
| rapidcsv, nanosvg, stb_image | vendored | Keep | |
| WebSocket client | none | A small RFC 6455 client on the existing socket code (text frames, ping / pong, client masking) | A vendored single-file client |
| Qt 6 Widgets, Network | studio | The project's Qt (same version as unreal-qt) | |

Each one gets its row in `THIRD_PARTY_NOTICES.md`.

## 6. Build options

| Option | Default | Builds |
|:--|:--|:--|
| `UNREAL_STREAMDECK` | ON where `BUILD_QT_APPS` is ON | daemon, sdctl, tests; the studio when Qt is found |
| (inherits) `TESTS` | | adds the companion's tests to `core-tests`? No: its own `streamdeck-tests` target, run by `tools/build/test.sh --all` |

Zero warnings under the project's flags (the library was built only with AppleClang and its own flags: expect a
warning pass).

## 7. Moving existing code

The clients do not move with this work; their centralised move later touches CMake (`CMakeLists.txt`,
`cmake/LinuxPackaging.cmake`, `cmake/Sanitize.cmake`, `cmake/Lto.cmake`), CI (`.github/workflows/release.yml`),
20 non-doc files and 166 docs naming `unreal-qt/` paths, `AGENTS.md` and recipes (build outputs do not change).
With this work: create `companions/`, `plugins/`, `utilities/`; put
the Stream Deck companion in `companions/streamdeck/`; move the plugin and utility entries of `tools/` (table below)
in one mechanical commit of its own (`git mv` only, plus the paths in docs and recipes that name them), so
`git log --follow` stays readable.

| From `tools/` | To | Why |
|:--|:--|:--|
| `wireshark/` | `plugins/wireshark/` | Loaded by Wireshark (extcap) |
| `imhex/` | `plugins/imhex/` | Loaded by ImHex |
| `idapro/` | `plugins/idapro/` | Run inside IDA |
| `diskconverter/`, `diskinfo/` | `utilities/` | Users run them on disk images |
| `porttrace/` | `utilities/` | Users capture and convert port traces |
| `python/` (shared-memory monitors, discovery) | `utilities/python/` | Users run them against an emulator |
| `workload-monitor/` | `utilities/` | Users run it |
| `neogs/` (SD image, flash packer) | `utilities/neogs/` | Users build NeoGS media |
| `build/`, `sanitize/`, `verification/`, `poc/`, `profiling/`, `bench/`, `icons/`, `controllers/`, `cd/`, `chd/`, `ide/`, `machines/`, `symbolicate.py`, `fix-absolute-paths.py`, `package-appimage.sh` | stay | Internal |

## 8. Open points

| # | Question |
|:--|:--|
| L-1 | **Decided 2026-10-19:** clients stay at the root for now; new groups `companions/`, `plugins/`, `utilities/`. |
| L-2 | **Decided:** the clients move later, all at once, in one centralised change (the name of their group then). |
| L-3 | **Decided 2026-10-19:** daemon `unreal-streamdeck`, studio "Stream Deck Studio" (`unreal-streamdeck-studio`), directory `companions/streamdeck/`, `sdctl` kept ([§4](#4-names)). |
| L-4 | **Decided 2026-10-19:** the project's style on import. |
| L-5 | **Decided 2026-10-19:** imported as new code, no history, no reference to where the prototype lived; only what is needed ([migration.md §1](migration.md#1-manifest)). |
| L-6 | **Decided 2026-10-19:** a small vendored baseline 4:4:4 encoder, after a measurement against turbojpeg on the real + XL (speed, frame size); vendored libjpeg-turbo if it is much worse. **Measured 2026-10-20 ([POC 032](../../../tools/poc/032-streamdeck-jpeg-encoder/README.md)): our own encoder `sdb::jpeg`** (`lib/src/jpeg.cpp`, ~470 lines): TooJpeg's structure (zlib, notice kept) with libjpeg-turbo's techniques (buffered 64-bit bit writer, bit-mask zero runs, fixed-point colour conversion, islow integer DCT, reciprocal quantisation, auto-vectorised). Its scan data equals turbojpeg's on every test image and quality (same bytes -16 B of headers, same PSNR); a whole + XL frame (36 keys + strip) encodes in 2.37 ms vs 1.73 ms (1.37x; the frame's USB transfer is ~90 ms); +17.5 KB in `unreal-streamdeck`, the system dylib and its macOS 15 link warning gone. Vendored TooJpeg (2.1x), jpge (2.2-5.6x or +13-19 % bytes) and stb_image_write lost; NEON / SSE2 not worth it (0.7 % of a frame) |
