#!/usr/bin/env bash
# Build the Steam Deck POCs inside the Steam Linux Runtime 4.0 SDK container (amd64, as on the Deck).
#
#   tools/poc/023-steamdeck-client/build.sh                 # every POC target
#   tools/poc/023-steamdeck-client/build.sh p08-input-inspector
#   tools/poc/023-steamdeck-client/build.sh --clean
#   tools/poc/023-steamdeck-client/build.sh --type Debug
#   tools/poc/023-steamdeck-client/build.sh --march x86-64      # baseline ISA: runs under Docker's amd64
#                                                               # emulation on a Mac (no AVX2), for smoke runs
#
# Output: tools/poc/023-steamdeck-client/build/deck/bin/ (x86-64-v3, for the Deck) or build/container/bin/
# (--march x86-64), git-ignored, with configs/ and rom/ staged next to the binaries, ready for deploy.sh. On Apple Silicon the amd64 container runs emulated (Rosetta / qemu):
# the first core build takes a while, later builds are incremental.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest"
TYPE="Release"
MARCH="x86-64-v3"
TARGETS=()
CLEAN=0
CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
JOBS=$(( CORES * 3 / 4 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))

while [ $# -gt 0 ]; do
  case "$1" in
    --type) TYPE="$2"; shift 2 ;;
    --march) MARCH="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --image) IMAGE="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) TARGETS+=("$1"); shift ;;
  esac
done
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=(steamdeck-poc-all)

POC="tools/poc/023-steamdeck-client"
case "$MARCH" in
  x86-64-v3) VARIANT="deck" ;;
  x86-64)    VARIANT="container" ;;
  *)         VARIANT="container-$MARCH" ;;
esac
[ "$TYPE" = "Release" ] || VARIANT="$VARIANT-$(echo "$TYPE" | tr '[:upper:]' '[:lower:]')"
BUILDDIR="$POC/build/$VARIANT"
[ "$CLEAN" = 1 ] && rm -rf "${ROOT:?}/$BUILDDIR"

# A worktree's .git points into the main repository: mount it read-only at its own path
MOUNTS=()
GIT_COMMON="$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
case "$GIT_COMMON" in
  ""|"$ROOT"/*) ;;
  *) MOUNTS+=(-v "$GIT_COMMON:$GIT_COMMON:ro") ;;
esac

docker run --rm --platform linux/amd64 \
  -v "$ROOT":/src -w /src ${MOUNTS[@]+"${MOUNTS[@]}"} -e GIT_OPTIONAL_LOCKS=0 \
  -e TYPE="$TYPE" -e JOBS="$JOBS" -e BUILDDIR="/src/$BUILDDIR" -e TARGETS="${TARGETS[*]}" -e MARCH="$MARCH" \
  "$IMAGE" bash -c '
    set -e
    git config --global --add safe.directory "*"
    cmake -S . -B "$BUILDDIR" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" \
          -DBUILD_QT_APPS=OFF -DENABLE_AUTOMATION=OFF -DTESTS=OFF \
          -DBUILD_STEAMDECK_POC=ON -DUNREAL_HOST_TLS=OFF -DCMAKE_CXX_FLAGS="-march=$MARCH"
    cmake --build "$BUILDDIR" --target $TARGETS -j "$JOBS"
  '
echo "built: $BUILDDIR/bin"
