#!/usr/bin/env bash
# Copy the built POCs (with the runtime data: configs/, rom/, symbols/, ...) to a Steam Deck over SSH.
#
#   DECK=deck@steamdeck.local apps/steamdeck-client/deploy.sh [--variant deck]
#   DECK=deck@192.168.1.50   apps/steamdeck-client/deploy.sh --logs   # fetch ~/steamdeck-poc-logs back
#   apps/steamdeck-client/deploy.sh --stage   # only stage the stripped copy in build/<variant>/deploy/ (USB stick)
#   ... --no-install                          # copy only: no launcher, no Steam library entry
#
# On the Deck: Desktop Mode -> enable SSH once (passwd; sudo systemctl enable --now sshd).
# Binaries land in ~/steamdeck-poc/; deck-install.sh then adds "Unreal NG" (p01 --boot) to the KDE menu, the
# desktop and the Steam library (Game Mode). Other POCs: add as non-Steam games by hand (README.md).
# Logs come back to apps/steamdeck-client/build/deck-logs/ (git-ignored).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VARIANT="deck"
IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest"
FETCH=0
STAGE_ONLY=0
INSTALL=1
while [ $# -gt 0 ]; do
  case "$1" in
    --variant) VARIANT="$2"; shift 2 ;;
    --logs) FETCH=1; shift ;;
    --stage) STAGE_ONLY=1; shift ;;
    --no-install) INSTALL=0; shift ;;
    -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ "$STAGE_ONLY" = 1 ] || : "${DECK:?set DECK=user@host of the Steam Deck}"

if [ "$FETCH" = 1 ]; then
  LOGS="$ROOT/apps/steamdeck-client/build/deck-logs"
  mkdir -p "$LOGS"
  rsync -av "$DECK:steamdeck-poc-logs/" "$LOGS/"
  exit 0
fi

BIN="$ROOT/apps/steamdeck-client/build/$VARIANT/bin"
[ -d "$BIN" ] || { echo "no build at $BIN - run build.sh first" >&2; exit 1; }
# The binaries and the runtime data the core reads next to them (cmake/RuntimeData.cmake), plus the WebAPI's web
# pages (resources/html) for automation builds
INCLUDES=(--include='p[0-9][0-9]-*' --include='*.sh')
for dir in configs rom symbols fonts midi boot controllers resources; do INCLUDES+=(--include="$dir/***"); done
# The copy goes without debug info (p01: ~530 MB with it, ~30 MB without; symbols stay for backtraces), stripped in
# the SDK image build.sh uses; the build keeps the full binaries for crash analysis
STAGE="$ROOT/apps/steamdeck-client/build/$VARIANT/deploy"
mkdir -p "$STAGE"
rsync -a --delete "${INCLUDES[@]}" --exclude='*' "$BIN/" "$STAGE/"
docker run --rm --platform linux/amd64 -v "$STAGE":/stage -e HOST_IDS="$(id -u):$(id -g)" "$IMAGE" \
  bash -c 'strip --strip-debug /stage/p[0-9][0-9]-* && chown -R "$HOST_IDS" /stage'
cp "$ROOT/apps/steamdeck-client/p19-runtime-packaging/run-in-slr.sh" "$ROOT/apps/steamdeck-client/deck-install.sh" "$STAGE/"
cp "$ROOT/unreal-qt/resources/icons/app/appicon_256.png" "$STAGE/unreal-ng-deck.png"
if [ "$STAGE_ONLY" = 1 ]; then
  echo "staged: $STAGE"
  exit 0
fi
rsync -av --delete "$STAGE/" "$DECK:steamdeck-poc/"
echo "deployed to $DECK:~/steamdeck-poc/"
if [ "$INSTALL" = 1 ]; then ssh "$DECK" steamdeck-poc/deck-install.sh; fi
