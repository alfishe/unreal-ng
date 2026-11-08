#!/usr/bin/env bash
# Install Unreal NG on a Steam Deck from the unpacked bundle (package.sh builds it): the app in ~/Applications/unreal-ng,
# a launcher in the KDE menu (Desktop Mode), a non-Steam game "Unreal NG" in the Steam library (Game Mode) with its
# artwork, and the Stream Deck's udev rule when asked. Runs on the Deck, as the user; safe to repeat (an update).
#
#   ./install.sh              # install or update
#   ./install.sh --udev       # also the Stream Deck udev rule (sudo asks for the password once)
#   ./install.sh --remove     # remove the app, the launcher and the Steam library entry
#
# Steam reads its library (shortcuts.vdf) and the artwork (grid/) when it starts and writes the library back when it
# stops, so the entry and its pictures are written with Steam stopped (steam-library.py): Steam is stopped for a
# moment and started again (Game Mode's session starts it by itself). Not while a game runs: the script says so and
# leaves the library alone; run it again afterwards.
set -euo pipefail
# The whole script is one block, parsed before it runs: an update that rewrites this file while it runs (install.sh
# copies itself into the app) cannot make bash read half of the new file
{

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$HOME/Applications/unreal-ng"
ID="unreal-ng"
NAME="Unreal NG"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
DESKTOP_FILE="$APPS/$ID.desktop"
LAUNCHER="$APP_DIR/unreal-ng.sh"
RULE="70-unreal-streamdeck.rules"
UDEV=0
REMOVE=0

for arg in "$@"; do
  case "$arg" in
    --remove) REMOVE=1 ;;
    --udev) UDEV=1 ;;
    -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

# Steam stopped around a library change and started again as it was; false when a game runs (it would end)
STEAM_WAS_RUNNING=0
GAME_MODE=0
stop_steam() {
  pgrep -x steam > /dev/null || return 0
  if pgrep -f "reaper SteamLaunch" > /dev/null; then
    echo "Steam library: a game is running, not changed (close it and run this script again)"
    return 1
  fi
  STEAM_WAS_RUNNING=1
  pgrep -f gamescope-session > /dev/null && GAME_MODE=1
  steam -shutdown > /dev/null 2>&1 &
  for _ in $(seq 1 120); do pgrep -x steam > /dev/null || return 0; sleep 0.5; done
  echo "Steam library: Steam did not stop, not changed" >&2
  return 1
}
start_steam() {
  [ "$STEAM_WAS_RUNNING" = 1 ] || return 0
  if [ "$GAME_MODE" = 0 ]; then   # Game Mode's session starts Steam again by itself
    systemd-run --user --quiet --collect steam > /dev/null 2>&1 || (setsid steam > /dev/null 2>&1 &)
  fi
  for _ in $(seq 1 120); do pgrep -x steam > /dev/null && break; sleep 0.5; done
  echo "Steam: started again"
}

if [ "$REMOVE" = 1 ]; then
  if [ -f "$APP_DIR/steam-library.py" ] && python3 -I "$APP_DIR/steam-library.py" list | grep -qF "\"$LAUNCHER\"" &&
     stop_steam; then
    python3 -I "$APP_DIR/steam-library.py" remove --exe "$LAUNCHER" || true
    start_steam
  fi
  rm -rf "$APP_DIR"
  rm -f "$DESKTOP_FILE" "$ICONS/$ID.png" "$HOME/Desktop/$ID.desktop"
  update-desktop-database "$APPS" 2>/dev/null || true
  echo "removed $APP_DIR and the launcher"
  exit 0
fi

[ -f "$SRC/UnrealNG.AppImage" ] || { echo "no UnrealNG.AppImage beside $0" >&2; exit 1; }

# The app: copied whole, then switched in (an update never leaves half an app behind)
mkdir -p "$APP_DIR"
if [ "$SRC" != "$APP_DIR" ]; then
  cp "$SRC/UnrealNG.AppImage" "$APP_DIR/UnrealNG.AppImage.new"
  chmod +x "$APP_DIR/UnrealNG.AppImage.new"
  mv -f "$APP_DIR/UnrealNG.AppImage.new" "$APP_DIR/UnrealNG.AppImage"
  install -m 0755 "$SRC/unreal-ng.sh" "$SRC/install.sh" "$SRC/steam-library.py" "$APP_DIR/"
  rm -rf "$APP_DIR/art"
  cp -R "$SRC/art" "$APP_DIR/art"
  [ -f "$SRC/VERSION" ] && cp "$SRC/VERSION" "$APP_DIR/VERSION"
fi
echo "app: $APP_DIR ($(cat "$APP_DIR/VERSION" 2>/dev/null || echo 'version unknown'))"

# The launcher (Desktop Mode menu and desktop)
mkdir -p "$APPS" "$ICONS"
cp "$APP_DIR/art/icon.png" "$ICONS/$ID.png"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=$NAME
Comment=ZX Spectrum emulator
Exec=$LAUNCHER
Path=$APP_DIR
Icon=$ID
Terminal=false
Categories=Game;Emulator;
EOF
chmod +x "$DESKTOP_FILE"
if [ -d "$HOME/Desktop" ]; then
  install -m 0755 "$DESKTOP_FILE" "$HOME/Desktop/$ID.desktop"
fi
update-desktop-database "$APPS" 2>/dev/null || true
echo "launcher: $DESKTOP_FILE"

# The Steam library entry with its poster, wide capsule, hero, logo and icon, written while Steam is stopped; Steam
# is left alone when they are in place already
if python3 -I "$APP_DIR/steam-library.py" check --name "$NAME" --exe "$LAUNCHER" --art "$APP_DIR/art"; then
  echo "Steam library: up to date"
elif stop_steam; then
  python3 -I "$APP_DIR/steam-library.py" add --name "$NAME" --exe "$LAUNCHER" --start-dir "$APP_DIR" \
    --icon "$APP_DIR/art/icon.png" --shortcut-path "$DESKTOP_FILE" --art "$APP_DIR/art" || true
  start_steam
fi

# The Stream Deck (Elgato) over USB: the daemon in the AppImage reads it through hidraw, which needs the udev rule
APPIMAGE_RULE="usr/lib/udev/rules.d/$RULE"
if [ -f "/etc/udev/rules.d/$RULE" ]; then
  echo "Stream Deck udev rule: installed"
elif [ "$UDEV" = 1 ]; then
  TMP="$(mktemp -d)"
  (cd "$TMP" && "$APP_DIR/UnrealNG.AppImage" --appimage-extract "$APPIMAGE_RULE" > /dev/null)
  echo "Stream Deck udev rule: installing to /etc/udev/rules.d (sudo asks for the password of $(id -un))"
  if sudo install -m 0644 "$TMP/squashfs-root/$APPIMAGE_RULE" "/etc/udev/rules.d/$RULE" &&
     sudo udevadm control --reload-rules &&
     sudo udevadm trigger --subsystem-match=hidraw --subsystem-match=usb; then
    echo "Stream Deck udev rule: installed (replug the deck)"
  else
    echo "Stream Deck udev rule: not installed (sudo failed; over SSH use ssh -t, a password is set with passwd)" >&2
  fi
  rm -rf "$TMP"
else
  echo "Stream Deck udev rule: not installed (for an Elgato Stream Deck: $APP_DIR/install.sh --udev)"
fi
exit 0
}
