#!/usr/bin/env bash
# Copy the built POCs (with configs/ and rom/) to a Steam Deck over SSH.
#
#   DECK=deck@steamdeck.local tools/poc/023-steamdeck-client/deploy.sh [--variant deck]
#   DECK=deck@192.168.1.50   tools/poc/023-steamdeck-client/deploy.sh --logs   # fetch ~/steamdeck-poc-logs back
#
# On the Deck: Desktop Mode -> enable SSH once (passwd; sudo systemctl enable --now sshd).
# Binaries land in ~/steamdeck-poc/; add each as a non-Steam game (see README.md "Running on the Deck").
# Logs come back to tools/poc/023-steamdeck-client/build/deck-logs/ (git-ignored).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
VARIANT="deck"
FETCH=0
while [ $# -gt 0 ]; do
  case "$1" in
    --variant) VARIANT="$2"; shift 2 ;;
    --logs) FETCH=1; shift ;;
    -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
: "${DECK:?set DECK=user@host of the Steam Deck}"

if [ "$FETCH" = 1 ]; then
  LOGS="$ROOT/tools/poc/023-steamdeck-client/build/deck-logs"
  mkdir -p "$LOGS"
  rsync -av "$DECK:steamdeck-poc-logs/" "$LOGS/"
  exit 0
fi

BIN="$ROOT/tools/poc/023-steamdeck-client/build/$VARIANT/bin"
[ -d "$BIN" ] || { echo "no build at $BIN - run build.sh first" >&2; exit 1; }
rsync -av --delete --include='p[0-9][0-9]-*' --include='configs/***' --include='rom/***' --include='*.sh' --exclude='*' \
  "$BIN/" "$DECK:steamdeck-poc/"
rsync -av "$ROOT/tools/poc/023-steamdeck-client/p19-runtime-packaging/run-in-slr.sh" "$DECK:steamdeck-poc/"
echo "deployed to $DECK:~/steamdeck-poc/"
