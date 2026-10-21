# unreal-streamdeck: the Stream Deck daemon

`unreal-streamdeck` owns the attached Stream Deck. It plays a scenario on it (menus, actions, the touch strip) and
serves software decks (Stream Deck Studio windows) over the device wire. The design is in
[docs/inprogress/2026-10-19-streamdeck-integration](../../inprogress/2026-10-19-streamdeck-integration/README.md).
Built with `-DUNREAL_STREAMDECK=ON`.

## Commands

| Command | What |
|:--|:--|
| `unreal-streamdeck run [scenario] [options]` | Play a scenario (`.json`, `.yaml`, `.csv`, `.tsv`) on the deck; without one, the built-in idle scenario (one empty menu) until a scenario is loaded for a deck |
| `unreal-streamdeck pair [--webapi URL] [--name NAME] [--file F]` | Pair with unreal-ng as a companion (approved at the emulator's GUI); the token goes to `~/.config/unreal-streamdeck/companion.json` (mode 0600) |
| `unreal-streamdeck scenario check \| convert \| extract \| resolve ...` | Check and convert scenario files |
| `unreal-streamdeck --help` | The usage on stdout, exit 0 (without a command: the same, exit 2) |

`run` options:

| Option | Meaning |
|:--|:--|
| `--watch` | Reload the start scenario when it or an included file changes (the menu stack is kept) |
| `--wait` | No deck attached: serve the viewers meanwhile (own decks work, mirrors wait) and take the first deck that appears (checked every second). Without it a missing deck ends `run` at once (exit 5) |
| `--api PORT` | Automation on `127.0.0.1:PORT`: REST and MCP (`/mcp`), [below](#automation) |
| `--viewers [PORT]` | Software decks join on `127.0.0.1:PORT` (device wire v2, default 37050). On by default |
| `--no-viewers` | No software decks |
| `--device S` | The deck whose serial contains S (default: the first one) |
| `--emulate MODEL` | Present a smaller model on the real deck |
| `--dry-run` | Log actions instead of running them |
| `--seconds N` | Stop after N seconds |
| `--with-parent` | Stop when the starting process goes away (macOS, Linux) |

## Software decks (device wire v2)

The daemon listens on `127.0.0.1:37050`; Stream Deck Studio connects to it, never to USB. A viewer joins in one of
two roles:

- **mirror**: shows the real deck. It gets every image and feature report the deck gets, and the current picture at
  once when it joins late (the daemon keeps the last image of every key, the strip, the brightness). Its clicks reach
  the deck's runtime; presses on the real deck are relayed to it.
- **own**: a deck of its own (any model). The daemon starts a runtime with the same scenario for it; it ends when
  the viewer leaves.

A slow viewer never stalls the deck. It has a bounded queue (64 frames, 256 KB of kernel buffer); when it falls
behind, the images queued for it are dropped and it gets the whole current picture once it catches up. A viewer that
does not join within 2 s is closed. Unplugging the deck keeps its viewers (they are told), and plugging it back in
continues ([below](#hot-plug)).

## Hot plug

The USB deck may be unplugged and plugged back in at any time, on macOS, Linux (hidraw) and Windows alike:

1. **Unplugged.** The first read, write or feature report that fails (hidapi's `-1`: macOS reports the removal, hidraw
   a `POLLHUP`, Windows a failed `ReadFile` / `WriteFile`) closes the handle. `GET /api/v1/decks` shows
   `"connected": false` at once; the deck's viewers get `DeckState {"connected": false}` and stay; a mirror that
   joins meanwhile waits for the deck. The runtime goes on: navigation, actions, brightness and scenario changes from
   the routes, viewers' presses. What it draws is kept (the picture cache of the deck: every key, the strip, the
   brightness), not sent.
2. **Looked for.** Once a second the device thread's own read enumerates the USB decks again (no thread or timer of
   its own) and opens the deck with the same serial and model when it is there. A handle that cannot be opened yet
   (the deck still settling, the Elgato app holding it) is tried again a second later.
3. **Back.** The deck gets its picture back from the cache (brightness first, then every key and the strip), so it
   shows what it showed, with the changes made while it was away. Same runtime, same menu stack, same scenario; the
   viewers get `DeckState {"connected": true}` (no new picture: they never lost theirs). The daemon prints
   `deck SERIAL unplugged (...)` and `deck SERIAL is back`.

**Another deck instead.** When the lost deck stays away 3 s and another deck is plugged in (one whose serial matches
`--device`; any deck without it), that deck is a new logical deck: the old runtime ends, the new deck gets a runtime
of its own on the start scenario, at its own model (a scenario `PUT` for the old deck does not carry over, its menu
stack neither: the grids differ). Mirrors that asked for "the first deck" move to it (a new `Welcome` with the new
model, then its picture); a mirror of the old serial keeps waiting for that deck. A short glitch never swaps decks:
within the 3 s only the same serial is taken. A deck with the same serial and another product ID (another mode of the
device) counts as another deck.

`POST /api/v1/decks/{serial}/reconnect` does what an unplug does, on purpose: the handle is closed on the device
thread's next call, stays closed `away_ms` (default 0), and the deck is opened again and redrawn the same way. It
mends a deck that shows garbage after a USB hiccup, and it is how the reopen path is checked on a real deck without
pulling the cable.

An older Stream Deck Studio that listens on 37050 itself takes the port: `run` then says so and runs without viewers.
`--mirror` / `--device emu` still talk to such a studio (the device wire v1).

## Automation

With `--api PORT` (Stream Deck Studio uses 37053) the daemon serves one route table twice: REST under
`http://127.0.0.1:PORT/api/v1/...` (described at `/api/v1/openapi.json`) and MCP on `POST /mcp` with the tools
`search_api` and `invoke_api` (stdio clients: `unreal-mcp-bridge --url http://127.0.0.1:PORT/mcp`). Both answer
from the same handlers (`sdb/daemondecks.h`), so a route is on both planes or on neither.

A **logical deck** is the real deck or a viewer's own deck. `{serial}` is its serial, or `first` (the first real
deck, else the first deck); the runtime routes take `?deck=SERIAL` with the same default.

| Route | What |
|:--|:--|
| `GET /api/v1/health` | `ok`, version, the number of decks and viewers, mirrors waiting for a deck, the wire port |
| `GET /api/v1/decks` | Every logical deck: serial, model, PID, real or own, connected, scenario file (`""` = the idle one), `start_scenario`, watched, brightness, viewers (id, role, name, pid); `waiting_mirrors` |
| `GET /api/v1/decks/{serial}` | One deck, as in the list |
| `GET` / `PUT /api/v1/decks/{serial}/brightness` | The deck's brightness; `{"value": 0..100}` or `{"delta": n}` (clamped). The runtime sends it on its next step, viewers get it as a feature report |
| `GET /api/v1/decks/{serial}/scenario` | The deck's file, whether it is the start scenario, watched, the scenario's name; `?document=true` adds the document |
| `PUT /api/v1/decks/{serial}/scenario` | `{"file": path, "watch": true}`: this deck plays another file (the other decks keep theirs), the menu stack kept; watched by default, so edits reload it. A file with errors is refused (422) and the deck keeps what it plays |
| `DELETE /api/v1/decks/{serial}/scenario` | The deck plays the daemon's start scenario again |
| `POST /api/v1/decks/{serial}/reconnect` | `{"away_ms": 0..60000}`: close the real deck's USB handle and open it again after that long (an unplug on purpose, [hot plug](#hot-plug)); 409 for an own deck |
| `POST /api/v1/decks/{serial}/snapshot` | `{"path": file}`: every viewer of the deck saves a PNG there (a `Control` frame); answers how many viewers it went to, 409 when nobody views the deck |
| `GET /api/v1/state`, `/page`, `/nodes` | The runtime of one deck: menu stack, page, toggles, last actions; what the keys mean now; the scenario's nodes (filters `tag`, `type`, `kind`, `menu`, `text`) |
| `POST /api/v1/navigate`, `/press` | `{"to": back\|home\|node, "node"}`; a key by `node` id or `key` index |
| `POST /api/v1/reload` | Read the deck's file again, or `{"file"}` for another one (as `PUT .../scenario`) |
| `GET /api/v1/scenario` | The deck's file and its document |

Own decks begin on the start scenario. Stream Deck Studio's Run puts the scenario open in its editor on the deck its
window shows with `PUT /api/v1/decks/{serial}/scenario` and gives the deck back with `DELETE`. A runtime asking for
another file (`nav {to: scenario}`) loads it for its own deck.

```bash
curl -s localhost:37053/api/v1/decks
curl -s -X PUT localhost:37053/api/v1/decks/first/brightness -d '{"value":40}'
curl -s -X PUT localhost:37053/api/v1/decks/EMU1/scenario -d '{"file":"/path/my.deck.json"}'
curl -s -X DELETE localhost:37053/api/v1/decks/EMU1/scenario
curl -s -X POST localhost:37053/api/v1/decks/first/reconnect -d '{"away_ms":2000}'
```

## Threads

| Thread | Does |
|:--|:--|
| Device (one per deck, the main one for the real deck) | Reads input, runs the scenario, writes HID reports; while the deck is unplugged, its reads look for it again once a second ([hot plug](#hot-plug)) |
| Network | The event loop: viewers and the device wire (`sdb/eventloop.h`, `sdb/deckhub.h`) |
| Runtime workers | Actions and the time-travel readout, per runtime (both go with the surface client, SD5) |
| Automation | With `--api`: one worker |
| Watcher | Scenario loads a runtime asks for, and the watched files of every deck (every 250 ms) |

## Platforms

| Platform | Deck access | Actions | Built with |
|:--|:--|:--|:--|
| macOS | IOHID (hidapi), exclusive open | `shell` via `/bin/sh`, `open`, `keys` (CGEvent) | Apple clang |
| Linux | hidraw (hidapi; libudev enumerates): the udev rule below | `shell` via `/bin/sh`, `open` via `xdg-open`, `keys` via `xdotool` (X11, optional) | gcc 14 (the CI image) |
| Windows | HID.dll (hidapi), no driver | `shell` via `cmd.exe` (output, timeout; the whole process tree ends on timeout), `open` via `start`; no `keys` | MSVC 2019, MinGW gcc 15 |

All three build the daemon, `sdctl`, the studio and both test suites with zero warnings. Build on Linux in the CI
image: `docker/linux/build.sh --streamdeck --test` (configures from the root with `-DUNREAL_STREAMDECK=ON`, runs
`streamdeck-tests` and `streamdeck-studio-tests` offscreen). A native build needs `libudev-dev` (and Qt 6 for the
studio).

### Linux: the udev rule

Without it only root can open the deck's `/dev/hidraw*` nodes. The rule
[`companions/streamdeck/linux/70-unreal-streamdeck.rules`](../../../companions/streamdeck/linux/70-unreal-streamdeck.rules)
tags every Elgato device (USB vendor `0fd9`) `uaccess`, so the user at the seat gets read and write access. The DEB
installs it to `/usr/lib/udev/rules.d`; the tarball has it in `lib/udev/rules.d` (copy that file instead); for an
AppImage or a build from source:

```bash
sudo install -m 0644 companions/streamdeck/linux/70-unreal-streamdeck.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# then unplug the deck and plug it back in
```

`uaccess` needs a local login session. A user without a seat (SSH only, a service account) needs a group instead:
copy the rule with `MODE="0660", GROUP="plugdev"` in place of `TAG+="uaccess"` and add the user to the group.

## Installed

Every release package carries the daemon (the release workflow builds with `-DUNREAL_STREAMDECK=ON`; layout and
rationale: [packaging.md](../../inprogress/2026-10-19-streamdeck-integration/packaging.md)):

| Package | Where |
|:--|:--|
| macOS DMG | `UnrealNG.app/Contents/MacOS/unreal-streamdeck`, a helper inside the suite's bundle, signed with it |
| Windows ZIP | `unreal-streamdeck.exe` beside `unreal-qt.exe` |
| DEB | `/usr/lib/unreal-ng/unreal-streamdeck`, linked from `/usr/bin`; the udev rule in `/usr/lib/udev/rules.d` |
| AppImage | `usr/bin/unreal-streamdeck` in the image (`--appimage-extract` to run it alone); the rule in `usr/lib/udev/rules.d`, not installed |
| Linux tarball | `unreal-streamdeck` at the top; the rule in `lib/udev/rules.d` |

Default scenarios and themes (`data/streamdeck/`) are not packaged yet (SD5).

## Developer checks (`sdctl`)

| Command | What |
|:--|:--|
| `sdctl viewer [--port N] [--deck SERIAL \| --own MODEL] [--seconds N]` | Join the daemon as a viewer; prints the welcome, the replayed picture and frame counts per second |
| `sdctl ws <ws://host:port/path> [message ...] [--token T] [--count N] [--seconds N]` | Open a WebSocket with the daemon's client; prints what arrives |
| `sdctl decks [--api PORT] [--json]` | The daemon's logical decks (`GET /api/v1/decks`, port 37053 by default) |
| `sdctl emu-snapshot --out f.png --api PORT [--deck SERIAL]` | The deck's viewers save a PNG through the daemon (device wire v2 `Control`); waits up to 3 s for the file |

### `sdctl emu-*` and the device wire

| Command | Wire | Why |
|:--|:--|:--|
| `emu-snapshot --api PORT` | v2, through the daemon | A snapshot is of a deck: the daemon knows its viewers and asks them (`Control`) |
| `emu-snapshot` (no `--api`), `emu-model`, `emu-record` | v1, against `unreal-streamdeck-studio --listen` | In v2 a viewer's model is the viewer's own choice (a mirror follows the deck, an own deck is picked in the studio or with its `PUT /api/v1/connection`) and recording is the studio's (`POST /api/v1/recording` on its port): there is nothing for the daemon to forward. They stay as the protocol tests of the v1 server, with `sdctl --device emu:...` |

```
unreal-streamdeck run my-scenario.yaml &    # the default scenario ships with SD5
sdctl viewer --seconds 3                  # mirror of the first deck: replay, then live frames
sdctl viewer --own neo --seconds 3        # a Neo of its own, with its own runtime
```
