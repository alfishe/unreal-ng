# Stream Deck integration: requirements

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Decisions** | D1-D6 in [README.md](README.md#decisions-owner-2026-10-19) |

## Contents

- [1. What exists](#1-what-exists)
- [2. Problem](#2-problem)
- [3. Goals](#3-goals)
- [4. Use cases](#4-use-cases)
- [5. Functional requirements](#5-functional-requirements)
- [6. Non-functional requirements](#6-non-functional-requirements)
- [7. Non-goals](#7-non-goals)
- [8. Acceptance](#8-acceptance)

## 1. What exists

### 1.1 The prototype (outside the repository)

| Part | What | Facts |
|:--|:--|:--|
| `streamdeck-bridge/` `libsdb` | Static C++20 library: 16 Elgato models, report builders (golden-tested), input parser, image pipeline (JPEG 4:4:4 via turbojpeg, BMP), `Device`, transports, theme, scenes, icons, scenario model / ops / io, navigator, renderer, actions, runtime, automation server (REST + OpenAPI + MCP) | 21 sources; `libsdb.a` 4.9 MB; 838 unit checks pass; macOS + Stream Deck + XL FW 1.0.2.2 only |
| `streamdeck-bridge/tools` `sdctl` | CLI: `info`, `events`, `set-*`, `lcd-scene`, `bench`, `pair`, `bridge` (legacy), `run <scenario> --watch --api N --with-parent` | 2.7 MB |
| `streamdeck-bridge/tools/mcp-bridge` `sdeck-mcp-bridge` | stdio ↔ HTTP MCP pipe | duplicates unreal-ng's `unreal-mcp-bridge` |
| `streamdeck-emulator/` | Qt 6 (Widgets, Network) software deck: device side of the HID protocol over TCP 37050 (it listens, one host, newest wins), all 16 models, bezel, dials, touch window; protocol log; video recording; scenario editor (tree, canvas, inspector, palette, undo); WebAPI + MCP on 37052; spawns `sdctl run` for preview | 3.4 MB binary; links `libsdb` (so hidapi); **its `UsbListener` opens the real deck itself** (shared open), draws press highlights on it, sets its brightness |
| `streamdeck-emulator/src/recording` | Video encoders | duplicates unreal-ng's `core/recording` with three stand-in headers |
| `streamdeck-emulator/presets/unrealng.json`, `gen_unrealng.py` | 405 palette presets in 20 categories, generated from a live unreal-ng's OpenAPI | every preset is a REST method + path |
| `streamdeck-bridge/examples/scenarios/unrealng-control.deck.json` | The unreal-ng control scenario: 32 menus, 368 actions, depth 3 | 598 `webapi` steps over ~140 REST paths; 98 absolute paths of the owner's machine |

How the runtime talks to unreal-ng today (`streamdeck-bridge/src/actions.cpp`):

- Every action is a REST call on a **new TCP connection** (`Connection: close`, 4 s timeout), relative paths prefixed
  with `/api/v1/emulator/{id}`.
- The instance is resolved from `GET /api/v1/emulator` (`symbolic_id == "unreal-qt"` or the first one), re-resolved
  on 404, followed through `new_emulator_id` / `model_switched`.
- With a companion token (from `sdctl pair`, `~/.config/streamdeck-bridge/companion.json`): `Authorization: Bearer`.
  Without: three requests per action (open a control session, call, close it).
- A monitor thread polls `GET …/ttd/position` at **50 Hz** and `…/ttd/status` every 500 ms while the page's strip shows
  the TTD; it shares one mutex with the actions, so a slow poll delays a key press and the reverse.
- "Replace machine" is `DELETE` + `POST /api/v1/emulator/start` with the inherited `symbolic_id`.

### 1.2 In unreal-ng (master `cb0d645a5`)

| Part | State |
|:--|:--|
| Companion clients | Landed (`48e0af786`): pairing at the GUI, `unc_` bearer tokens, 7 scopes, `held_by_session` / `not_now`, `GET /api/v1/companions/me` with the 27-action catalogue, the events WebSocket `/api/v1/companions/events` (`CompanionEventHub`). Pairing works only in unreal-qt (503 elsewhere; `UNREAL_COMPANION_TOKENS` for headless). |
| Controller manager | Landed: `core/controllers` (backends, `InputRouter`, YAML profiles, the device database), Surface Dial input + clicks on macOS, input on Linux. The `ActionRegistry` lives in `unreal-qt/src/controllers` and runs on the GUI thread. |
| Control surfaces | **Designed, not landed** (`control-surfaces-design`): the core `ActionRegistry` with one worker per instance, values (press / delta + rate / text), `ContextState`, the TTD published cursor, `SurfaceHub` on `/api/v1/surfaces/ws` (`join` / `action` / `result` / `ctx` / `readout`), haptic marks. |
| Windows | One `MainWindow` per unreal-qt process with one `EmulatorBinding`; `unreal-videowall` is a separate process with a focused tile; both start a WebAPI on 8090 (the second one runs without it). `EmulatorManager::SetSelectedEmulatorId` exists, the Qt client never calls it. No window id on the WebAPI. |
| Dependencies | Source embedded in the tree, no download at configure time (`CMakeLists.txt:270-284`). No hidapi, no libjpeg-turbo. drogon + jsoncpp in the WebAPI, rapidyaml in the core. |

## 2. Problem

1. **Two sources of truth.** The deck computes TTD positions, toggles and the machine's run state by polling; the
   Dial, the pads and the GUI act on the core directly. A Dial scrub does not show on the deck's strip until the next
   poll; a deck page change cannot change what the Dial does.
2. **REST coupling.** Scenarios hold REST paths, response shapes and instance-id rules. Any route change breaks
   them silently; the palette has to be regenerated against a running emulator.
3. **Two USB owners.** The emulator and the runtime open the same deck (shared open on macOS, a `/tmp` lock against
   interleaved images, POSIX-only). The emulator's view of the deck depends on what it reads itself, not on what the
   runtime decided.
4. **Out of tree.** The code lives outside the repository, with duplicates of unreal-ng code
   (recording, the MCP bridge), absolute paths, Homebrew and FetchContent dependencies, untested on Windows / Linux.
5. **Polling load.** 50 Hz HTTP polling on a fresh connection per request is the pattern that already caused a
   "only with the companion connected" bug (2026-10-18).

## 3. Goals

| # | Goal |
|:--|:--|
| G-1 | Take any controller and see the same state change everywhere: deck keys and strip, Dial clicks, GUI. |
| G-2 | All inputs go through the controller manager and the core action registry; the deck calls actions by id. |
| G-3 | The real deck and any number of software decks show the same thing (mirror) or different models, without opening USB twice. |
| G-4 | Ship as part of unreal-ng: one repository, one release, the default scenario works out of the box. |
| G-5 | Work with many windows and instances: the deck drives the instance the user is looking at, or the one it is pinned to, and shows which. |
| G-6 | No polling of the emulator by companions: push only. |

## 4. Use cases

| # | Use case |
|:--|:--|
| UC-1 | Scrub the TTD with the Surface Dial; the deck's strip follows the position at up to 50 Hz and shows the marks crossed. |
| UC-2 | Open "TTD · time code" on the deck; the Dial switches to seconds before the next turn. |
| UC-3 | Pause from the game pad; the deck's run key changes its face and flashes (activity); optionally the strip says "PAUSE · pad". |
| UC-4 | Open the debugger in the GUI; the deck goes to its Debug page (context `debug`), the Dial gets its `@debug` layer. |
| UC-5 | Two windows (or two instances in a video wall); click the other one: the deck, the Dial and the pads now drive it; the deck shows its model and index. |
| UC-6 | Pin the deck to instance #1 (a game) while the Dial follows focus into instance #2 (debugging). |
| UC-7 | No hardware: open the studio, use the software deck exactly as the real one. |
| UC-8 | With the hardware: the studio mirrors the real deck, lights presses made on it, records a video of the session. |
| UC-9 | Edit a scenario in the studio; the running deck reloads it and keeps the menu stack. |
| UC-10 | An agent drives the deck and the editor over MCP (navigate, press, screenshot) and the emulator over its own MCP, on one machine. |
| UC-11 | Headless instance (CI, a remote host): the daemon pairs with a token from `UNREAL_COMPANION_TOKENS` and drives it. |

## 5. Functional requirements

| # | Requirement |
|:--|:--|
| FR-1 | The daemon is the only process that opens a Stream Deck over USB. |
| FR-2 | The daemon listens for software decks; each connects, says which role it takes (mirror of a real deck, or its own virtual deck) and gets the same output reports a real deck gets. |
| FR-3 | Input from a real deck and from every mirroring software deck reaches the same runtime; a software deck sees presses made on the real deck. |
| FR-4 | The daemon is a paired companion on the WebSocket `/api/v1/ws` (one socket, topics `surface` and `instances`); scenarios call **action ids with values**; the REST kind stays: REST is a first-class API, not a fallback. |
| FR-5 | The daemon subscribes to readouts (`ttd.position`, `run.state`, toggles' states) and draws from them; it never polls. |
| FR-6 | A deck page may name a context; entering it sets the context (`ctx.set`); a `ctx` from the core navigates the deck to the page bound to that context, without echo. |
| FR-7 | The core publishes an **activity** readout: action id, origin kind, surface name, result; the deck may show it (flash a key, a strip line); it never executes it. |
| FR-8 | The deck follows the target of [targeting.md](targeting.md): focus by default, a pin on request, and shows the current target. |
| FR-9 | unreal-qt starts the daemon when a deck is attached (setting: off / when a deck is attached / always), restarts it after a crash (bounded), stops it on exit, shows its state in the controllers settings. |
| FR-10 | The studio's palette is built from the live action catalogue (`GET /api/v1/controllers/actions`), grouped and described by the core. |
| FR-11 | Scenarios carry no absolute paths: data, icons and test software resolve through named roots. |
| FR-12 | The default scenario ships with unreal-ng and works against a fresh install. |
| FR-13 | Pairing from the daemon works from the deck itself (code on the strip) and from the studio. |
| FR-14 | Everything the studio and the daemon do is scriptable over WebAPI / MCP, as today. |

## 6. Non-functional requirements

| # | Requirement |
|:--|:--|
| NFR-1 | Deck key press → action submitted ≤ 10 ms on the host; readout → strip redraw ≤ 25 ms (one 50 Hz tick + a region write). |
| NFR-2 | A companion connected at 50 Hz readouts adds no measurable time to the emulation thread (readouts come from the published cursor; a polled or pushed read never holds the CPU or loads the emulation thread). |
| NFR-3 | The daemon has no Qt; it builds on macOS, Windows (MSVC, MinGW), Linux (gcc, clang) with zero warnings. |
| NFR-4 | No download at configure time: every third-party source is in the tree, with its licence in `THIRD_PARTY_NOTICES.md`. |
| NFR-5 | The core gets no new dependency (hidapi and the JPEG encoder are the companion's only). |
| NFR-6 | A daemon crash or hang never affects the emulator; a host restart never needs a daemon restart (it reconnects). |
| NFR-7 | Input is never journaled from the daemon: actions are host commands (TTD determinism, control-surfaces §12). |
| NFR-8 | The WebSocket hub uses asynchronous I/O on the WebAPI's existing event loops: no thread per connection, topic or hub; the host's thread count does not grow with clients; the daemon runs all its networking on one event-loop thread ([protocol.md §0.1](protocol.md#01-asynchronous-io-a-fixed-number-of-threads)). |

## 7. Non-goals

- Rendering deck faces in the core or sending images through the surface protocol (the daemon renders).
- Raw deck controls bound by core profiles in v1 (later: [TODO.md](TODO.md#later)).
- Replacing the Elgato app for other applications; the daemon is for unreal-ng.
- Driving the deck from several daemons at once.
- Network play or a deck on another machine than the daemon (the daemon may still talk to a remote WebAPI).

## 8. Acceptance

| # | Check |
|:--|:--|
| ACC-1 | UC-1 on a real Stream Deck + XL and a Surface Dial: 60 s of scrubbing, the strip and the Dial result show the same frame at every rest; the daemon sends no HTTP request in that time. |
| ACC-2 | UC-2 and UC-4: a context change from either side switches the other before its next input. |
| ACC-3 | UC-5 / UC-6 with two instances: the target switches on focus, the pin holds, the deck shows the target. |
| ACC-4 | Real deck + two studio windows (one mirror, one other model): the mirror is pixel-identical to the deck's last images; presses from any of them reach the runtime once. |
| ACC-5 | The default scenario runs on a fresh install of each platform package; no absolute path, every action id known to the catalogue. |
| ACC-6 | Killing the daemon during a scrub: emulation keeps its frame time; unreal-qt restarts the daemon; the deck comes back on the same page. |
| ACC-7 | The conformance suite (a virtual deck + a virtual companion) passes headless in CI on all three OSes. |
