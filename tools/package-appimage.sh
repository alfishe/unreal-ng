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
# the suite's data with it: the Stream Deck daemon finds usr/bin/streamdeck beside itself
cp -a "$suite/." "$appdir/usr/bin/"
# The Stream Deck companion's udev rule (-DUNREAL_STREAMDECK=ON): an AppImage cannot install it; it rides along in
# the image's usr/lib/udev/rules.d, the place the DEB uses, for the user to copy (unreal-qt's settings page is to
# show its text)
if [[ -d "$appdir/usr/bin/lib/udev" ]]; then
    mkdir -p "$appdir/usr/lib"
    mv "$appdir/usr/bin/lib/udev" "$appdir/usr/lib/udev"
    rmdir "$appdir/usr/bin/lib"
fi
cp "$repo_dir/LICENSE" "$repo_dir/THIRD_PARTY_NOTICES.md" "$appdir/usr/share/doc/unreal-ng/"

executables=()
# the Stream Deck daemon and studio when the build has them; the studio is deployed with the same Qt
for app in unreal-qt unreal-screen-viewer unreal-videowall unreal-mcp-bridge \
        unreal-streamdeck unreal-streamdeck-studio; do
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
