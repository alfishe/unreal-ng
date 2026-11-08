#!/bin/sh
# Unreal NG on the Steam Deck: the launcher the Steam library entry and the KDE menu start (install.sh puts it beside
# the AppImage). In Game Mode (gamescope) unreal-qt goes full screen without its chrome and starts a Pentagon 128K
# (unreal-qt/src/platform/linux/steamdeckgamemode.h); there it runs on XWayland, the path gamescope composes for
# every non-Steam game. The output goes to ~/.local/state/unreal-ng/unreal-ng.log (Steam shows none).
DIR="$(dirname "$(readlink -f "$0")")"
LOG_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/unreal-ng"
mkdir -p "$LOG_DIR"
if [ "${XDG_CURRENT_DESKTOP:-}" = "gamescope" ]; then
    export QT_QPA_PLATFORM=xcb
fi
exec "$DIR/UnrealNG.AppImage" "$@" > "$LOG_DIR/unreal-ng.log" 2>&1
