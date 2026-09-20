#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CUBE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> Building 3D Video Cube iOS App for Simulator..."
xcodebuild -project "${CUBE_DIR}/UnrealNGCube.xcodeproj" \
  -target UnrealNGCube \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'id=CBD38440-A45C-4DE5-ADCA-A1E12F4C37F3' \
  ARCHS=arm64 \
  build

echo "==> Done building UnrealNGCube.app!"
