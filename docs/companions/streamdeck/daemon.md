# unreal-streamdeck: the Stream Deck daemon

`unreal-streamdeck` owns the attached Stream Deck. It plays a scenario on it (menus, actions, the touch strip) and
serves software decks (Stream Deck Studio windows) over the device wire. The design is in
[docs/inprogress/2026-10-19-streamdeck-integration](../../inprogress/2026-10-19-streamdeck-integration/README.md).
Built with `-DUNREAL_STREAMDECK=ON`.

## Commands

| Command | What |
|:--|:--|
| `unreal-streamdeck run [scenario] [options]` | Play a scenario (`.json`, `.yaml`, `.csv`, `.tsv`) on the deck; without one, the built-in [idle scenario](#the-idle-scenario) (the UnrealNG look) until a scenario is loaded for a deck |
| `unreal-streamdeck pair [--webapi URL] [--name NAME] [--file F]` | Pair with unreal-ng as a companion (approved at the emulator's GUI); the token goes to `~/.config/unreal-streamdeck/companion.json` (mode 0600) |
| `unreal-streamdeck scenario check \| convert \| upgrade \| extract \| resolve ...` | Check scenario files (`check` also warns about action ids the catalogue lacks: `$data/streamdeck/catalogue.json` or `--catalogue F`), convert them to another file format, upgrade format v1 to v2 |
| `unreal-streamdeck --help` | The usage on stdout, exit 0 (without a command: the same, exit 2) |

`run` options:

| Option | Meaning |
|:--|:--|
| `--watch` | Reload the start scenario when it or an included file changes (the menu stack is kept) |
| `--wait` | No deck attached: serve the viewers meanwhile (own decks work, mirrors wait) and take the first deck that appears (checked every second). Without it a missing deck ends `run` at once (exit 5) |
| `--api PORT` | Automation on `127.0.0.1:PORT`: REST and MCP (`/mcp`), [below](#automation) |
| `--host URL` | The unreal-ng WebAPI the scenarios' actions go to (default `http://127.0.0.1:8090`; `host:port` is taken too). A v2 scenario names no host; a v1 scenario's `settings.webapi` still wins for it. The `action` kind posts `POST /api/v1/controllers/actions/{id}` there until the surface client comes ([scenarios.md §2](../../inprogress/2026-10-19-streamdeck-integration/scenarios.md#2-the-action-kind)) |
| `--viewers [PORT]` | Software decks join on `127.0.0.1:PORT` (device wire v2, default 37050). On by default |
| `--no-viewers` | No software decks |
| `--device S` | The deck whose serial contains S (default: the first one) |
| `--emulate MODEL` | Present a smaller model on the real deck |
| `--dry-run` | Log actions instead of running them |
| `--seconds N` | Stop after N seconds |
| `--with-parent` | Stop when the starting process goes away (macOS, Linux) |

## The idle scenario

Without a scenario every deck plays the idle one: the UnrealNG theme of the data directory
(`$data/streamdeck/themes/unrealng`, [below](#data-directory)), laid out for the deck's model when the deck appears
(`sdb/idlescenario.h`). Its keys are pictures only: pressing one does nothing (an empty `multi`).

| Deck | Strip / window | Keys |
|:--|:--|:--|
| Stream Deck + XL (the theme's own deck) | `strip.png`, the "UNREAL NG / ZX SPECTRUM EMULATOR" banner, edge to edge | the theme's 27 icons at their keys (rows 0-2), row 3 blank |
| Stream Deck + | the banner | the first 8 icons |
| Stream Deck Neo | the logo (`logo.png`, the banner's middle; `theme.json` `logo`): the whole banner would be a thin band in the 248 x 58 window | the first 8 icons |
| XL (4 x 8), Original / MK.2 (3 x 5), Mini (2 x 3) | none | the logo (`logo.png`) spread over the middle row (row 1; the Mini's row 0): the bezel gaps between keys hide parts of it, so the larger letters; the icons in the theme's order on the other keys |
| Pedal | none | none (no key screens) |
| any, no data directory | the drawn title "UnrealNG" | the middle key: the drawn title (decks without a strip) |

Never the time-travel timeline: it is a strip readout, never keys (and idle has no emulator, so no history). The
theme's `ttd_strip.png` (`lcd_ttd` in `theme.json`) is a picture of that readout for the strip; the live one is the
runtime's strip status (SD5). The brightness is the theme's (60). `GET /api/v1/decks/{serial}/scenario?document=true` shows the document made for
that deck; a picture spread over several keys is one file with a region per key (`file.png#xywh=x,y,w,h`, below).
`DELETE .../scenario` and `POST /api/v1/reload` make it again (an edited theme shows).

## Data directory

`$data` in scenario paths ([scenarios.md §3](../../inprogress/2026-10-19-streamdeck-integration/scenarios.md#3-paths))
is the companion's data directory, laid out as `data/` of the repository (`streamdeck/` with the themes, `icons/`).
The daemon, `sdctl` and Stream Deck Studio look for it the way unreal-qt finds its resources
(`FileHelper::GetResourcesPath`), and take the first of these folders that has a `streamdeck/` folder
(`sdb/datadir.h`):

| # | Where | For |
|:--|:--|:--|
| 1 | `UNREAL_STREAMDECK_DATA` | an override ([environment-variables.md](../../emulator/environment-variables.md)) |
| 2 | `Contents/Resources` of every app bundle round the executable, innermost first | macOS: the daemon in `UnrealNG.app/Contents/MacOS` and the nested `Stream Deck Studio.app` both reach `UnrealNG.app/Contents/Resources` |
| 3 | the executable's folder | the Windows ZIP, the Linux tarball, the DEB (`/usr/lib/unreal-ng`); a build's `bin/` (`cmake/RuntimeData.cmake` copies `data/streamdeck` there) |
| 4 | the folder holding the outermost bundle | a build tree: `bin/` beside `bin/unreal-streamdeck-studio.app` |
| 5 | `../share/unreal-ng` | an FHS install |
| 6 | `data/` in the executable's folder and in each of the four folders above it | a build inside the source tree (`cmake-build-*/bin` -> `data/`) |
| 7 | the source tree's `data/` the binary was built from | a build elsewhere, not installed |

The executable's path is the real one (links resolved: `/usr/bin` on the DEB, the DMG root's link to the studio).
`run` prints the folder it took (`data: ...`), or a note when there is none: then `$data` paths do not load and the
idle screen is the drawn title. Every release package carries `data/streamdeck` where row 2 or 3 finds it:
`UnrealNG.app/Contents/Resources/streamdeck`, `streamdeck/` beside the executables (Windows ZIP, Linux tarball,
AppImage `usr/bin`), `/usr/lib/unreal-ng/streamdeck` (DEB) ([packaging.md §6](../../inprogress/2026-10-19-streamdeck-integration/packaging.md#6-data)).

Picture paths in faces and the strip (`face.image`, `strip.image`):

| Form | Means |
|:--|:--|
| `$data/streamdeck/...` | under the data directory |
| `$icons/name.svg` | `$data/icons/name.svg` |
| `...png#xywh=x,y,w,h` | a region of the file in its pixels ([W3C media fragment](https://www.w3.org/TR/media-frags/#naming-space), `pixel:` allowed); the parts of the region outside the picture are black |
| anything else | as written (relative to the daemon's working directory) |

An image strip (`strip.content: image`) is drawn edge to edge, from the crumbs (or the left edge) to the right edge,
aspect kept.

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
pulling the cable. A real cable pull and replug on the + XL (macOS) was checked by hand: the same picture and menu return.

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
| `POST /api/v1/navigate`, `/press` | `{"to": back\|home\|node, "node"}`; a key by `node` id or `key` index (down and up). The answer is `/state` once the deck's device thread has applied it (its next step, ~10 ms; no fixed wait), with `deck` and `"applied": true`; a device thread that does not come round within 1 s answers 202, `"applied": false`, the state as it was (the request stays queued). An action the press starts runs on the runtime's worker: its result is in `last_actions` of a later `/state`. A node that is not on the page: 404 before anything is queued |
| `POST /api/v1/reload` | Read the deck's file again, or `{"file"}` for another one (as `PUT .../scenario`) |
| `GET /api/v1/scenario` | The deck's file and its document |

Own decks begin on the start scenario. Stream Deck Studio's Run puts the scenario open in its editor on the deck its
window shows with `PUT /api/v1/decks/{serial}/scenario` and gives the deck back with `DELETE`
([scenario editor](scenario-editor/02-ux-design.md#3-running-on-the-deck); the scenario format:
[03-technical-design.md](scenario-editor/03-technical-design.md#2-file-format-v1)). A runtime asking for
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
| Linux | hidraw (hidapi; libudev enumerates): the udev rule below | `shell` via `/bin/sh`, `open` via `xdg-open`, `keys` via `xdotool` (X11, optional; missing: the action fails with "xdotool not found") | gcc 14 (the CI image) |
| Windows | HID.dll (hidapi), no driver | `shell` via `cmd.exe` (output, timeout; the whole process tree ends on timeout), `open` via `start`, `keys` via `SendInput` | MSVC 2019, MinGW gcc 15 |

A `keys` combo takes the same names everywhere (`sdb::scn::ParseKeyCombo`, `KeyNames()` in `sdb/actions.h`; US
layout positions): modifiers `cmd` (`command`, `meta`: Command on macOS, the Windows key, Super on Linux), `ctrl`,
`alt` (`option`), `shift`, `fn` (macOS only, ignored elsewhere) and one key: `a`-`z`, `0`-`9`,
`` = - [ ] ' ; \ , / . ` ``, `return` / `enter`, `tab`, `space`, `delete` / `backspace`, `escape` / `esc`,
`f1`-`f12`, `home`, `end`, `pageup`, `pagedown`, `forwarddelete`, `left`, `right`, `up`, `down`. An unknown name or a
second key is a scenario error. `text` types characters (macOS: ASCII; Windows and Linux: UTF-8).

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
| `sdctl ws <ws://host:port/path> [message ...] [--token T] [--count N] [--seconds N]` | Open a WebSocket with the daemon's client; prints what arrives (`ws://127.0.0.1:8090/api/v1/ws '{"op":"join","v":1,"name":"deck","model":"elgato.streamdeck.plus"}' --token unc_…` joins the surface topic) |
| `sdctl decks [--api PORT] [--json]` | The daemon's logical decks (`GET /api/v1/decks`, port 37053 by default) |
| `sdctl emu-snapshot --out f.png --api PORT [--deck SERIAL]` | The deck's viewers save a PNG through the daemon (device wire v2 `Control`); waits up to 3 s for the file |

### `sdctl emu-*` and the device wire

| Command | Wire | Why |
|:--|:--|:--|
| `emu-snapshot --api PORT` | v2, through the daemon | A snapshot is of a deck: the daemon knows its viewers and asks them (`Control`) |
| `emu-snapshot` (no `--api`), `emu-model`, `emu-record` | v1, against `unreal-streamdeck-studio --listen` | In v2 a viewer's model is the viewer's own choice (a mirror follows the deck, an own deck is picked in the studio or with its `PUT /api/v1/connection`) and recording is the studio's (`POST /api/v1/recording` on its port): there is nothing for the daemon to forward. They stay as the protocol tests of the v1 server, with `sdctl --device emu:...` |

```
unreal-streamdeck run my-scenario.yaml &    # the default scenario ships with SD5; no file: the idle screen
sdctl viewer --seconds 3                  # mirror of the first deck: replay, then live frames
sdctl viewer --own neo --seconds 3        # a Neo of its own, with its own runtime
```
