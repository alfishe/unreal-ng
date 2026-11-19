# Unreal NG on the Steam Deck

The Steam Deck bundle, `UnrealNG-SteamDeck-x86_64.tar.gz`, is the Linux suite AppImage with an installer. It ships
with every release (the GitHub release's downloads). The installer puts the emulator in the Steam library as a
non-Steam game, with its own artwork, so it starts from Game Mode like any other game.

Recipes for building, installing and checking it from another machine are in
[.recipe/testing/steamdeck.md](../../.recipe/testing/steamdeck.md); the scripts that do it (a build on a remote Linux
host, the install over SSH, a check of the installed build) are in
[tools/deploy/steamdeck](../../tools/deploy/steamdeck/README.md).

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
app: /home/deck/Applications/unreal-ng (<version>)
launcher: /home/deck/.local/share/applications/unreal-ng.desktop
Steam library: Unreal NG (3357196967) with its artwork in /home/deck/.local/share/Steam/userdata/<account>
Steam: started again
Steam library: Unreal NG registered
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

Steam reads its library and the artwork when it starts and writes the library back when it stops. So every run of
the installer (an install, an update, a redeploy) re-registers the entry: it stops Steam for a moment, rewrites the
entry and its artwork, starts Steam again and checks that the entry is there (`Steam library: Unreal NG registered`).
The app id stays the same, so the Game Mode tile, its play time and its launch id stay. An older entry named
"Unreal NG" that points to another executable is replaced. In Game Mode Steam is the user unit
`steam-launcher.service`, which the Game Mode session restarts at once; the installer masks it in `/run` while it
writes and always unmasks and starts it afterwards. If a game is running, the installer leaves the library alone and
says so: close the game and run it again.

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
| `steam-library.py add --name N --exe E --art DIR [--icon --start-dir --shortcut-path --launch-options]` | Add or update the entry with its artwork, replacing older entries of the same executable or name (refuses while Steam runs) |
| `steam-library.py remove --exe E` | Remove the entries of an executable and their artwork (refuses while Steam runs) |
| `steam-library.py art --match S --art DIR` | Artwork only, for entries whose executable contains `S` (entries Steam added itself) |

See [Steam Library Shortcuts](https://developer.valvesoftware.com/wiki/Steam_Library_Shortcuts).
