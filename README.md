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

In GitHub Actions, run **Release Build** to publish a rolling `continuous`
prerelease. No version bump or tag is required. Pull requests build the same
packages without publishing them; pushing a `v*` tag still creates an optional
milestone release.

| Platform | Download |
|----------|----------|
| Ubuntu 24.04, x86-64 | `.deb` (bundled Qt 6.9.3) |
| Ubuntu 25.10, x86-64 | `.deb` (system Qt 6.9.2 or newer) |
| Fedora 43, x86-64 | `.rpm` (system Qt dependencies) |
| Linux x86-64, glibc 2.39 or newer | `.AppImage` (bundled Qt) |
| macOS Intel / Apple Silicon | Separate `.dmg` files |
| Windows x86-64 / ARM64 | Separate portable `.zip` files containing executables, Qt and compiler runtime DLLs |

DEB/RPM metadata requires a version. CI generates `YYYYMMDD.HHMMSS` in UTC
once per run; download filenames stay stable. Existing application version
constants are unchanged. The release identifies the source commit and includes
SHA-256 checksums. Linux packages are installed and checked in fresh containers
before publishing. macOS builds use the existing ad-hoc signing, without Apple
notarization; Windows builds are unsigned.

The two DEBs are alternatives: `UnrealNG-Suite-Ubuntu-24.04-Qt-bundled-x86_64.deb`
includes Qt privately; `UnrealNG-Suite-Ubuntu-25.10-x86_64.deb` uses the system Qt.
Install the appropriate one with `sudo apt install ./<filename>.deb`, or install
the RPM with `sudo dnf install ./UnrealNG-Suite-Linux-x86_64.rpm`. For AppImage, mark the
file executable and run it. If FUSE is unavailable, use
`./UnrealNG-Suite-Linux-x86_64.AppImage --appimage-extract-and-run`.
Extract the entire Windows ZIP before launching `unreal-qt.exe`.

### Local Linux packaging

The GUI source requires **Qt 6.7 or newer**. Install development packages for
Core, Widgets, OpenGL, Multimedia, SVG and Core5Compat, plus OpenSSL, zlib, UUID,
Brotli, CMake, Ninja, and the platform packaging
tools (`dpkg-dev` or `rpm-build`). System Qt is discovered automatically.
For an SDK installation, pass `-DCMAKE_PREFIX_PATH=/path/to/Qt`. Both
`-DQt6_DIR=/path/to/Qt6` and the older `-DQT_INSTALL_PATH=...` accept a directory
containing `Qt6Config.cmake`. A requested GUI build fails if Qt is unavailable;
use `-DBUILD_QT_APPS=OFF` for a deliberate headless build.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
jobs=$(( $(nproc) / 2 )); jobs=$(( jobs > 0 ? jobs : 1 ))
cmake --build build --parallel "$jobs"
cpack --config build/CPackConfig.cmake -G DEB  # use RPM on Fedora
```

For a DEB that bundles Qt, configure with `-DUNREAL_BUNDLE_QT=ON` and an SDK
prefix. CI uses the official Qt 6.9.3 SDK on Ubuntu 24.04, whose system Qt is too
old. The bundled libraries and plugins stay private under `/usr/lib/unreal-ng`;
no additional package repository is needed. The smaller system-Qt DEB is built
on Ubuntu 25.10 and installation-tested on both Ubuntu 25.10 and 26.04.
RPM builds use Fedora’s system Qt. CI includes the
Ubuntu baseline in each DEB filename; local builds can set
`-DUNREAL_PACKAGE_BASENAME=...` to choose a filename without the extension.

For AppImage, install `linuxdeploy-x86_64.AppImage` and
`linuxdeploy-plugin-qt-x86_64.AppImage` on `PATH`, then run:

```bash
cmake --build build --parallel "$jobs" --target package_suite_linux
UNREAL_PACKAGE_VERSION=$(date -u +%Y%m%d.%H%M%S) \
  QMAKE=/path/to/Qt/bin/qmake bash tools/package-appimage.sh build
```

Outputs go to `build/packages`. Build AppImages on the oldest supported system;
bundling Qt does not remove the host glibc requirement. The AppImage launches
the emulator; its extracted `usr/bin` also contains the display utilities and
MCP bridge. Native packages install these utilities on `PATH`.

## License

unreal-ng is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. See [LICENSE](LICENSE).

Copyright (C) 2020-2026 Ilia Sharin. Portions derived from UnrealSpeccy, Copyright (C) SMT, Alone Coder, deathsoft.

Third-party components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
ROM images (`data/rom/`) and test fixtures (`testdata/`) are not covered by the GPL; see
[data/rom/README-ROMS.md](data/rom/README-ROMS.md) and [testdata/NOTICE.md](testdata/NOTICE.md).
