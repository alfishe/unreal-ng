# Stream Deck integration: packaging

| | |
|---|---|
| **Date** | 2026-10-19 |
| **Status** | Staging done 2026-10-20 on all three platforms (sections 3-5); `data/streamdeck` in every package and the companion's symbols in the symbol archives 2026-10-20 (sections 6, 8); the scenarios, thumbnails and catalogue wait on SD5 |
| **Decision** | D4 in [README.md](README.md#decisions-owner-2026-10-19): in the unreal-ng release |

## Contents

- [1. Today](#1-today)
- [2. What ships](#2-what-ships)
- [3. macOS](#3-macos)
- [4. Windows](#4-windows)
- [5. Linux](#5-linux)
- [6. Data](#6-data)
- [7. Sizes](#7-sizes)
- [8. Debug symbols](#8-debug-symbols)

## 1. Today

| Platform | Target | Contents |
|:--|:--|:--|
| macOS | `package_suite_macos` (`CMakeLists.txt:397-501`) | DMG: UnrealNG.app, Screen Viewer.app, Video Wall.app, `unreal-mcp-bridge` at the DMG root (signed alone), `examples/`; `macdeployqt` per app, ad-hoc `codesign --deep --options runtime` |
| Windows | `package_suite_windows` (`:503-625`) | ZIP: the exes, `unreal-mcp-bridge.exe`, windeployqt, MSVC runtime, data |
| Linux | `package_suite_linux` (`:719`), `tools/package-appimage.sh`, `cmake/LinuxPackaging.cmake` | AppImage (`AppDir/usr/bin`, `unreal-mcp-bridge` if present), DEB (`lib/unreal-ng`, symlinks in `bin`, data dirs) |
| — | No helper executable inside a `.app` before the companion | |

The release workflow builds every package with `-DUNREAL_STREAMDECK=ON` since 2026-10-20; the targets above stage the
companion when its targets exist, so a build without it packages as before.

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

Done 2026-10-20 in `package_suite_macos`:

| Step | What |
|:--|:--|
| Daemon | `bin/unreal-streamdeck` -> `UnrealNG.app/Contents/MacOS/unreal-streamdeck`, signed alone, then again by the bundle's `--deep` signature |
| Studio | `bin/unreal-streamdeck-studio.app` -> `UnrealNG.app/Contents/Applications/Stream Deck Studio.app` (`cp -a`), no `Contents/Frameworks` of its own |
| Qt | `macdeployqt UnrealNG.app -executable=<studio binary>`: one Qt; the studio's references become `@loader_path/../../../../Frameworks/...` (Qt 6.9.3). `cmake/MacOSNestedRPath.cmake` is a guard: no absolute (build tree / SDK) rpath left, `@executable_path/../../../../Frameworks` on the rpath |
| Plugins | the studio adds `UnrealNG.app/Contents/PlugIns` itself before `QApplication` (`main.cpp`, from the executable's real path). Not a `qt.conf`: its relative prefix resolves next to the path the app was started through, so the DMG-root link found no platform plugin (checked 2026-10-20) |
| Info.plist | the suite's template (`companions/streamdeck/studio/install/macos/Info.plist`): `LSMinimumSystemVersion`, high DPI |
| Signing | inside-out: the nested app, the daemon, then UnrealNG.app (`--deep`), ad hoc, hardened runtime, the suite's entitlements |
| DMG | `Stream Deck Studio.app` at the root is a relative symlink into UnrealNG.app; copied to `/Applications` beside UnrealNG.app it resolves there too |

The studio finds the daemon in the suite's bundle (`../../../../MacOS` from its real directory,
`DaemonControl::Path`). The release job checks the layout, `codesign --verify --deep --strict`, both `--help` runs
(the studio also through the DMG-root link) and that the studio loads no Qt from outside UnrealNG.app. The hardened
runtime question above, first answer (2026-10-20, ad hoc signature, macOS 15): the packaged helper opened a Stream
Deck + XL and played the idle scenario (`run --seconds 1`) with no permission prompt; a Developer ID signature and
notarisation are still to check (SD9).

## 4. Windows

- `unreal-streamdeck.exe` and `unreal-streamdeck-studio.exe` beside `unreal-qt.exe`, sharing the Qt DLLs
  (windeployqt once with all three executables).
- hidapi's Windows backend uses SetupAPI and HID.dll (system).

Done 2026-10-20 in `package_suite_windows`: both exes copied beside `unreal-qt.exe`; `windeployqt` runs on the studio
as on the other apps (into the same directory, so the DLLs are shared; the daemon has no Qt). MinGW:
`CollectMinGWDependencies.cmake` scans every staged exe, the companion's included. The release job runs
`unreal-streamdeck.exe --help` and `unreal-streamdeck-studio.exe --help` from the ZIP's tree with a bare `PATH`.

## 5. Linux

- AppImage: both binaries in `AppDir/usr/bin`, the studio deployed by linuxdeploy's Qt plugin with the rest.
- DEB: both in `lib/unreal-ng`, symlinks in `bin`; `/usr/lib/udev/rules.d/70-unreal-streamdeck.rules`
  (`SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0fd9", TAG+="uaccess"`, plus the same for `usb`). Done 2026-10-20 in
  `cmake/LinuxPackaging.cmake` when the build has `-DUNREAL_STREAMDECK=ON`; the rule's source is
  `companions/streamdeck/linux/70-unreal-streamdeck.rules`.
- Tarball (`package_suite_linux`, done 2026-10-20): both binaries at the top beside `unreal-qt`, the rule in
  `lib/udev/rules.d/70-unreal-streamdeck.rules` (the DEB's place, for the user to copy to `/etc/udev/rules.d`).
- AppImage (`tools/package-appimage.sh`, done 2026-10-20): both binaries in `usr/bin`, passed to linuxdeploy as
  `--executable` (the studio's Qt deployed with the rest), the rule moved to `usr/lib/udev/rules.d`. `AppRun` starts
  unreal-qt only; the companion is reached through unreal-qt (SD7) or `--appimage-extract`.
- AppImage users get the rule text in the settings page (an AppImage cannot install it): done 2026-10-21 (SD7), Tools >
  Stream Deck... on Linux, also `streamdeck udev` on every plane.
- The release job checks: the DEB in clean Ubuntu 24.04 / 26.04 (the rule installed, both `--help`), the extracted
  AppImage (the rule, both `--help` with a bare environment).

## 6. Data

| Path in the repo | Installed | Used by |
|:--|:--|:--|
| `data/streamdeck/scenarios/*.deck.json` | `$data/streamdeck/scenarios` | daemon (default scenario), studio (templates) |
| `data/streamdeck/themes/` | `$data/streamdeck/themes` | daemon (idle and boot screens) |
| `data/streamdeck/thumbnails/` | `$data/streamdeck/thumbnails` | default scenario faces |
| `catalogue.json` (generated at build time) | `$data/streamdeck/catalogue.json` | studio palette offline |
| unreal-qt icons | `$data/icons` | faces (`$icons`) |

`data/streamdeck` ships in every package since 2026-10-20 (today: the idle screen's theme, `themes/unrealng`; the
rows above that SD5 adds go into the same directory and ship with it). The daemon finds it through
`companions/streamdeck/lib/src/datadir.cpp` (the first candidate that holds a `streamdeck/` directory; `run` prints
`data: <dir>`, or a note when none is found):

| Package | Copied to | The daemon's candidate that finds it | The studio's |
|:--|:--|:--|:--|
| macOS DMG | `UnrealNG.app/Contents/Resources/streamdeck` | `Contents/Resources` (helper in `Contents/MacOS`) | the outer bundle's `Contents/Resources` (nested app; from the real path) |
| Windows ZIP | `streamdeck/` beside the exes | beside the exe | beside the exe |
| DEB | `/usr/lib/unreal-ng/streamdeck` | beside the binary (`/usr/bin` holds symlinks: resolved) | beside the binary |
| AppImage | `usr/bin/streamdeck` | beside the binary | beside the binary |
| Linux tarball | `streamdeck/` at the top | beside the binary | beside the binary |
| Build tree | `bin/streamdeck` (`cmake/RuntimeData.cmake`) | beside the binary | `../../..` (the studio's bundle on macOS) |

`UNREAL_STREAMDECK_DATA` names another directory (the candidate before all others); a source tree's `data/` is the
last. Targets: `package_suite_macos` / `package_suite_windows` / `package_suite_linux` (`CMakeLists.txt`, with the
daemon's copy), `cmake/LinuxPackaging.cmake` (DEB), `tools/package-appimage.sh` (takes the tarball's staging tree as
it is). The release job checks `streamdeck/themes/unrealng/theme.json` in the bundle, the ZIP's tree (MSVC and
MinGW), the extracted AppImage and the installed DEB. Checked 2026-10-20 on macOS (`package_suite_macos` on the M3
Ultra): the file in the bundle, and the daemon run from it prints `data: .../UnrealNG.app/Contents/Resources`.

## 7. Sizes

| Part | Today (macOS arm64, prototype build) |
|:--|:--|
| `sdctl` (≈ the daemon) | 2.7 MB |
| studio binary | 3.4 MB (+ Qt, shared) |
| `sdeck-mcp-bridge` | 59 KB (not shipped: `unreal-mcp-bridge`) |
| `unrealng-control.deck.json` | 263 KB |
| thumbnails | unmeasured |

## 8. Debug symbols

The symbol archives (`-DPACKAGE_SYMBOLS=ON`, release `workflow_dispatch` runs) carry the companion beside the apps
since 2026-10-20: `unreal-streamdeck` and `unreal-streamdeck-studio` join the MCP bridge in the list of executables
the symbol targets walk (`SYMBOL_TOOLS` in `CMakeLists.txt`).

| Target | The companion's entries |
|:--|:--|
| `package_symbols_macos` | `unreal-streamdeck.dSYM` (`bin/unreal-streamdeck`), `unreal-streamdeck-studio.dSYM` (the binary in `bin/unreal-streamdeck-studio.app`); both stripped (`strip -S`) before `package_suite_macos` copies them |
| `package_symbols_windows` (MSVC) | `unreal-streamdeck.pdb`, `unreal-streamdeck-studio.pdb` |
| `package_symbols_mingw`, `package_symbols_linux` | `unreal-streamdeck.debug`, `unreal-streamdeck-studio.debug` (`objcopy --only-keep-debug`, then `strip --strip-debug` and a `.gnu_debuglink` in the binary) |

The release job lists each archive and fails when `unreal-qt`, `unreal-streamdeck` or `unreal-streamdeck-studio` is
missing from it.
