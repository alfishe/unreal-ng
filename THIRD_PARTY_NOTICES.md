# Third-party notices

unreal-ng is licensed under the GNU General Public License v3.0 or later (see [LICENSE](LICENSE)).
It bundles or links the following third-party components. Each keeps its own license file where one is
vendored; the table records the license as evidenced in this repository and how the component is used.
Full audit: `docs/inprogress/2026-09-02-gplv3-licensing/license-audit.md`.

## Heritage

Parts of the emulator core (Z80 core and opcode tables, memory paging, WD1793/FDD, HDD/ATA, port decoding,
sound rendering, `z80asm`) are a re-engineered port of **UnrealSpeccy 0.3x** by SMT, Alone Coder and deathsoft.
Portions Copyright (C) SMT, Alone Coder, deathsoft. The license of the original sources is being confirmed
(see the audit, item P1); the affected files carry inline attribution comments.

## Components compiled into shipped binaries

| Component | Location | License | Use |
|-----------|----------|---------|-----|
| zstd 1.5.7 | `core/src/3rdparty/zstd/` | BSD-3-Clause (dual BSD/GPL-2.0; BSD elected) | static |
| LZMA SDK 19.00 | `core/src/3rdparty/liblzma/` | Public domain | static |
| miniaudio 0.11.21 | `core/src/3rdparty/miniaudio/` | Public domain / MIT-0 | header |
| blip_buf (C++ port of Shay Green's blip_buf 1.1.0) | `core/src/3rdparty/blip_buf/` | LGPL-2.1-or-later | static |
| lodepng 20200306 | `core/src/3rdparty/lodepng/` | zlib | static |
| digestpp | `core/src/3rdparty/digestpp/` | Public domain | header |
| simpleini 4.17 | `core/src/3rdparty/simpleini/` | MIT | header |
| tinywav | `core/src/3rdparty/tinywav/` | ISC | static |
| simple-fft | `core/src/3rdparty/simple-fft/` | MIT | header |
| CLI11 2.5.0 | `core/src/3rdparty/cli11/`, `core/automation/cli/lib/cli11/` | BSD-3-Clause | header |
| message-center | `core/src/3rdparty/message-center/` | project code (GPL-3.0-or-later) | static |
| gif.h (Charlie Tangora) | `core/recording/src/3rdparty/gif/` | Public domain | static |
| NVIDIA nvEncodeAPI.h | `core/recording/src/platform/windows/nvEncodeAPI.h` | MIT | header; the driver DLL is loaded at run time and never shipped |
| Drogon 1.9.11 | `core/automation/webapi/lib/drogon/` | MIT | static |
| Trantor | `core/automation/webapi/lib/drogon/trantor/` | BSD-3-Clause | static |
| wepoll | `.../trantor/third_party/wepoll/` | BSD-2-Clause | static (Windows) |
| mman-win32 | `.../drogon/third_party/mman-win32/` | MIT | static (Windows) |
| jsoncpp 1.9.5 | `core/automation/webapi/lib/jsoncpp/` | Public domain / MIT | static |
| zlib 1.3.1 | `core/automation/webapi/lib/zlib/` | zlib | static |
| OpenSSL (system) | via `find_package(OpenSSL)` | Apache-2.0 (3.x) | dynamic; static on MSVC. OpenSSL 3.0 or later is required |
| Lua 5.4.7 | `core/automation/lua/lib/lua/` | MIT | static |
| sol2 3.5.0 | `core/automation/lua/lib/sol2/` | MIT | header |
| pybind11 2.13.6 | `core/automation/python/3rdparty/pybind11/` | BSD-3-Clause | header |
| CPython 3.13.1 | fetched at configure time (`core/automation/python/CMakeLists.txt`) | PSF-2.0 | static |
| Qt 6 (Gui, Widgets, Multimedia, Network) | system / aqtinstall | LGPL-3.0 | dynamic |
| QHexView 5.0 | `unreal-qt/3rdparty/QHexView-5.0/` | MIT | static |
| ayumi (interpolation filter excerpt) | `core/src/common/sound/filters/filter_interpolate.h` | MIT | source |

## Tools, tests and benchmarks only

| Component | Location | License |
|-----------|----------|---------|
| googletest 1.15.2 | `lib/googletest/` (submodule) | BSD-3-Clause |
| Google Benchmark 1.9.0 | `lib/benchmark/` (submodule) | Apache-2.0 |
| rapidyaml 0.9.0 / c4core | `core/src/3rdparty/rapidyaml/` | MIT |
| nlohmann/json 3.12.0 | `core/src/3rdparty/json/` | MIT (unused) |
| vcd-writer | `core/src/3rdparty/vcd-writer/` | MIT (unused) |
| libwave | `core/src/3rdparty/wave/` | MIT (unused) |
| ownShell | `testclient/src/3rdparty/ownshell/` | MIT |
| FastLZ 0.5.0 | `tools/poc/01-ttd-compression/cpp/vendored/fastlz/` | MIT |
| python-cmake-buildsystem, cmake-python-build | `core/automation/python/3rdparty/` | Apache-2.0 (unused) |
| Z80 test suites: ZEXALL (GPL-2.0-or-later), z80test by Patrik Rak (MIT), z80bltst (MIT), Z80 XCF Flavor (GPL-3.0-or-later), FUSE test vectors (GPL-2.0-or-later) | `data/testsoft/`, `core/tests/z80/`, `testdata/z80/` | as listed |

## Not covered by the GPL

* ROM images under `data/rom/` and `data/testrom/` — firmware of the respective machines, see
  [data/rom/README-ROMS.md](data/rom/README-ROMS.md).
* Third-party programs used as test fixtures under `testdata/` — see [testdata/NOTICE.md](testdata/NOTICE.md).
* Datasheets and books under `docs/` (Western Digital FD179X datasheet, "The Complete Spectrum ROM Disassembly")
  remain the property of their publishers and are included for reference only.

## Items still being resolved (not compatible or unverified)

* `data/fonts/consolas.ttf` — Microsoft Consolas is proprietary and must be replaced by an OFL font before publication.
* `core/src/3rdparty/z80ex/` — GPL-2.0-only, not compiled; scheduled for removal.
* `core/src/3rdparty/simpleini/convertutf.{c,h}` — legacy Unicode Inc. notice with a field-of-use clause, not compiled; scheduled for removal.
