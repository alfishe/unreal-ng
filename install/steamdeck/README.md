# Unreal NG on the Steam Deck

The Steam Deck bundle, `UnrealNG-SteamDeck-x86_64.tar.gz`, is the Linux suite AppImage with an installer. It ships
with every release (the GitHub release's downloads). The installer puts the emulator in the Steam library as a
non-Steam game, with its own artwork, so it starts from Game Mode like any other game.

Recipes for building, installing and checking it from another machine are in
[.recipe/testing/steamdeck.md](../../.recipe/testing/steamdeck.md).

## What is in the bundle

| File | What it is |
|:--|:--|
| `UnrealNG.AppImage` | The Linux suite AppImage, the same file as the release's `UnrealNG-Suite-Linux-x86_64.AppImage`. It contains unreal-qt with its own Qt, the Stream Deck daemon `unreal-streamdeck` with its scenarios and icon packs, the other suite tools, and the Stream Deck udev rule (`usr/lib/udev/rules.d/70-unreal-streamdeck.rules`) |
| `install.sh` | The installer. It also updates an existing install, and with `--remove` uninstalls |
| `unreal-ng.sh` | The launcher that Steam and the menu entry start. In Game Mode it runs Qt on XWayland and writes the log |
| `steam-library.py` | Writes the Steam library entry and its artwork (`shortcuts.vdf`, `grid/`) |
| `art/` | The library artwork: `poster.png` 600x900, `capsule.png` 920x430, `hero.png` 1920x620, `logo.png` 1280x400, `icon.png` 256x256 |
| `VERSION` | The package version (`YYYYMMDD.HHMMSS`) |
| `README.md` | This file |

Debug symbols: the bundle carries the Linux AppImage, so its symbols are the release's
`UnrealNG-Symbols-Linux-x86_64.tar.gz` (rolling snapshots).

## Install

1. Copy the bundle to the Deck. In Desktop Mode open a terminal (Konsole).
2. Unpack it and run the installer:

   ```sh
   tar xzf UnrealNG-SteamDeck-x86_64.tar.gz
   UnrealNG-SteamDeck/install.sh
   ```

3. Switch to Game Mode. "Unreal NG" is in Library > Non-Steam, with its artwork.

Running `install.sh` again (from a new bundle, or `~/Applications/unreal-ng/install.sh`) updates the install. It
prints one line per step:

```
app: /home/deck/Applications/unreal-ng (20261108.121658)
launcher: /home/deck/.local/share/applications/unreal-ng.desktop
Steam library: Unreal NG (3357196967) with its artwork in /home/deck/.local/share/Steam/userdata/58956458
Stream Deck udev rule: installed
```

The installer sets up:

| What | Where |
|:--|:--|
| The app and its launcher | `~/Applications/unreal-ng/` (`UnrealNG.AppImage`, `unreal-ng.sh`, `install.sh`, `steam-library.py`, `art/`) |
| The menu entry and desktop icon | `~/.local/share/applications/unreal-ng.desktop` (Desktop Mode) |
| The Steam library entry "Unreal NG" (non-Steam game) | `userdata/<account>/config/shortcuts.vdf` in every Steam account on the Deck (a backup goes to `shortcuts.vdf.bak`) |
| The artwork: poster, wide capsule, hero, logo, icon | `userdata/<account>/config/grid/<appid>*.png` |
| The Stream Deck udev rule | `/etc/udev/rules.d/70-unreal-streamdeck.rules`, only with `--udev` (asks for the sudo password) |

Steam reads its library and the artwork when it starts and writes the library back when it stops. So when the entry or
its artwork needs a change (the first install, new artwork), the installer stops Steam for a moment, writes them, and
starts Steam again (in Game Mode the session restarts Steam by itself). When both are in place already it leaves Steam
alone and says `Steam library: up to date`. If a game is running while something needs a change, the installer leaves
the library alone and says so: close the game and run it again.

To uninstall, run `~/Applications/unreal-ng/install.sh --remove`. That removes the app, the menu entry, and the
Steam library entry with its artwork. The settings stay in `~/.config` (unreal-qt and `unreal-streamdeck/`).

The log goes to `~/.local/state/unreal-ng/unreal-ng.log`.

## Game Mode

When started from the library in Game Mode (the gamescope session, `XDG_CURRENT_DESKTOP=gamescope`), unreal-qt:

- opens full screen, without the menu, toolbars or status bar, and stays that way (the fullscreen shortcut does not
  leave it);
- starts a Pentagon 128K, unless a file is named on the command line;
- scales the picture to the panel's height with its aspect ratio kept.

The code is in [`unreal-qt/src/platform/linux/steamdeckgamemode.h`](../../unreal-qt/src/platform/linux/steamdeckgamemode.h)
and is built on Linux only. Desktop Mode gets the usual window with its menu. gamescope never confirms the fullscreen
state to the window, so the module hides the menu, toolbars and status bar itself and keeps them hidden.

The WebAPI listens on `http://127.0.0.1:8090` and MCP on `:8092`, as on the desktop.

## Stream Deck

An Elgato Stream Deck (any model, including the Stream Deck + XL) works when plugged into the Deck's USB-C port,
directly or through a dock. To turn it on:

1. Install the udev rule once. Without it only root can open the deck. In Konsole:

   ```sh
   ~/Applications/unreal-ng/install.sh --udev
   ```

   Over SSH use `ssh -t deck@<host> /home/deck/Applications/unreal-ng/install.sh --udev`. Write the full path,
   because a `~` would expand on the machine you type it on. sudo asks for the password of `deck`; set one with
   `passwd` if there is none. Then unplug the Stream Deck and plug it back in.
2. Start Unreal NG. unreal-qt starts the daemon (`unreal-streamdeck`, from the AppImage) when a deck is plugged in,
   unless Tools > Stream Deck is set to off.
3. Pair once: tap the deck's strip while it says `NOT PAIRED - TOUCH TO PAIR`. A daemon on the same machine is
   approved at once, with no code to compare. The token is kept in `~/.config/unreal-streamdeck/companion.json`, and
   later starts connect without pairing.

The daemon grabs the deck's keyboard device, the evdev node the kernel makes of the deck. Without the grab every key
press reached the window manager, and the window maximised and restored on each press.

| Symptom | Cause and fix |
|:--|:--|
| The deck stays dark | No udev rule: `ls -l /dev/hidraw*` shows no `+` (ACL). Run `install.sh --udev` and replug |
| A tap on `NOT PAIRED` ends in a refusal | Pairing requests are turned off in Tools > Companions. Turn them on |
| `COMPANION REVOKED - TOUCH TO PAIR` | The companion was removed in Tools > Companions. Tap to pair again |
| No daemon (`pgrep -af unreal-streamdeck` is empty) | Tools > Stream Deck is off, or no deck is plugged in |

Daemon details: [docs/companions/streamdeck/daemon.md](../../docs/companions/streamdeck/daemon.md).

## Build

The release workflow (`.github/workflows/release.yml`, job `build-linux`) builds the bundle right after the AppImage,
checks what it contains, and uploads it as `UnrealNG-SteamDeck-x86_64.tar.gz`. The release job attaches it to the
GitHub release with the other downloads.

To build it by hand, make the AppImage in the release configuration (the CI image, `tools/package-appimage.sh build`;
the commands are in [the recipe](../../.recipe/testing/steamdeck.md#build-the-bundle)), then run:

```sh
install/steamdeck/package.sh build                            # build/packages/UnrealNG-SteamDeck-x86_64.tar.gz
install/steamdeck/package.sh build --deploy deck@steamdeck    # also install it on a Deck over SSH
UNREAL_PACKAGE_VERSION=20261108.120000 install/steamdeck/package.sh build   # the VERSION it carries
```

The artwork comes from [`tools/icons/generate_steam_art.py`](../../tools/icons/generate_steam_art.py), which writes
`art/` (`--preview sheet.png` draws a contact sheet). The PNGs are committed; rerun the script and commit them after a
change. The POC client in `apps/steamdeck-client` uses the same artwork.

## The Steam library entry

`steam-library.py` writes the entry the way Steam ROM Manager and steamtinkerlaunch do: into
`shortcuts.vdf` (binary KeyValues) and `grid/`, while Steam is stopped, so the entry shows with its pictures from
Steam's first start. The entry's app id is `crc32('"<exe>"' + name) | 0x80000000`. It is the same on every install,
so an update finds its own entry instead of adding another one. `steamos-add-to-steam` is not used: Steam gives the
entries it adds a random app id, and it reads their artwork only after a restart.

| Command | What it does |
|:--|:--|
| `steam-library.py list` | The non-Steam games and their app ids |
| `steam-library.py check --name N --exe E --art DIR` | Exit 0 when the entry and its artwork are in place |
| `steam-library.py add --name N --exe E --art DIR [--icon --start-dir --shortcut-path --launch-options]` | Add or update the entry with its artwork (refuses while Steam runs) |
| `steam-library.py remove --exe E` | Remove the entries of an executable and their artwork (refuses while Steam runs) |
| `steam-library.py art --match S --art DIR` | Artwork only, for entries whose executable contains `S` (entries Steam added itself) |

See [Steam Library Shortcuts](https://developer.valvesoftware.com/wiki/Steam_Library_Shortcuts).
