# Stream Deck integration: packaging

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Design, for review |
| **Decision** | D4 in [README.md](README.md#decisions-owner-2026-10-19): in the unreal-ng release |

## Contents

- [1. Today](#1-today)
- [2. What ships](#2-what-ships)
- [3. macOS](#3-macos)
- [4. Windows](#4-windows)
- [5. Linux](#5-linux)
- [6. Data](#6-data)
- [7. Sizes](#7-sizes)

## 1. Today

| Platform | Target | Contents |
|:--|:--|:--|
| macOS | `package_suite_macos` (`CMakeLists.txt:397-501`) | DMG: UnrealNG.app, Screen Viewer.app, Video Wall.app, `unreal-mcp-bridge` at the DMG root (signed alone), `examples/`; `macdeployqt` per app, ad-hoc `codesign --deep --options runtime` |
| Windows | `package_suite_windows` (`:503-625`) | ZIP: the exes, `unreal-mcp-bridge.exe`, windeployqt, MSVC runtime, data |
| Linux | `package_suite_linux` (`:719`), `tools/package-appimage.sh`, `cmake/LinuxPackaging.cmake` | AppImage (`AppDir/usr/bin`, `unreal-mcp-bridge` if present), DEB (`lib/unreal-ng`, symlinks in `bin`, data dirs) |
| — | No helper executable inside a `.app` today | |

The prototype build of the studio has no `Contents/Frameworks` (Qt by `@rpath`), so its deployable size is unknown.

## 2. What ships

| Component | Every package | Notes |
|:--|:--|:--|
| Daemon | yes | No Qt; small |
| Studio | yes | Qt app; shares the suite's Qt where the platform allows |
| `sdctl` | no | Developer tool; built in dev builds only |
| Default scenario, themes, thumbnails, catalogue | yes | `data/streamdeck/` |
| udev rule for `0FD9` | Linux | |

## 3. macOS

- The daemon goes **inside `UnrealNG.app/Contents/MacOS/`** (a helper; unreal-qt starts it from its own bundle, no
  PATH lookup). Signed with the bundle. Unverified: whether the hardened runtime or the Input Monitoring permission
  affects opening a Stream Deck from a signed helper; checked with the first signed build (the prototype build was
  never signed).
- The studio as a nested app `UnrealNG.app/Contents/Applications/Stream Deck Studio.app` sharing
  `UnrealNG.app/Contents/Frameworks` through `@rpath` (`@executable_path/../../../../Frameworks`), so Qt is not
  copied twice; it is also linked from the DMG root for users who want it in `/Applications`.
  (Decided 2026-10-19, Q-2; the rejected alternative was a separate `.app` with its own Qt, ~+60-100 MB.)
- `macdeployqt` runs on UnrealNG.app after the nested app is placed, with `-executable=` for the studio binary.

## 4. Windows

- `unreal-streamdeck.exe` and `unreal-streamdeck-studio.exe` beside `unreal-qt.exe`, sharing the Qt DLLs
  (windeployqt once with all three executables).
- hidapi's Windows backend uses SetupAPI and HID.dll (system).

## 5. Linux

- AppImage: both binaries in `AppDir/usr/bin`, the studio deployed by linuxdeploy's Qt plugin with the rest.
- DEB: both in `lib/unreal-ng`, symlinks in `bin`; `/usr/lib/udev/rules.d/70-unreal-streamdeck.rules`
  (`SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0fd9", TAG+="uaccess"`, plus the same for `usb`). Done 2026-10-20 in
  `cmake/LinuxPackaging.cmake` when the build has `-DUNREAL_STREAMDECK=ON`; the rule's source is
  `companions/streamdeck/linux/70-unreal-streamdeck.rules`. The release workflow does not turn the companion on yet.
- AppImage users get the rule text in the settings page (an AppImage cannot install it).

## 6. Data

| Path in the repo | Installed | Used by |
|:--|:--|:--|
| `data/streamdeck/scenarios/*.deck.json` | `$data/streamdeck/scenarios` | daemon (default scenario), studio (templates) |
| `data/streamdeck/themes/` | `$data/streamdeck/themes` | daemon (idle and boot screens) |
| `data/streamdeck/thumbnails/` | `$data/streamdeck/thumbnails` | default scenario faces |
| `catalogue.json` (generated at build time) | `$data/streamdeck/catalogue.json` | studio palette offline |
| unreal-qt icons | `$data/icons` | faces (`$icons`) |

`cmake/RuntimeData.cmake` gets `streamdeck` in its list so dev builds find it beside the binaries; the macOS suite
copy gets it too (today it misses `data/controllers`, harmless there because the controllers data is compiled in).

## 7. Sizes

| Part | Today (macOS arm64, prototype build) |
|:--|:--|
| `sdctl` (≈ the daemon) | 2.7 MB |
| studio binary | 3.4 MB (+ Qt, shared) |
| `sdeck-mcp-bridge` | 59 KB (not shipped: `unreal-mcp-bridge`) |
| `unrealng-control.deck.json` | 263 KB |
| thumbnails | unmeasured |
