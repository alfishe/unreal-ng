#!/usr/bin/env bash
# POC 035: fetch and build the third-party GUI libraries at the commits the measurements used.
#
#   scripts/fetch-deps.sh <deps-dir>          e.g. scratch/2026-10-24-poc-035/deps
#
# Result: <deps-dir>/prefix (SDL3 shared, FreeType and RmlUi static), <deps-dir>/imgui and <deps-dir>/rmlui (sources;
# the POC compiles ImGui and the RmlUi SDL3/GL3 backend files itself). Configure the POC with
# -DPOC035_DEPS_PREFIX=<deps-dir>/prefix. Idempotent: a library already installed is not rebuilt.
# These are third-party builds outside the emulator's CMake tree, so they use cmake directly, not tools/build/build.sh.
set -euo pipefail

D=${1:?usage: fetch-deps.sh <deps-dir>}
mkdir -p "$D"
D=$(cd "$D" && pwd)
PREFIX=$D/prefix
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
JOBS=$(( CORES / 2 > 0 ? CORES / 2 : 1 ))

# name url commit
DEPS=(
  "SDL       https://github.com/libsdl-org/SDL.git        a8589a84226a6202831a3d49ff4edda4acab9acd"  # release-3.2.24
  "imgui     https://github.com/ocornut/imgui.git         d88f238d1d363ef387382b2e29ad12cf96f826ac"  # docking, 1.92 WIP
  "freetype  https://github.com/freetype/freetype.git     VER-2-13-3"
  "rmlui     https://github.com/mikke89/RmlUi.git         821cbd229d804cb1c905b729af86c21133abceee"  # master
)

checkout() {
  local name=$1 url=$2 rev=$3
  if [ ! -d "$D/$name/.git" ]; then
    git clone --quiet --filter=blob:none --no-checkout "$url" "$D/$name"
  fi
  if ! git -C "$D/$name" rev-parse -q --verify "$rev^{commit}" >/dev/null; then
    git -C "$D/$name" fetch --quiet --filter=blob:none origin "$rev" || git -C "$D/$name" fetch --quiet --tags origin
  fi
  git -C "$D/$name" -c advice.detachedHead=false checkout --quiet --force "$rev"
  echo "== $name $(git -C "$D/$name" rev-parse --short=12 HEAD)"
}

for line in "${DEPS[@]}"; do
  # shellcheck disable=SC2086
  checkout $line
done

COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX"
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
if [ "$(uname)" = Darwin ]; then
  COMMON+=(-DCMAKE_OSX_ARCHITECTURES="$(uname -m)" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0)
fi

build() {
  local name=$1 stamp=$2; shift 2
  if [ -e "$PREFIX/$stamp" ]; then
    echo "== $name: installed"
    return
  fi
  cmake -S "$D/$name" -B "$D/build-$name" "${COMMON[@]}" "$@" > "$D/build-$name.log" 2>&1 \
    || { tail -40 "$D/build-$name.log"; exit 1; }
  cmake --build "$D/build-$name" -j "$JOBS" >> "$D/build-$name.log" 2>&1 || { tail -40 "$D/build-$name.log"; exit 1; }
  cmake --install "$D/build-$name" >> "$D/build-$name.log" 2>&1
  echo "== $name: built"
}

build SDL lib/cmake/SDL3/SDL3Config.cmake -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF
build freetype lib/cmake/freetype/freetype-config.cmake -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
build rmlui lib/cmake/RmlUi/RmlUiConfig.cmake -DBUILD_SHARED_LIBS=OFF -DRMLUI_SAMPLES=OFF -DRMLUI_PRECOMPILED_HEADERS=OFF \
  -DRMLUI_FONT_ENGINE=freetype
echo "POC035_DEPS_PREFIX=$PREFIX"
