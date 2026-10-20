# unreal-streamdeck: the Stream Deck daemon

`unreal-streamdeck` owns the attached Stream Deck. It plays a scenario on it (menus, actions, the touch strip) and
serves software decks (Stream Deck Studio windows) over the device wire. The design is in
[docs/inprogress/2026-10-19-streamdeck-integration](../../inprogress/2026-10-19-streamdeck-integration/README.md).
Built with `-DUNREAL_STREAMDECK=ON`.

## Commands

| Command | What |
|:--|:--|
| `unreal-streamdeck run <scenario> [options]` | Play a scenario (`.json`, `.yaml`, `.csv`, `.tsv`) on the deck |
| `unreal-streamdeck pair [--webapi URL] [--name NAME] [--file F]` | Pair with unreal-ng as a companion (approved at the emulator's GUI); the token goes to `~/.config/unreal-streamdeck/companion.json` (mode 0600) |
| `unreal-streamdeck scenario check \| convert \| extract \| resolve ...` | Check and convert scenario files |

`run` options:

| Option | Meaning |
|:--|:--|
| `--watch` | Reload the scenario when it or an included file changes (the menu stack is kept) |
| `--api PORT` | Automation on `127.0.0.1:PORT`: REST (`/api/v1/state`, navigate, press, reload, scenario) and MCP (`/mcp`) |
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
continues.

An older Stream Deck Studio that listens on 37050 itself takes the port: `run` then says so and runs without viewers.
`--mirror` / `--device emu` still talk to such a studio (the device wire v1).

## Threads

| Thread | Does |
|:--|:--|
| Device (one per deck, the main one for the real deck) | Reads input, runs the scenario, writes HID reports |
| Network | The event loop: viewers and the device wire (`sdb/eventloop.h`, `sdb/deckhub.h`) |
| Runtime workers | Actions and the time-travel readout, per runtime (both go with the surface client, SD5) |
| Automation | With `--api`: one worker |

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
installs it to `/usr/lib/udev/rules.d`; for an AppImage or a build from source:

```bash
sudo install -m 0644 companions/streamdeck/linux/70-unreal-streamdeck.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# then unplug the deck and plug it back in
```

`uaccess` needs a local login session. A user without a seat (SSH only, a service account) needs a group instead:
copy the rule with `MODE="0660", GROUP="plugdev"` in place of `TAG+="uaccess"` and add the user to the group.

## Developer checks (`sdctl`)

| Command | What |
|:--|:--|
| `sdctl viewer [--port N] [--deck SERIAL \| --own MODEL] [--seconds N]` | Join the daemon as a viewer; prints the welcome, the replayed picture and frame counts per second |
| `sdctl ws <ws://host:port/path> [message ...] [--token T] [--count N] [--seconds N]` | Open a WebSocket with the daemon's client; prints what arrives |

```
unreal-streamdeck run my-scenario.yaml &    # the default scenario ships with SD5
sdctl viewer --seconds 3                  # mirror of the first deck: replay, then live frames
sdctl viewer --own neo --seconds 3        # a Neo of its own, with its own runtime
```
