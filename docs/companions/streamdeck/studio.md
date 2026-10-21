# Stream Deck Studio

The software Stream Deck and scenario editor of unreal-ng: executable `unreal-streamdeck-studio`, sources in
`companions/streamdeck/studio/`. Design: [studio.md](../../inprogress/2026-10-19-streamdeck-integration/studio.md),
[device-wire.md](../../inprogress/2026-10-19-streamdeck-integration/device-wire.md).

## Contents

- [1. What it is](#1-what-it-is)
- [2. Running it with the daemon](#2-running-it-with-the-daemon)
- [3. Roles](#3-roles)
- [4. The window](#4-the-window)
- [5. The scenario editor](#5-the-scenario-editor)
- [6. Recording](#6-recording)
- [7. Automation](#7-automation)
- [8. Command line](#8-command-line)
- [9. Protocol tests: `--listen`](#9-protocol-tests---listen)
- [10. Building and testing](#10-building-and-testing)
- [11. Source map](#11-source-map)
- [12. Not done yet](#12-not-done-yet)

## 1. What it is

| Part | What it does |
|:--|:--|
| Software deck | The device side of the Stream Deck HID protocol for all models (keys, Neo sensors, dials, the touch strip, firmware quirks of the + XL), drawn like the hardware: chassis, bezel mask, backlight, knobs |
| Viewer | A client of `unreal-streamdeck` over the device wire v2: it shows what the daemon draws on a deck and sends clicks back as the deck's input |
| Protocol log | Five sources, the last 5000 lines, filters |
| Scenario editor | Tree, design canvas, inspector (form + JSON), palette, breadcrumbs, issues, runtime output, undo; JSON / YAML / CSV / TSV files, includes |
| Recording | Video of the deck at 2x, constant frame rate, with the core's encoders |
| Automation | WebAPI + MCP on `127.0.0.1:37052` |

The studio never opens a USB device: the daemon is the only owner of the real decks.

## 2. Running it with the daemon

```bash
# the daemon with a scenario on the attached deck, viewers on 127.0.0.1:37050
unreal-streamdeck run my.deck.json --viewers --watch --api 37053
# the studio: a mirror of that deck
unreal-streamdeck-studio
```

- On launch, when nothing answers on the device wire (`127.0.0.1:37050`) nor on the daemon's automation (37053),
  the studio starts one: `unreal-streamdeck run --wait --api 37053 --viewers 37050 --with-parent` (the executable:
  `UNREAL_STREAMDECK_BIN`, else beside the studio, its bundle or the suite's bundle, else `PATH`). It plays the idle
  scenario on a deck attached now or later, serves the studio's own decks meanwhile, and ends with the studio.
  `--no-daemon-start` turns this off; a daemon on another host (`--daemon`) is never started. Its output goes to the
  protocol log (info).
- The studio connects to `127.0.0.1:37050` and sends `Join`. While no daemon is there the deck says "waiting for
  unreal-streamdeck on 127.0.0.1:37050" and the studio retries (250 ms, doubling to 4 s); a daemon that starts later
  is picked up without a restart.
- The daemon answers `Welcome` and replays the current picture (between `ReplayBegin` and `ReplayEnd`), so the deck
  shows at once what the real one shows.
- The editor's Run also starts a daemon when none runs ([§5](#5-the-scenario-editor)).
- Closing the window, Quit, `SIGTERM` and `SIGINT` (macOS, Linux) all end the studio normally: it sends `Leave`, a
  run gives its deck back, a daemon the studio started is stopped.

## 3. Roles

| Role | Join | What the deck shows | Clicks |
|:--|:--|:--|:--|
| Mirror (default) | `{"v":2,"role":"mirror","deck":"first"}` (or a serial with `--deck`) | the real deck, its model and serial; presses on the real deck light here (blue-violet) | go to the real deck's runtime |
| Own deck | `{"v":2,"role":"own","model":"neo"}` | a deck of its own: the daemon runs a runtime for it | go to that runtime |

Both also carry `"name":"Stream Deck Studio"` and the process id. Pick the role in the toolbar's Deck box ("Real deck
(mirror)", "Own: <model>"), the Deck menu, `--own MODEL`, or `PUT /api/v1/connection`.

What the studio does with each answer:

| Daemon sends | The studio |
|:--|:--|
| `Welcome` with a deck | takes the deck's model (a mirror follows it) and serial; the deck is live |
| `Welcome` `error: no_deck`, `waiting: true` | stays connected and shows "waiting for a Stream Deck"; the next `Welcome` makes it live |
| `Welcome` `error: version`, `bad_role`, `bad_model`, `bad_request` | shows the reason over the deck and does not retry (Deck > Reconnect, or another role, tries again) |
| `Output`, `SetFeature` | applied to the software deck exactly as a device would |
| `GetFeature` | own role only: answered with `FeatureReply` (serial, firmware `1.0.0.E`, sleep, geometry) |
| `HostInput` | the press lights on the software deck; the protocol log's DECK source |
| `HostNote` | the log's APP source |
| `HostPage` | kept for `GET /api/v1/page`; the deck stops drawing its own press glow (the runtime draws pressed faces) |
| `DeckState` | `connected: false` shows "deck unplugged" over the deck and clears lit presses; `target` shows in the status bar |
| `ReplayBegin` / `ReplayEnd` | clears the screen, applies the replay without a log line per report, then logs one summary line |
| `Control` `{"op":"snapshot","path":...}` | saves a PNG of the device there |

## 4. The window

- **Toolbar**: Deck (role or model), Brightness, Bezel mask, Visible area (the part of each key visible head-on,
  measured on the + XL), Firmware quirks, Rec, Scenario editor.
- **Brightness**: the brightness of the deck the window shows (the real deck for a mirror, the own deck otherwise),
  set through the daemon (`PUT /api/v1/decks/{serial}/brightness` on 37053). Off while the window is not joined or
  the daemon has no automation port; it follows changes made elsewhere (a scenario's brightness action, an agent).
- **The deck**: click = press, wheel over a dial = turn, drag around a dial = turn, a short click on a dial = press
  and indicator reset, hold = long press, drag on the strip = swipe. Cyan glows are this window's input, blue-violet
  ones the real deck's.
- **Protocol panel**: DAEMON (frames to this deck: images, features), VIEWER (this window's input), DECK (the real
  deck's input), APP (what the runtime did), info (the studio itself).
- **Status bar**: the daemon address and state, the role and deck serial, reports and images per second, recording.
- **Title**: "Stream Deck Studio - <model> <serial> · mirror" (or `own`, `listening`).

## 5. The scenario editor

Opened with the toolbar button or Cmd+E, in its own window or docked beside the deck.

| Area | What it does |
|:--|:--|
| Tree | The whole scenario; included subtrees in italics, read-only |
| Canvas | The shown menu on the model's panel, faces drawn by the runtime's renderer; drop palette items on keys, drag keys to move / swap / into a menu, double-click a menu to enter, the back key to go up, right-click for import / export / links |
| Inspector | A form for the selected node or the scenario, and its JSON (Apply = one undo step) |
| Palette | Generic presets (menus, HTTP, multi, toggle, delay, shell, open, keys, navigation, brightness) and the user's own from `<config>/unreal-ng/streamdeck/presets/*.json` (`{"presets":[{category,name,description,node}]}`; `<config>` is `~/.config` on Linux, `~/Library/Preferences` on macOS, `%LOCALAPPDATA%` on Windows) |
| Issues / Runtime | Validation (errors block Run) and the run's output |

**Run on the deck** (Cmd+R) writes the document, includes resolved, to a run file in the studio's data folder
(`run/<name>.deck.json`) and:

- when a daemon answers on its automation port `127.0.0.1:37053`, the deck the window shows plays the run file, that
  deck only (`PUT /api/v1/decks/{serial}/scenario`, watched): an own deck runs the editor's scenario while the real
  deck keeps the daemon's; a mirror runs it on the real deck. Not joined: the daemon's first deck. **Stop** gives the
  deck the daemon's start scenario back (`DELETE /api/v1/decks/{serial}/scenario`);
- otherwise starts `unreal-streamdeck run <run file> --watch --wait --api 37053 --with-parent`; **Stop** ends it.

Edits autosave to the run file after 250 ms and the daemon reloads it with the menu stack kept.

## 6. Recording

Rec (or Record > Start, `--record FILE`, `POST /api/v1/recording`) records the device as shown at 2x: `.mp4` / `.mov`
with VideoToolbox on macOS or NVENC on Windows, else ffmpeg (`.mkv`, `.webm`, any container ffmpeg has), `.gif`
built in. Constant frame rate (30 by default, GIF at most 25); a stall repeats the last picture.

## 7. Automation

WebAPI on `http://127.0.0.1:37052/api/v1/...`, MCP on `/mcp` (Automation > Copy MCP Config: through
`unreal-mcp-bridge`), the description at `/api/v1/openapi.json`; `--api-port 0` turns it off.

| Route | What |
|:--|:--|
| `GET /api/v1/device`, `GET /api/v1/models`, `PUT /api/v1/device/model` | The deck; the models; switch the model (an own deck of it; with `--listen` the deck's model) |
| `GET /api/v1/decks` | The daemon's logical decks (forwarded: serial, model, real, connected, scenario, brightness, viewers) |
| `GET` / `PUT /api/v1/deck/brightness` | The shown deck's brightness through the daemon (`{"value": 0..100}`); the toolbar follows |
| `GET /api/v1/connection` | Daemon address, state (`connecting`, `joining`, `waiting`, `joined`, `failed`, `stopped`), role, deck serial, model, real or own, deck connected, target, error |
| `PUT /api/v1/connection` | `{"role":"mirror","deck":"first"}` or `{"role":"own","model":"neo"}` |
| `GET` / `PUT /api/v1/settings` | Bezel mask, visible area, firmware quirks |
| `POST /api/v1/input/key`, `/input/sensor`, `/input/dial`, `/input/touch` | Input like a finger (`source: deck` shows a press as the real deck's, nothing sent) |
| `GET /api/v1/screenshot` | PNG: `area` lcd / deck / window; `target editor` for the canvas |
| `GET /api/v1/page` | What the keys mean now (the runtime's `HostPage`) |
| `GET` / `DELETE /api/v1/log`, `PUT /api/v1/log/filter` | The protocol log (`source`: daemon, viewer, deck, app, info) |
| `GET` / `POST /api/v1/recording` | Recording status, start / stop |
| `PUT /api/v1/window` | Raise, resize, protocol panel, editor window / dock / hidden |
| `/api/v1/editor/...` | Status, new, open, save, export, document (JSON / YAML / CSV / TSV), tree, nodes (add / patch / delete / move / duplicate / export / inline), import, select, enter, canvas, undo, redo, issues, presets, run, stop, screenshot |
| `/api/v1/runtime/state`, `/page`, `/navigate`, `/press` | Proxied to the daemon's automation on 37053, for the deck the run plays on |

## 8. Command line

| Option | Default | What |
|:--|:--|:--|
| `--daemon HOST:PORT` | `127.0.0.1:37050` | The daemon's device wire |
| `--deck SERIAL` | `first` | Mirror this real deck |
| `--own MODEL` | | An own deck of this model (`plusxl`, `plus`, `xl`, `classic`, `neo`, `mini`, `original`, `pedal`, ..., or a PID) |
| `--api-port PORT` | `37052` | Automation; 0 = off |
| `--daemon-api PORT` | `37053` | The daemon's automation (decks, brightness, Run); also the port of a daemon the studio starts |
| `--no-daemon-start` | | Do not start a daemon when none answers at launch |
| `--record FILE`, `--fps N` | 30 | Record from the start |
| `--listen [PORT]`, `--model MODEL`, `--any` | 37050, `plusxl` | Protocol tests ([§9](#9-protocol-tests---listen)) |

Environment: `UNREAL_STREAMDECK_BIN`, the daemon the studio starts (at launch, or for the editor's Run)
([environment-variables.md](../../emulator/environment-variables.md)).

## 9. Protocol tests: `--listen`

`unreal-streamdeck-studio --listen [PORT]` makes the studio the device for **one** host over the v1 emulator wire
(`[u8 type][u16 length LE][payload]`, `companions/streamdeck/lib/include/sdb/emuwire.h`), the way `sdctl
--device emu:127.0.0.1:PORT ...` drives it without a daemon. The newest host wins; the picture stays between hosts;
`Hello`, `SetModel`, `Snapshot` and `Record` work as in v1. Only for protocol tests: it never talks to the daemon.

## 10. Building and testing

```bash
cmake -S . -B cmake-build-agent-release -G Ninja -DUNREAL_STREAMDECK=ON ...   # once
tools/build/build.sh unreal-streamdeck-studio
tools/build/build.sh streamdeck-studio-tests
cmake-build-agent-release/bin/streamdeck-studio-tests
```

- Built when `UNREAL_STREAMDECK` is on and Qt 6 is found (the same discovery as unreal-qt); a macOS bundle
  `unreal-streamdeck-studio.app` ("Stream Deck Studio", `dev.unrealng.streamdeck-studio`).
- Packaged in every release ([packaging.md](../../inprogress/2026-10-19-streamdeck-integration/packaging.md#3-macos)):
  macOS `UnrealNG.app/Contents/Applications/Stream Deck Studio.app`, a nested app on UnrealNG.app's Qt (frameworks through
  `../../../../Frameworks`; `main.cpp` adds UnrealNG.app's `PlugIns` from the real executable path), linked from the
  DMG root; Windows
  `unreal-streamdeck-studio.exe` beside `unreal-qt.exe` on the same Qt DLLs; DEB `/usr/lib/unreal-ng`, linked from
  `/usr/bin`; AppImage `usr/bin` (deployed with the suite's Qt); tarball at the top. The daemon it starts is beside
  it, or in the suite's bundle on macOS.
- Links `sdb` (scenario, navigator, render, wire), `unrealng::recording` and Qt Widgets / Network, not hidapi: USB
  lives in `sdb-hid` (`lib/src/hid/`), which only the daemon and `sdctl` link.
- `streamdeck-studio-tests` (not built by default) runs offscreen with no window: the wire v2 client against a fake
  daemon in the test (Join for both roles, `Welcome` and its errors, replay, `HostInput`, `HostPage`, `DeckState`,
  `GetFeature` / `FeatureReply`, `Input`, `Leave`, reconnect after the daemon goes and after a broken frame), the
  `--listen` server (`Hello`, `SetModel`, `GetFeature`) and the daemon control against a stand-in automation
  (health, decks, brightness and scenario per deck, errors, the started process).

## 11. Source map

| File | What |
|:--|:--|
| `main.cpp` | Options |
| `mainwindow.*` | The window, menus, log, roles |
| `virtualdeck.*` | The device side of the HID protocol |
| `deckwidget.*` | Drawing the deck, mouse to input |
| `decklink.h` | What feeds the deck (the window's view of a link) |
| `deckclient.*` | Device wire v2 client (default) |
| `daemoncontrol.*` | The daemon's automation (decks, brightness, scenario per deck) and the daemon process the studio starts |
| `listenserver.*` | v1 server for `--listen` |
| `studioapi.*`, `gui.h` | Automation routes; running a handler on the GUI thread |
| `recorder.*` | Video recording |
| `textedit.h` | Edit commands for the focused text field |
| `editor/editordocument.*` | The document, undo, validation, selection |
| `editor/designcanvas.*` | The canvas |
| `editor/inspector.*` | The inspector |
| `editor/presets.*` | The palette |
| `editor/runcontroller.*` | Run on the shown deck through the daemon |
| `editor/editorwidget.*` | The editor's layout and commands |
| `editor/editorapi.*` | The editor's routes, the runtime proxy |
| `tests/studio_test.cpp` | The tests |

## 12. Not done yet

| What | Needs |
|:--|:--|
| A single daemon per session (lock), adopting one started elsewhere, `--parent` instead of `--with-parent` | the daemon's supervision rules (daemon.md §7, §8; SD7) |
| A deck list in the window (the Deck box offers "the first real deck" and own models) | several real decks (later) |
| Palette from the host's catalogue, action forms, contexts, activity | the `action` kind and the catalogue (SD4, SD5) |
| Open shipped example scenarios | `data/streamdeck/scenarios/` (SD5) |
| Windows and Linux builds checked | SD8 |
