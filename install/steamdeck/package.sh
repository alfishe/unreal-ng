#!/usr/bin/env bash
# Build the Steam Deck bundle from the Linux suite AppImage (tools/package-appimage.sh): the AppImage (its own Qt, the
# Stream Deck daemon, the udev rule), the launcher, the installer, the Steam library artwork. The release workflow
# runs it after the AppImage; on the Deck: tar xzf UnrealNG-SteamDeck-x86_64.tar.gz && UnrealNG-SteamDeck/install.sh
#
#   install/steamdeck/package.sh build                      # build/packages/UnrealNG-Suite-Linux-x86_64.AppImage
#   install/steamdeck/package.sh build path/to/Some.AppImage
#   install/steamdeck/package.sh build --deploy deck@host   # also copy it to a Steam Deck over SSH and install
#
# Output: <build>/packages/UnrealNG-SteamDeck-x86_64.tar.gz
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir=$(realpath "${1:?usage: package.sh <build dir> [AppImage] [--deploy user@host]}")
shift
appimage="$build_dir/packages/UnrealNG-Suite-Linux-x86_64.AppImage"
deploy=""
while [ $# -gt 0 ]; do
  case "$1" in
    --deploy) deploy="$2"; shift 2 ;;
    *) appimage="$1"; shift ;;
  esac
done
[ -f "$appimage" ] || { echo "no AppImage at $appimage (tools/package-appimage.sh builds it)" >&2; exit 1; }

name="UnrealNG-SteamDeck"
stage="$build_dir/packages/$name"
out="$build_dir/packages/$name-x86_64.tar.gz"
rm -rf "$stage"
mkdir -p "$stage"
cp "$appimage" "$stage/UnrealNG.AppImage"
chmod +x "$stage/UnrealNG.AppImage"
install -m 0755 "$HERE/install.sh" "$HERE/unreal-ng.sh" "$HERE/steam-library.py" "$stage/"
cp -R "$HERE/art" "$stage/art"
cp "$HERE/README.md" "$stage/README.md"
echo "${UNREAL_PACKAGE_VERSION:-$(basename "$appimage" .AppImage)}" > "$stage/VERSION"
# no extended attributes: a bundle made on macOS would carry com.apple.* headers GNU tar warns about
COPYFILE_DISABLE=1 tar --no-xattrs -C "$build_dir/packages" -czf "$out" "$name"
rm -rf "$stage"
echo "$out ($(du -h "$out" | cut -f1))"

if [ -n "$deploy" ]; then
  scp -q "$out" "$deploy:/tmp/$name.tar.gz"
  ssh "$deploy" "set -e; d=\$(mktemp -d); tar -C \"\$d\" -xzf /tmp/$name.tar.gz; \"\$d/$name/install.sh\"; rm -rf \"\$d\" /tmp/$name.tar.gz"
fi
