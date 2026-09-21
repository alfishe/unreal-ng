#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CUBE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${CUBE_DIR}/../../../.." && pwd)"
REPO_XCFW="${REPO_ROOT}/unreal-ios/Frameworks/UnrealNGCore.xcframework"
LOCAL_XCFW="${CUBE_DIR}/Frameworks/UnrealNGCore.xcframework"

echo "==> Syncing UnrealNGCore.xcframework from the repo build..."
# The Xcode project links its LOCAL copy (SRCROOT/Frameworks), NOT the one
# produced by unreal-ios/scripts/build-ios-xcframework.sh. Without this
# sync a stale local framework silently keeps old core code in the app
# even after a full xcframework + app rebuild (bit us twice: stale
# GetFrameInfo, stale IsModelSupported).
if [ -d "${REPO_XCFW}" ]; then
  rm -rf "${LOCAL_XCFW}"
  ditto "${REPO_XCFW}" "${LOCAL_XCFW}"
else
  echo "WARNING: ${REPO_XCFW} not found - keeping existing local framework" >&2
fi

echo "==> Building 3D Video Cube iOS App for Simulator..."
# Keep ALL build state inside the project: writing to the default
# ~/Library/Developer/Xcode/DerivedData fails under sandboxed shells
# ("Operation not permitted" on manifest.json / info.plist)
xcodebuild -project "${CUBE_DIR}/UnrealNGCube.xcodeproj" \
  -scheme UnrealNGCube \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'id=CBD38440-A45C-4DE5-ADCA-A1E12F4C37F3' \
  -derivedDataPath "${CUBE_DIR}/build/DerivedData" \
  ARCHS=arm64 \
  build

echo "==> Done building UnrealNGCube.app!"
