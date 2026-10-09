#!/usr/bin/env bash
# P-19: which shared libraries do the POC binaries need, and are they all provided by the runtime?
# Runs ldd inside the steamrt4 SDK container (the same image build.sh uses) and lists every library
# that does not resolve, plus the SDL3 version the runtime brings.
#
#   tools/poc/023-steamdeck-client/p19-runtime-packaging/check-deps.sh [--variant deck|container]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
VARIANT="deck"
[ "${1:-}" = "--variant" ] && VARIANT="$2"
IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest"
BIN="tools/poc/023-steamdeck-client/build/$VARIANT/bin"

docker run --rm --platform linux/amd64 -v "$ROOT":/src -w "/src/$BIN" "$IMAGE" bash -c '
  echo "SDL3 in runtime: $(pkg-config --modversion sdl3)"
  for f in p[0-9][0-9]-*; do
    [ -x "$f" ] || continue
    echo "== $f"
    ldd "$f" | awk "{print \"   \" \$1, \$3}"
    if ldd "$f" | grep -q "not found"; then echo "   MISSING LIBRARIES"; fi
  done
'
