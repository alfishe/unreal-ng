#!/usr/bin/env bash
# Install the deployed emulator as an app on the Steam Deck: a launcher in the KDE menu and on the desktop
# (Desktop Mode) and a non-Steam game in the Steam library (Game Mode). Runs on the Deck; deploy.sh calls it.
#
#   ~/steamdeck-poc/deck-install.sh             # install or refresh (safe to repeat)
#   ~/steamdeck-poc/deck-install.sh --remove    # remove the launchers (the Steam entry: Steam -> Manage -> Remove)
#
# The app is p01 with --boot: the ROM starts fullscreen, the WebAPI (:8090) drives it. Steam adds the library
# entry only while it runs (Desktop Mode or Game Mode): without it the step is skipped, run the script again later.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ID="unreal-ng-deck"
NAME="Unreal NG"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
DESKTOP_FILE="$APPS/$ID.desktop"

if [ "${1:-}" = "--remove" ]; then
  rm -f "$DESKTOP_FILE" "$ICONS/$ID.png" "$HOME/Desktop/$ID.desktop"
  echo "launchers removed; the Steam library entry stays (Steam -> Manage -> Remove non-Steam game)"
  exit 0
fi

mkdir -p "$APPS" "$ICONS"
cp "$DIR/$ID.png" "$ICONS/$ID.png"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=$NAME
Comment=ZX Spectrum emulator (WebAPI on port 8090)
Exec="$DIR/p01-sdl-gpu-latency" --boot
Path=$DIR
Icon=$ID
Terminal=false
Categories=Game;Emulator;
EOF
chmod +x "$DESKTOP_FILE"
if [ -d "$HOME/Desktop" ]; then
  cp "$DESKTOP_FILE" "$HOME/Desktop/$ID.desktop"
  chmod +x "$HOME/Desktop/$ID.desktop"
fi
update-desktop-database "$APPS" 2>/dev/null || true
echo "launcher: $DESKTOP_FILE"

# Steam keeps non-Steam games in userdata/<account>/config/shortcuts.vdf (it reads Exec from the launcher): the
# entry is there already when the file names the binary
if grep -qs "p01-sdl-gpu-latency" "$HOME"/.local/share/Steam/userdata/*/config/shortcuts.vdf; then
  echo "Steam library: already there"
elif pgrep -x steam > /dev/null; then
  steamos-add-to-steam "$DESKTOP_FILE"
  echo "Steam library: added (Game Mode: Library -> Non-Steam)"
else
  echo "Steam library: Steam is not running, skipped (run this script again with Steam up)"
fi
