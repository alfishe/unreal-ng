#!/usr/bin/env bash
# Run a POC binary inside a Steam Linux Runtime container on the Deck, as Steam would for a native game
# whose Linux Runtime is set to that runtime.
#
#   ~/steamdeck-poc/run-in-slr.sh [--runtime 4|sniper] ./p08-input-inspector [args]
#
# Without Steam, the runtime has to be installed once: steam://install/1628350 (sniper); SLR 4.0 installs
# with the first game that uses it, or from the Steam library (Tools). The script finds the runtime under
# the usual Steam library folders and prints what it ran with.
set -euo pipefail

WANT="4"
if [ "${1:-}" = "--runtime" ]; then WANT="$2"; shift 2; fi
[ $# -gt 0 ] || { sed -n '2,10p' "$0"; exit 2; }

case "$WANT" in
  4) PATTERN="SteamLinuxRuntime_4*" ;;
  sniper|3) PATTERN="SteamLinuxRuntime_sniper" ;;
  *) PATTERN="SteamLinuxRuntime_$WANT" ;;
esac

RUNTIME=""
for lib in "$HOME/.local/share/Steam/steamapps/common" "$HOME/.steam/steam/steamapps/common" /run/media/*/steamapps/common; do
  for dir in $lib/$PATTERN; do
    [ -x "$dir/run" ] && RUNTIME="$dir" && break 2
  done
done
[ -n "$RUNTIME" ] || { echo "runtime $WANT not found (looked for $PATTERN)" >&2; exit 1; }

cd "$(dirname "$0")"
echo "runtime: $RUNTIME"
exec "$RUNTIME/run" -- "$@"
