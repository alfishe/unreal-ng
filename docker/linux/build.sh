#!/usr/bin/env bash
# Build (and optionally test) the project inside the CI Linux image - the same
# toolchain (gcc, libstdc++, Qt) GitHub Actions uses, so Linux-only compiler
# errors reproduce locally.
#
# The image is multi-arch. By default the container runs NATIVELY on the host
# architecture (arm64 on Apple Silicon, amd64 on x86) - fast, no emulation.
# Use --platform only when the architecture itself is under suspicion.
#
# Usage: docker/linux/build.sh [options] [target]
#   target            ninja target, default: core-tests
#   --platform ARCH   amd64 | arm64 (default: host native)
#   --test            run core-tests after a successful build
#   --filter EXPR     gtest filter for --test (default: all)
#   --jobs N          parallel jobs (default: half the logical cores)
#   --cpus N          hard CPU cap for the whole container: configure, link and the
#                     test run included, not only the compile jobs (default: same as
#                     --jobs, i.e. half the host cores)
#   --image REF       image (default: ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04)
#   --type TYPE       CMAKE_BUILD_TYPE (default: Release, like CI)
#   --clean           remove the build directory first
#   --asan / --tsan   shortcuts for --sanitize address,undefined / --sanitize thread
#   --sanitize LIST   build with -DSANITIZE=LIST (cmake/Sanitize.cmake: address,undefined | thread | ...);
#                     own build directory, ASAN_/LSAN_/UBSAN_/TSAN_OPTIONS passed in,
#                     SYS_PTRACE granted (LeakSanitizer needs it). docs/testing/sanitizers.md
#
# Build output goes to scratch/linux-<arch>-<type>[-san-<list>]/ (git-ignored); remove it
# when done: rm -rf scratch/linux-*
#
# Note: lib/googletest and lib/benchmark must be real submodule checkouts
# (git submodule update --init --recursive), not symlinks, or the container
# cannot see them.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04"
TARGET="core-tests"
TYPE="Release"
PLATFORM=""
RUNTESTS=0
FILTER="*"
CLEAN=0
CORES="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
JOBS=$(( CORES / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS ))
CPUS=""
SANITIZE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --platform) PLATFORM="$2"; shift 2 ;;
    --test) RUNTESTS=1; shift ;;
    --filter) FILTER="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --cpus) CPUS="$2"; shift 2 ;;
    --image) IMAGE="$2"; shift 2 ;;
    --type) TYPE="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --sanitize) SANITIZE="$2"; shift 2 ;;
    --asan) SANITIZE="address,undefined"; shift ;;
    --tsan) SANITIZE="thread"; shift ;;
    -h|--help) sed -n '2,31p' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) TARGET="$1"; shift ;;
  esac
done

if [ -z "$PLATFORM" ]; then
  case "$(uname -m)" in
    arm64|aarch64) PLATFORM="arm64" ;;
    *) PLATFORM="amd64" ;;
  esac
fi

for sub in lib/googletest lib/benchmark; do
  if [ -L "$ROOT/$sub" ] || [ ! -e "$ROOT/$sub/CMakeLists.txt" ]; then
    echo "error: $sub is not a real submodule checkout" >&2
    exit 1
  fi
done

[ -n "$CPUS" ] || CPUS="$JOBS"

BUILDDIR="scratch/linux-$PLATFORM-$TYPE"
SANITIZER_ARGS=()
if [ -n "$SANITIZE" ]; then
  BUILDDIR="$BUILDDIR-san-${SANITIZE//,/-}"
  SANITIZER_ARGS=(--cap-add SYS_PTRACE -e SANITIZE="$SANITIZE")
  case "$SANITIZE" in
    # TSan's fixed shadow layout dies on a high mmap_rnd_bits kernel ("FATAL: ThreadSanitizer:
    # unexpected memory mapping", Ubuntu 24.04 with kernel 6.6+): no ASLR for the build (gtest
    # discovery runs the binary) and the tests; seccomp=unconfined lets personality() through
    *thread*) SANITIZER_ARGS+=(--security-opt seccomp=unconfined -e NOASLR=1) ;;
  esac
  for v in ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS; do
    [ -n "${!v:-}" ] && SANITIZER_ARGS+=(-e "$v=${!v}")
  done
fi
[ "$CLEAN" = 1 ] && rm -rf "$ROOT/$BUILDDIR"

# Git worktrees: their .git file points at the main repository's .git by absolute path
# (gitdir: <common>/worktrees/<name>). Mount that directory at the same path, read-only, so
# git - CMake's build fingerprint - works in the container from any worktree. The main
# checkout's .git already is inside $ROOT. GIT_OPTIONAL_LOCKS=0: git never writes to it.
MOUNTS=()
GIT_COMMON="$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
case "$GIT_COMMON" in
  ""|"$ROOT"/*) ;;
  *) MOUNTS+=(-v "$GIT_COMMON:$GIT_COMMON:ro") ;;
esac

# ${MOUNTS[@]+...}: an empty array under set -u fails in macOS's bash 3.2
docker run --rm --platform "linux/$PLATFORM" --cpus "$CPUS" \
  -v "$ROOT":/src -w /src/unreal-qt ${MOUNTS[@]+"${MOUNTS[@]}"} -e GIT_OPTIONAL_LOCKS=0 \
  ${SANITIZER_ARGS[@]+"${SANITIZER_ARGS[@]}"} \
  --tmpfs /scratch-tmp:exec,size=4g -e UNREAL_TEST_SCRATCH_DIR=/scratch-tmp \
  -e TYPE="$TYPE" -e TARGET="$TARGET" -e JOBS="$JOBS" \
  -e BUILDDIR="/src/$BUILDDIR" -e RUNTESTS="$RUNTESTS" -e FILTER="$FILTER" \
  "$IMAGE" bash -c '
    set -e
    git config --global --add safe.directory "*"
    SAN=()
    if [ -n "${SANITIZE:-}" ]; then
      # cmake/Sanitize.cmake: the flags, gcc'"'"'s libstdc++ maybe-uninitialized waiver included
      SAN=("-DSANITIZE=$SANITIZE")
    fi
    cmake -S . -B "$BUILDDIR" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" -DTESTS=ON ${SAN[@]+"${SAN[@]}"}
    RUN=()
    [ "${NOASLR:-}" = 1 ] && RUN=(setarch "$(uname -m)" -R)
    ${RUN[@]+"${RUN[@]}"} cmake --build "$BUILDDIR" --target "$TARGET" -j "$JOBS"
    if [ "$RUNTESTS" = 1 ]; then
      # From the binary directory: the tests open testdata/ and rom/ relative to it (the
      # copies CMake puts next to core-tests), and the scratch helper expects the same layout
      cd "$BUILDDIR/bin"
      ${RUN[@]+"${RUN[@]}"} ./core-tests --gtest_filter="$FILTER" --gtest_color=no
    fi
  '
