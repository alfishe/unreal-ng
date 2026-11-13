# Unreal NG on a Steam Deck: build, install, drive, check

> **Transport.** Everything here runs over SSH to the Deck (`ssh deck@<host>`). The emulator on the Deck has the usual
> WebAPI on `http://127.0.0.1:8090` and MCP on `:8092`, reachable from the Deck itself (forward a port with
> `ssh -L 8090:127.0.0.1:8090` to drive it from another machine). What the bundle holds and installs:
> [install/steamdeck/README.md](../../install/steamdeck/README.md).

## Build the bundle

The release workflow builds `UnrealNG-SteamDeck-x86_64.tar.gz` after the Linux AppImage. By hand, in the CI image on
an x86_64 Linux host (not under emulation on an ARM Mac):

```bash
# the release configuration, the suite staging and the AppImage, as .github/workflows/release.yml does
docker run --rm -v "$PWD":/src -e UNREAL_PACKAGE_VERSION=$(date +%Y%m%d.%H%M%S) \
  ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04 bash -c '
    cd /src && apt-get update -qq && apt-get install -y -qq libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
      libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libegl-dev libdecor-0-dev
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_APPS=ON -DTESTS=OFF -DBENCHMARKS=OFF \
      -DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF -DQT_INSTALL_PATH=/opt/qt/current -DUNREAL_BUNDLE_QT=ON \
      -DUNREAL_STREAMDECK=ON -DUNREAL_PACKAGE_VERSION=$UNREAL_PACKAGE_VERSION \
      -DUNREAL_PACKAGE_BASENAME=UnrealNG-Suite-Linux-x86_64
    cmake --build build && cmake --build build --target package_suite_linux && bash tools/package-appimage.sh build'
install/steamdeck/package.sh build                    # -> build/packages/UnrealNG-SteamDeck-x86_64.tar.gz
```

Pitfalls:

- In a git worktree the container must also see the main repository's `.git` (the worktree's `.git` file points
  there): add `-v <main>/.git:<main>/.git:ro`, or the configure fails with "not a git repository".
- Images without the X11 / Wayland dev headers fail SDL3's configure ("no desktop windows"): the `apt-get` line above.
- Packaging on macOS works too (`package.sh` writes the tar without extended attributes).

## Install or update

```bash
install/steamdeck/package.sh build --deploy deck@<host>   # copy, unpack in a temporary folder, run install.sh
```

or on the Deck: `tar xzf UnrealNG-SteamDeck-x86_64.tar.gz && UnrealNG-SteamDeck/install.sh`. The output says each
step:

```
app: /home/deck/Applications/unreal-ng (20261108.121658)
launcher: /home/deck/.local/share/applications/unreal-ng.desktop
Steam library: up to date                       # or: Unreal NG (3357196967) with its artwork in ...
Stream Deck udev rule: installed
```

- The library entry and its artwork are written while Steam is stopped; the installer stops and starts Steam only when
  something changes, and leaves it alone while a game runs ("a game is running, not changed": close it, run again).
- The udev rule needs sudo: `ssh -t deck@<host> /home/deck/Applications/unreal-ng/install.sh --udev`. Write the full
  path: a `~` in the command expands on the machine you type it on, not on the Deck.
- Never edit `shortcuts.vdf` while Steam runs (it writes the file back on exit): use `steam-library.py`, which refuses.

## Start it in Game Mode and look at the screen

The Steam game id of a non-Steam entry is `(appid << 32) | 0x02000000`; the app id is stable
(`crc32('"<exe>"' + name) | 0x80000000`, 3357196967 for the default install):

```bash
ssh deck@<host> '
  ~/Applications/unreal-ng/steam-library.py list                 # app ids of the non-Steam games
  steam steam://rungameid/$(( (3357196967 << 32) | 0x02000000 )) &
  for i in $(seq 30); do curl -s -m1 127.0.0.1:8090/api/v1/emulator | grep -q PENTAGON && break; sleep 1; done
  DISPLAY=:0 xprop -root GAMESCOPE_FOCUSED_APP                   # 3357196967 = our window has the focus
  gamescopectl screenshot /tmp/deck.png'                          # the composed screen, what the user sees
scp deck@<host>:/tmp/deck.png scratch/
```

