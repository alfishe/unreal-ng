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
- [4. Live](#4-live)
- [5. Hardware](#5-hardware)
- [6. Cross-platform](#6-cross-platform)
- [7. Load](#7-load)

## 1. Today

- `sdb_tests`: one ctest, 838 checks, 0.9 s: models, report bytes (golden), parser, image, mapping, theme, scenes,
  scenario (parse, ops, io, render), navigator, actions and runtime with a fake host.
- Not covered: the unreal-ng HTTP code (resolve, replace, control sessions, token, `ttdNow`, the instance follow), the
  TCP and Tee transports, the lock file. The studio has no tests.

## 2. Unit

| Area | Tests |
|:--|:--|
| Library | The 838 checks, moved; run as `streamdeck-tests` |
| Multiplexer | Fake HID transport + fake viewers: fan-out identical bytes, input merge with per-source parsers, late-join replay equals the deck's last picture, slow viewer drops images and never blocks the deck, unplug / replug keeps the page |
| Wire v2 | Frame round trips, `Join` roles, 32-bit lengths (a `HostPage` > 64 KB), `Welcome` errors |
| Surface client | Against an in-process fake hub: join, subscribe, action / result by `rid`, delta merge before send, reconnect with pin and resubscribe, `revoked` |
| Runtime | `action` kind, state-bound faces from readouts, context → navigation without echo, activity flashes, target line |
| Core side | `TargetState` rules 1-6 and lifetime ([targeting.md](targeting.md)); activity merge and rate; readout vocabulary; catalogue fields (in `core-tests`, `*Surface*`, `*Target*`) |

Rules of `core/tests/README.md` apply on the core side (no `sleep_for`, ≤ 50 ms, `TestWait`).

## 3. Conformance

A headless host + the daemon with no real deck + a virtual viewer, scripted (`tools/verification/streamdeck/`):

1. Pair by `UNREAL_COMPANION_TOKENS`; join; the root page renders (viewer bytes).
2. Press "pause" on the viewer: the instance pauses (WebAPI read); the face changes from `run.state`.
3. Pause through the WebAPI: the face changes, an activity flash shows.
4. Turn the dial: `ttd.seek_by` coalesced; the strip readout equals `GET …/ttd/position`.
5. Enter "Time code": `ctx` changes; set `ctx` from the WebAPI: the deck navigates.
6. Two instances, two views (fake views registered through a test hook): focus switches the target; a pin holds.
7. Kill the host: the daemon shows the reconnect state; restart: same page.

Runs in CI on macOS, Windows, Linux (ACC-7).

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
- The daemon's Winsock port and parent watch are checked on the Windows host; hidraw + the udev rule on Linux.

## 7. Load

- Readouts at 50 Hz to two decks + one Dial: emulation frame time unchanged within noise (A/B, interleaved rounds,
  `docs/guidelines/performance-guidelines.md`).
- Activity under a Dial scrub: ≤ 20 messages per second per session.
