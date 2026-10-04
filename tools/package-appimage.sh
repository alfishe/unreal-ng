#!/usr/bin/env bash
# Build from the suite staging directory produced by package_suite_linux.
# linuxdeploy and linuxdeploy-plugin-qt must be executable and on PATH.
set -euo pipefail

build_dir=$(realpath "${1:-build}")
suite="$build_dir/packages/UnrealNG-Suite"
appdir="$build_dir/packages/UnrealNG.AppDir"
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(dirname "$script_dir")

for app in unreal-qt unreal-screen-viewer unreal-videowall; do
    test -x "$suite/$app"
done
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/doc/unreal-ng"
cp -a "$suite/." "$appdir/usr/bin/"
cp "$repo_dir/LICENSE" "$repo_dir/THIRD_PARTY_NOTICES.md" "$appdir/usr/share/doc/unreal-ng/"

executables=()
for app in unreal-qt unreal-screen-viewer unreal-videowall unreal-mcp-bridge; do
    if [[ -x "$appdir/usr/bin/$app" ]]; then
        executables+=(--executable "$appdir/usr/bin/$app")
    fi
done
# SVG icons are loaded dynamically. Offscreen is used by the smoke test.
export EXTRA_QT_MODULES=svg
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so
export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT="$build_dir/packages/UnrealNG-Suite-Linux-x86_64.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="${UNREAL_PACKAGE_VERSION:?Set UNREAL_PACKAGE_VERSION to YYYYMMDD.HHMMSS}"

linuxdeploy-x86_64.AppImage --appdir "$appdir" \
    "${executables[@]}" \
    --desktop-file "$repo_dir/unreal-qt/install/linux/unrealng.desktop" \
    --icon-file "$repo_dir/unreal-qt/install/linux/icons/hicolor/256x256/apps/unrealng.png" \
    --plugin qt --output appimage
