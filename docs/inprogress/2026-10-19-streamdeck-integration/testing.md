# Stream Deck integration: testing

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Acceptance** | ACC-1 … ACC-7 in [requirements.md](requirements.md#8-acceptance) |

## Contents

- [1. Today](#1-today)
- [2. Unit](#2-unit)
- [3. Conformance](#3-conformance)
  - [3.1 Device wire and daemon routes (hardware-free, in CI)](#31-device-wire-and-daemon-routes-hardware-free-in-ci)
  - [3.2 With a host (needs SD7)](#32-with-a-host-needs-sd7)
- [4. Live](#4-live)
- [5. Hardware](#5-hardware)
- [6. Cross-platform](#6-cross-platform)
- [7. Load](#7-load)

## 1. Today

- `streamdeck-tests` (`companions/streamdeck/tests/`, own check macros, no GTest): 1290 checks, about 1 s: models,
  report bytes (golden), parser, image, mapping, theme, scenes, scenario (parse, ops, io, render), navigator, actions
  and runtime with a fake host, WebSocket client, multiplexer (`deckhub_test.cpp`), daemon decks and routes
  (`daemondecks_test.cpp`), hot plug, and the wire v2 conformance suite (`conformance_test.cpp`, [§3.1](#31-device-wire-and-daemon-routes-hardware-free-in-ci)).
- `streamdeck-studio-tests` (`companions/streamdeck/studio/tests/`, offscreen): 109 checks on macOS and Linux, 105 on
  Windows.
- Both run in CI on every push and pull request (`.github/workflows/cmake-ci.yml`, [§6](#6-cross-platform)).
- Not covered: the unreal-ng HTTP code (resolve, replace, control sessions, token, `ttdNow`, the instance follow), the
  lock file.

## 2. Unit

| Area | Tests |
|:--|:--|
| Library | The 838 checks, moved; run as `streamdeck-tests` |
| Multiplexer | Fake HID transport + fake viewers: fan-out identical bytes, input merge with per-source parsers, late-join replay equals the deck's last picture, slow viewer drops images and never blocks the deck, unplug / replug keeps the page |
| Wire v2 | Frame round trips, `Join` roles, 32-bit lengths (a `HostPage` > 64 KB), `Welcome` errors |
| Surface client | Against an in-process fake hub: join, subscribe, action / result by `rid`, delta merge before send, reconnect with pin and resubscribe, `revoked` |
| Runtime | `action` kind, state-bound faces from readouts, context → navigation without echo, activity flashes, target line |
| Core side | `TargetState` rules 1-6 and lifetime ([targeting.md](targeting.md)); activity merge and rate; readout vocabulary; catalogue fields (in `core-tests`, `*Surface*`, `*Target*`). Done SD4 2026-10-20: `TargetState_Test`, `TargetPin_Test`, `TargetStateNotification_Test`, `ActivityFeed_Test`, `ActionCatalogue_Test`, `SurfacesApi_Test`, `McpActions_Test.SurfaceVerbsBecomeTheSurfacesRoutes`; unreal-qt-tests `SurfaceTargets_Test` (the window as a view, the status icon, the surfaces table); live `tools/verification/mcp/test_surfaces.py` `test_no_id_follows_the_focused_view_and_pins_hold`. The per-session rate (20 / s) is the topic's (S4) |

Rules of `core/tests/README.md` apply on the core side (no `sleep_for`, ≤ 50 ms, `TestWait`).

## 3. Conformance

### 3.1 Device wire and daemon routes (hardware-free, in CI)

A scripted peer on loopback against the real `DeckHub` and `DaemonDecks`, fake USB decks
(`companions/streamdeck/tests/conformance_test.cpp`, shared helpers in `tests/hubpeer.h`); the multiplexer's own
cases stay in `deckhub_test.cpp`, the route basics in `daemondecks_test.cpp`. Every case takes under 50 ms (each
conformance case is timed; one over the budget is named in the output, not failed), no `sleep_for`: the test runs its
own event loop until the condition holds.

| Rule ([device-wire.md](device-wire.md)) | Covered by |
|:--|:--|
| Frames: `[u8][u32 LE][payload]`, a stream cut anywhere, `kMaxFrame` accepted, one byte more breaks the reader for good | `TestWireCodec` (deckhub), `FrameBoundaries` |
| 32-bit lengths: a `HostPage` over 64 KB, live and in the replay | `FrameBoundaries`, `ReplayFraming` |
| `Join` roles: mirror `first` / by serial / no deck field, own with a serial, a taken serial replaced, the default model; `Welcome` fields (`v`, `deck`, `model` (the token, every model's token reads back), `model_name`, `pid`, `real`, `daemon`) | `JoinRoles`, `TestMirror`, `TestOwnDeck` (deckhub) |
| `Welcome` errors, then the connection closes: `bad_request` (not JSON, not an object, a field of the wrong type), `version` (missing, 1, 3, a string), `bad_role` (missing, unknown), `bad_model`; `no_deck` with `waiting` keeps it open; a frame before `Join` closes without a `Welcome`; no `Join` within the timeout closes | `WelcomeErrors`, `TestJoinRules`, `TestMirrorWaitsForDeck` (deckhub) |
| Replay: `Welcome`, `ReplayBegin`, the state (brightness) first, the key chains, the page, `ReplayEnd`, nothing else in between; the replayed reports are the deck's own; live frames only after it, in the deck's order | `ReplayFraming`, `TestPictureCache`, `TestMirror` (deckhub) |
| Fan-out: two mirrors get the same bytes | `ReplayFraming` |
| Unknown frame types after `Join` are ignored | `JoinRoles` |
| `DeckState` on unplug / replug; a mirror of "the first deck" follows a new deck (new `Welcome`), a pinned one waits | `TestMirror`, `TestHubReconnect`, `TestMirrorFollowsFirstDeck` (deckhub) |
| Input: several mirrors merge into the runtime, each in its order; the real deck's input relayed as `HostInput` | `InputMerge`, `TestMirror` (deckhub) |
| `GetFeature` / `FeatureReply`: answered by an own viewer; unanswered: -1 after the timeout, the full requested length sent; a stray reply not taken for the next request; a mirror's reply ignored (the real deck answers) | `TestOwnDeck` (deckhub), `OwnFeatureTimeout`, `OwnFeatureReplyAndLeave`, `InputMerge` |
| `Leave`: a mirror's closes only its connection; an own viewer's (or a vanished one, no `Leave`) ends its deck (`onOwnDeckGone`, the transport closed); a waiting request ends at once | `Leave`, `OwnFeatureReplyAndLeave`, `TestOwnDeck` (deckhub) |
| Oversize frame (length > `kMaxFrame`) from a mirror, an own viewer or before `Join`: closed, the deck and other viewers go on | `OversizeFrame` |
| Slow viewer: images dropped, feature reports kept, a replay when it drains; a fast neighbour gets every frame and no extra replay; its input still arrives; the deck's writes never wait | `TestSlowViewer` (deckhub), `SlowViewerBesideFast` |
| `Control` to the deck's viewers only | `TestHubInfo` (deckhub), `DaemonRoutesOverHub` |
| Routes over a live hub: `/health` (decks, viewers, waiting mirrors, wire port), `/decks` (real, own, a deck only the hub has: `runtime: false`, unplugged), `/decks/{serial}`, `snapshot` (200 to its viewers, 404, 409 nobody views / no hub), `reconnect` (200, 400, 404, 409 own deck), `/state` `/page` `/nodes` `/scenario` by `?deck=`, `/press` `/navigate` refusals, MCP `invoke_api` | `DaemonRoutesOverHub`, `TestHubWithDaemonDecks`, `TestHubReconnect` (deckhub), `TestDaemonDecks` (brightness, scenario per deck, watch, reload, MCP) |
| `/press` (by node, by key: the back key) and `/navigate` (node, back, home) that succeed: the state after the device thread's step, `applied`; a device thread that does not step: 202 within the bound, applied by the next step; a ticket outlives its runtime (the wait ends at once) | `PressAndNavigate`, `TestRuntime` (tickets) |

Not covered here: `DeckState`'s `busy_by` / `target` (not sent yet: SD5 / SD7).

The daemon's life and pairing (SD7, `tests/daemonlife_test.cpp`): the lock (held, busy, its holder, released), the
parents' leases (a pid released, a parent process that dies seen through its handle, a daemon started by hand that
stays), the routes and `Adopt` (a second start hands its parent to the running daemon over HTTP), the pairing against
a fake host (pending, approved: the token in a 0600 file; 503 refused; cancelled; unreachable), the strip's lines, a
tap on a notice. unreal-qt's side: `StreamDeckSupervisor_Test` and `StreamDeckApi_Test` in core-tests (the state
machine on the test's clock with a fake launcher and a fake daemon: start on a deck, crash backoff 1 / 2 / 4 s then
60 s, a hang killed after three checks, adopt with a lease, a foreign host, stop, restart, mode off, settings applied,
a missing binary, a second start that ends and is adopted; the verbs, 503 without unreal-qt, revoke),
`StreamDeckDialog_Test` in unreal-qt-tests (the page and the approval at the GUI), `McpStreamDeck_Test`.

### 3.2 With a host (needs SD7)

A headless host + the daemon with no real deck + a virtual viewer, scripted (`tools/verification/streamdeck/`):

1. Pair by `UNREAL_COMPANION_TOKENS`; join; the root page renders (viewer bytes).
2. Press "pause" on the viewer: the instance pauses (WebAPI read); the face changes from `run.state`.
3. Pause through the WebAPI: the face changes, an activity flash shows.
4. Turn the dial: `ttd.seek_by` coalesced; the strip readout equals `GET …/ttd/position`.
5. Enter "Time code": `ctx` changes; set `ctx` from the WebAPI: the deck navigates.
6. Two instances, two views (fake views registered through a test hook): focus switches the target; a pin holds.
7. Kill the host: the daemon shows the reconnect state; restart: same page.

Runs in CI on macOS, Windows, Linux (ACC-7), next to [§3.1](#31-device-wire-and-daemon-routes-hardware-free-in-ci).

## 4. Live

- A recipe in `.recipe/` for agents: start unreal-qt offscreen, start the daemon, open the studio (or a virtual
  viewer), drive both MCP planes.
- MCP: the daemon's `search_api` / `invoke_api` against its routes; unreal-ng's own MCP script set is untouched
  unless a WebAPI endpoint an MCP tool forwards to changes (then `tools/verification/mcp/run_all.py`).

## 5. Hardware

| Check | With |
|:--|:--|
| ACC-1 | Stream Deck + XL + Surface Dial, 60 s scrub, strip vs Dial result per rest |
| ACC-4 | Stream Deck + XL + two studio windows (mirror + own Neo) |
| ACC-6 | kill -9 the daemon during a scrub; frame-time trace of the emulation thread |
| Exclusive open | Other Stream Deck software (the Elgato app) holding the deck: the daemon reports busy |
| Models | Every model the owner has; the rest by the studio's per-model emulation (`emulation` test section) |

## 6. Cross-platform

- macOS (AppleClang), Linux (gcc 16 in docker, clang), Windows (MSVC, MinGW): zero warnings, unit tests pass.
- CI (`.github/workflows/cmake-ci.yml`, every push and pull request, after the throttle): four jobs build the
  daemon, sdctl, the studio and both test binaries from the repository root (`-DUNREAL_STREAMDECK=ON
  -DBUILD_QT_APPS=OFF -DTESTS=OFF`: no core build, the companion does not link the core) and run `streamdeck-tests`
  and `streamdeck-studio-tests` (offscreen): `build-streamdeck-linux` (the CI image, gcc 14), `build-streamdeck-macos`
  (macos-15, AppleClang), `build-streamdeck-windows-msvc` (windows-2022, MSVC 2022), `build-streamdeck-windows-mingw`
  (MSYS2 gcc, its runtime ahead of Qt's older `libstdc++` on `PATH`). `build-gcc-latest` adds the Qt-free targets with
  gcc 16 after the core tests and runs `streamdeck-tests`.
- The daemon's Winsock port and parent watch are checked on the Windows host; hidraw + the udev rule on Linux.

## 7. Load

- Readouts at 50 Hz to two decks + one Dial: emulation frame time unchanged within noise (A/B, interleaved rounds,
  `docs/guidelines/performance-guidelines.md`).
- Activity under a Dial scrub: ≤ 20 messages per second per session.
