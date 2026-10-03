Fully re-engineered Unreal Speccy emulator

Cross-platform emulation core:
- Windows 32-bit
- Windows 64-bit
- Linux 32/64-bit
- macOS 64-bit

Modular architecture:
- core - all emulation logic


Pre-requisites:
- CMake v3.16 or newer
- QT 6.x with 5.x compatibility installed if you want to use GUI

Submodules:
- Google Test
- Google Benchmark

# How to start:

    git clone --recurse-submodules https://github.com/alfishe/unreal-ng

or

    git clone https://github.com/alfishe/unreal-ng
    git submodule init
    git submodule update


Updates:

    git pull --recurse-submodules
    
# Build

## Linux / macOS

    mkdir build
    cd build
    cmake ..
    cmake --build . --parallel 12

/src /tests /benchmarks can be built separately same way

## Windows

CMake/MSBuild chain under windows behaves weirdly: it generates MSVC projects and always uses Debug configuration totally ignoring CMAKE_BUILD_TYPE value. So the only way to have control over build type - to use cmake --config parameter.
 
    --config Debug
    --config Release

The rest is similar to *nix:

    mkdir build
    cd build
    cmake ..
    cmake --build . --config Release

/src /tests /benchmarks can be built separately same way

## Downloadable builds

In GitHub Actions, run **Release Build** on `master` to publish a rolling `continuous`
prerelease (no version bump or tag needed); on any other branch the same run builds and
verifies the packages without publishing. Pull requests that touch the build build all
packages; pushing a `v*` tag creates a milestone release.

| Platform | File |
|----------|------|
| Linux x86_64, portable (glibc 2.39+) | `UnrealNG-Suite-Linux-x86_64.AppImage` |
| Linux x86_64, Ubuntu 24.04+ / Debian 13+ | `UnrealNG-Suite-Linux-x86_64.deb` |
| macOS 12+, Apple Silicon / Intel | `UnrealNG-Suite-macOS-arm64.dmg` / `UnrealNG-Suite-macOS-x86_64.dmg` |
| Windows x86_64 / ARM64 | `UnrealNG-Suite-Windows-x86_64.zip` / `UnrealNG-Suite-Windows-arm64.zip` |
| Windows x86_64, MinGW build | `UnrealNG-Suite-Windows-x86_64-MinGW.zip` |

Every package carries its own private Qt (from the cached Qt SDK); all other dependencies
are vendored and linked statically, so nothing has to be installed on the user's system.
Release builds disable TLS (`-DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF`): the WebAPI
serves plain HTTP on localhost, so neither OpenSSL nor c-ares is needed.

The DEB package version is the build time, `YYYYMMDD.HHMMSS` UTC; file names stay stable.
macOS builds use ad-hoc signing without notarization; Windows builds are unsigned.
Install the DEB with `sudo apt install ./UnrealNG-Suite-Linux-x86_64.deb`; for the
AppImage, mark it executable and run it (without FUSE: `--appimage-extract-and-run`).
Extract the entire Windows ZIP before launching `unreal-qt.exe`.

### Local Linux packaging

The GUI requires **Qt 6.7 or newer**. A requested GUI build fails if Qt is not found;
use `-DBUILD_QT_APPS=OFF` for a deliberate headless build. Point CMake at a Qt SDK with
`-DCMAKE_PREFIX_PATH=/path/to/Qt`, `-DQt6_DIR=...` or `-DQT_INSTALL_PATH=...`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.9.3/gcc_64 -DUNREAL_BUNDLE_QT=ON \
  -DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF
cmake --build build --parallel
cpack --config build/CPackConfig.cmake -G DEB      # DEB with private Qt

# AppImage: linuxdeploy-x86_64.AppImage and linuxdeploy-plugin-qt-x86_64.AppImage on PATH
cmake --build build --parallel 1 --target package_suite_linux
UNREAL_PACKAGE_VERSION=$(date -u +%Y%m%d.%H%M%S) \
  QMAKE=$HOME/Qt/6.9.3/gcc_64/bin/qmake bash tools/package-appimage.sh build
```

Outputs go to `build/packages`. Build on the oldest distribution you want to support:
bundling Qt does not remove the host glibc requirement. The AppImage launches the
emulator; its `usr/bin` also contains the display utilities and the MCP bridge. The DEB
installs everything under `/usr/lib/unreal-ng` with launchers on `PATH`.

## License

unreal-ng is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. See [LICENSE](LICENSE).

Copyright (C) 2020-2026 Ilia Sharin. Portions derived from UnrealSpeccy, Copyright (C) SMT, Alone Coder, deathsoft.

Third-party components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
ROM images (`data/rom/`) and test fixtures (`testdata/`) are not covered by the GPL; see
[data/rom/README-ROMS.md](data/rom/README-ROMS.md) and [testdata/NOTICE.md](testdata/NOTICE.md).