- `steam://nav/games/details/<appid>` opens the entry's library page (check the artwork).
- `GAMESCOPE_FOCUSED_APP` 769 is Steam's own UI: the game runs but the user is in the Steam menu or the Quick
  Access menu ("…"). Read it on Steam's X display `:0`; the game runs on `:1`, whose root has no such property.
  unreal-qt checks it every 50 ms (`platform/linux/steamoverlay.h`): while it is 769 the app is not the controllers'
  owner, so neither the machine nor the handheld menu / on-screen keyboard read the pads (they reach every reader
  of the device, Steam's menu included). The log says "Steam's menu is over the window, the controllers are Steam's".
- Watch the focus while someone opens the menus: `DISPLAY=:0 xprop -root -spy GAMESCOPE_FOCUSED_APP`.
- Mute what you start: `PUT /api/v1/emulator/<id>/audio/mixer/master {"muted":true}`.
- Processes started over SSH die with the session (`KillUserProcesses`): start through Steam as above, or with
  `systemd-run --user`.
- In Game Mode the window has no menu: everything goes through the WebAPI / MCP (or the Stream Deck).

## Load a game, show the border

```bash
ssh deck@<host> '
  I=$(curl -s 127.0.0.1:8090/api/v1/emulator | python3 -c "import sys,json; print(json.load(sys.stdin)[\"emulators\"][0][\"id\"])")
  # a disk / tape / snapshot with autostart (the reply carries automation_session.id: send it as X-Unreal-Session)
  curl -s -X POST 127.0.0.1:8090/api/v1/controllers/actions/media.open -H "Content-Type: application/json" \
    -d "{\"instance\":\"$I\",\"params\":{\"path\":\"/home/deck/Documents/UnrealNG/Games/<game>.trd\",\"autostart\":true}}"
  # the border: the WINDOW view, full / screen / content (the Deck starts in content: the picture, no border)
  curl -s -X PUT 127.0.0.1:8090/api/v1/emulator/$I/video/window -H "Content-Type: application/json" -d "{\"view\":\"full\"}"
  # end the implicit session the action opened
  curl -s -X DELETE 127.0.0.1:8090/api/v1/emulator/$I/control/session -H "X-Unreal-Session: <session>"'
```

- The border is the window view, not `video.viewport`: [media/window-view-border.md](../media/window-view-border.md).
- Games go in `~/Documents/UnrealNG/Games/<name>/` (the handheld menu's Library root); copy them with `scp`.

## Check the Stream Deck

With an Elgato Stream Deck on the Deck's USB-C:

```bash
ssh deck@<host> '
  lsusb | grep 0fd9                                     # the deck on the bus
  ls -l /dev/hidraw*                                     # "+" = the uaccess ACL from the udev rule
  pgrep -af unreal-streamdeck                            # started by unreal-qt from the AppImage (--host 127.0.0.1:8090)
  cat ~/.config/unreal-streamdeck/companion.json         # the token: paired
  grep -i "companion pair" ~/.local/state/unreal-ng/unreal-ng.log'   # "approved (this host)"
```

- No daemon: unreal-qt starts it only when a deck is plugged in and Tools > Stream Deck is not off.
- `NOT PAIRED` on the strip: tap the strip; a request from the same host is approved at once (no code). Turning
  pairing requests off (Tools > Companions) refuses it too.
- The window jumping between maximised and normal on each press is the deck's evdev keyboard reaching the window
  manager: the daemon grabs it (`EVIOCGRAB`); an old daemon without the grab shows it.

## Logs

`~/.local/state/unreal-ng/unreal-ng.log` (unreal-qt and the daemon it starts). The installed version:
`~/Applications/unreal-ng/VERSION`.
