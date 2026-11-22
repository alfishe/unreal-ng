#!/usr/bin/env bash
# Install a Steam Deck bundle on a Deck over SSH: copy, unpack in a temporary folder, run its install.sh (closes a
# running Unreal NG, re-registers the Steam entry), clean up. Fails unless the entry is registered (deploy guidelines).
#
#   tools/deploy/steamdeck/deploy.sh <UnrealNG-SteamDeck-x86_64.tar.gz> [<deck>]    # <deck>: SSH target (UNREAL_DEPLOY_DECK)
#
# SSH options come from ~/.ssh/config, or UNREAL_DEPLOY_SSH="ssh -i <key>".
set -euo pipefail

bundle="${1:?usage: deploy.sh <bundle.tar.gz> [<deck>]}"
deck="${2:-${UNREAL_DEPLOY_DECK:-}}"
[ -n "$deck" ] || { echo "no Deck: pass <deck> or set UNREAL_DEPLOY_DECK" >&2; exit 2; }
[ -f "$bundle" ] || { echo "no bundle at $bundle" >&2; exit 2; }
SSH="${UNREAL_DEPLOY_SSH:-ssh}"
name="UnrealNG-SteamDeck"

$SSH -o ConnectTimeout=15 "$deck" true || { echo "the Deck does not answer over SSH (asleep?)" >&2; exit 1; }
rsync -q -e "$SSH" "$bundle" "$deck:/tmp/$name.tar.gz"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
$SSH "$deck" "set -e; d=\$(mktemp -d); tar -C \"\$d\" -xzf /tmp/$name.tar.gz; \"\$d/$name/install.sh\"; rm -rf \"\$d\" /tmp/$name.tar.gz" | tee "$log"
if grep -q "a game is running, not changed" "$log"; then
  echo "deploy: the app is installed, but a running game kept the Steam entry unchanged: close it and deploy again" >&2
  exit 3
fi
grep -q "Steam library: Unreal NG registered" "$log" || { echo "deploy: the Steam entry is not registered" >&2; exit 1; }
