#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DEVICE="${ROOT_DIR}/build-ios-device"
BUILD_SIM="${ROOT_DIR}/build-ios-sim"
OUT_DIR="${ROOT_DIR}/unreal-ios/Frameworks"

mkdir -p "${OUT_DIR}"

echo "==> Cleaning old build trees..."
rm -rf "${BUILD_DEVICE}" "${BUILD_SIM}"

echo "==> Building for iOS Device (arm64)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DEVICE}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DBUILD_QT_APPS=OFF -DTESTS=OFF -DBENCHMARKS=OFF -DBUILD_TESTCLIENT=OFF -DBUILD_POC=OFF \
  -DENABLE_PYTHON_AUTOMATION=OFF -DENABLE_RECORDING=ON \
  -DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF

ninja -C "${BUILD_DEVICE}" unrealng_embed

echo "==> Building for iOS Simulator (arm64)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_SIM}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DBUILD_QT_APPS=OFF -DTESTS=OFF -DBENCHMARKS=OFF -DBUILD_TESTCLIENT=OFF -DBUILD_POC=OFF \
  -DENABLE_PYTHON_AUTOMATION=OFF -DENABLE_RECORDING=ON \
  -DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF

ninja -C "${BUILD_SIM}" unrealng_embed

echo "==> Bundling static libraries into XCFramework..."
rm -rf "${OUT_DIR}/UnrealNGCore.xcframework"
mkdir -p "${OUT_DIR}/device" "${OUT_DIR}/sim"

DEVICE_LIBS=$(find "${BUILD_DEVICE}" -name "*.a")
SIM_LIBS=$(find "${BUILD_SIM}" -name "*.a")

libtool -static -o "${OUT_DIR}/device/libUnrealNGCore.a" ${DEVICE_LIBS}
libtool -static -o "${OUT_DIR}/sim/libUnrealNGCore.a" ${SIM_LIBS}

xcodebuild -create-xcframework \
  -library "${OUT_DIR}/device/libUnrealNGCore.a" -headers "${ROOT_DIR}/core/embed/include" \
  -library "${OUT_DIR}/sim/libUnrealNGCore.a" -headers "${ROOT_DIR}/core/embed/include" \
  -output "${OUT_DIR}/UnrealNGCore.xcframework"

echo "==> Done! XCFramework generated at ${OUT_DIR}/UnrealNGCore.xcframework"
