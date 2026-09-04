# unreal-ng — GPL v3 license audit

Repository: `/Users/dev/Projects/Local GitLab/unreal` (branch master, HEAD 878b4786, working tree clean apart from an untracked docs folder).
Scope: everything that is not the project's own code. Build output dirs (`build/`, `cmake-build-*/`, `.cache/`, `.ccache/`, `Testing/`, `*/CMakeFiles/`) were excluded. No files in the repo were added or modified.

All paths below are relative to the repo root unless absolute.

---

## 0. Executive summary

**Verdict: GPL-3.0 is achievable, but not yet. There are 3 hard blockers, 1 provenance question that must be answered, and ~15 notice/cleanup items.**

Hard blockers (must fix before adding LICENSE):

| # | Item | Path | Problem |
|---|------|------|---------|
| B1 | **Microsoft Consolas font** | `data/fonts/consolas.ttf` (loaded by `unreal-qt/src/main.cpp:23`) | Proprietary Microsoft font; its EULA does not permit redistribution. Cannot ship in any public repo, GPL or not. Replace with an OFL font. |
| B2 | **z80ex disassembler** | `core/src/3rdparty/z80ex/` | Headers say `Released under GNU GPL v2` (no "or later"). GPL-2.0-only is incompatible with GPL-3.0. Currently **dead code** (not compiled — `core/src/CMakeLists.txt` globs `*.cpp` only, these are `.c`; zero `#include` references). Delete the directory. |
| B3 | **Unicode ConvertUTF** | `core/src/3rdparty/simpleini/convertutf.{c,h}` | Old Unicode Inc. notice with a field-of-use restriction ("in the creation of products supporting the Unicode Standard") — treated as non-free/GPL-incompatible by Debian. Also **dead code** (`.c` not globbed, only pulled in by simpleini under `SI_CONVERT_GENERIC`, which is not defined). Delete both files. |

Provenance question (P1): the CPU core, `platform.h`, `z80asm.cpp`, HDD/ATA code, WD1793 and sound-render code are ports of **UnrealSpeccy 0.3x (SMT / Alone Coder / deathsoft)** and still carry their inline comments. The repo contains **no statement of the original UnrealSpeccy license**. See section 1.2 — this must be resolved (verify upstream `unreal_e.txt`; if GPL-2.0-or-later or GPL-3 it's fine; if GPL-2.0-only or "freeware", get written permission).

Everything else is permissive (MIT / BSD / zlib / ISC / public domain / PSF), LGPL-3 (Qt), LGPL-2.1+ (blip_buf origin), GPL-2.0-or-later (FUSE test vectors, ZEXALL), GPL-3.0-or-later (Z80 XCF Flavor), or Apache-2.0 (Google Benchmark — test only; OpenSSL 3 — system). All of these are GPL-3.0 compatible.

Recommended license expression: **GPL-3.0-or-later** (section 5.4).

---

## 1. Existing notices and UnrealSpeccy heritage

### 1.1 What exists today

* **No** `LICENSE`, `COPYING`, `NOTICE` or `AUTHORS` at the repo root. All 27 license files found belong to vendored libraries (listed in section 2).
* `README.md` line 1: "Fully re-engineered Unreal Speccy emulator". No license statement.
* Several docs already *assume* a license file exists:
  * `docs/emulator/design/control-interfaces/README.md:340` — "Documentation is part of the unreal-ng project. See project LICENSE file."
  * `tools/python/README.md:195` — "distributed under the same license".
  * `unreal-videowall/README.md:240,482` — "Part of the Unreal-NG ... project."
* Copyright strings already in build metadata: `Copyright © 2025 UnrealNG Team` in `unreal-qt/CMakeLists.txt:463`, `unreal-videowall/CMakeLists.txt:391`, `unreal-screen-viewer/CMakeLists.txt:254`, `unreal-qt/install/windows/app.rc.in:25`, `unreal-qt/src/install/windows/app.rc.in:25`, and the three macOS `Info.plist` files (`NSHumanReadableCopyright`).
* `core/src/debugger/ttd/ttd.ksy:47,55` declares the TTD on-disk schema as **MIT** ("re-publishable, vendorable, forkable"). Keep that — a file-format spec under MIT alongside a GPL implementation is a deliberate and sensible choice; just document it in THIRD_PARTY/NOTICE so it isn't mistaken for an error.
* No SPDX headers anywhere in own code. Own source files have no copyright headers at all.
* Git history: a single author (Ilia Sharin, 1803 commits, first commit 2020-01-02 "Initial source drop... Ported memory bank switching logic"). Copyright holder is therefore unambiguous: the owner (plus whatever UnrealSpeccy authors' rights survive in ported code).

### 1.2 UnrealSpeccy heritage — evidence in the tree

Literal UnrealSpeccy source remnants (comments naming the original contributors, kept verbatim):

| File | Evidence |
|------|----------|
| `core/src/emulator/cpu/op_noprefix.cpp:688-708` | `//Alone Coder` (six occurrences) |
| `core/src/emulator/platform.h:242,284,475,514,863,942,943` | `//0.37.0`, `//0.36.6 from 0.35b2`, `//Alone Coder (IDC_SOUNDFILTER)` |
| `core/src/debugger/assembler/z80asm.cpp:189,229,454,465` | `//Alone Coder 0.36.6` |
| `core/src/emulator/io/hdd/hddio.h:55-59` | `//SMT (crashes if ...)`, `//Alone Coder (CD doesn't work ...)` |
| `core/src/emulator/io/hdd/hdd.h:128` | `//Alone Coder` |
| `core/src/emulator/cpu/z80.cpp:178,779` | "port of the original UnrealSpeccy step() logic", "Same approach as the original Unreal Speccy handle_int" |
| `core/src/emulator/memory/memory.h:260`, `memory.cpp:792`, `cpu/core.cpp:512` | "port of the original UnrealSpeccy set_mode() / set_banks()" |
| `core/src/emulator/config.cpp:292,523` | "original UnrealSpeccy 'PortFF' option", "Unreal Speccy conf.paper calibration" |
| `core/src/emulator/io/fdc/wd1793.cpp:2594` | "like unrealspeccy's getindex()" |
| `core/src/emulator/ports/models/portdecoder_pentagon128.cpp:93`, `video/ulacontention.cpp:151` | "Matches the original UnrealSpeccy: io.cpp ..." |
| `core/src/emulator/cpu/daa_tabs.cpp`, `op_cb/op_dd/op_ddcb/op_ed/op_fd.cpp` | File layout and names mirror UnrealSpeccy's Z80 core (`op_*.cpp`, `daa_tabs`) |
| `core/src/common/sound/filters/filter_unreal.{h,cpp}` | "UnrealFilter" — the UnrealSpeccy `sndrender` resampler |
| `core/tests/debugger/breakpoints_test.cpp:1128` | "UnrealSpeccy set_banks() semantics" |
| `docs/inprogress/2026-08-17-conditional-breakpoints/research/unreal-speccy.md:3` | Identifies the reference source as mkoloberdin/unrealspeccy (0.38.x, SMT / Alone Coder / deathsoft) and tslabs/zx-evo |

Conclusion: this is a **derivative work of UnrealSpeccy** for at least the Z80 core, memory paging, port decoding, WD1793, HDD/ATA, Z80 assembler and sound resampler, regardless of how much has been rewritten. The original authors' copyright survives in those parts.

**License of the original UnrealSpeccy — not determinable from the repo.** No `unreal_e.txt`/`unreal_r.txt` or license text from the original distribution is present, and no doc in `docs/` states it (grep for licen/freeware/GPL in the UnrealSpeccy research docs returned nothing).

From general knowledge, stated with the appropriate uncertainty:

* The original UnrealSpeccy 0.3x source archives (SMT, later Alone Coder / deathsoft) shipped a readme that describes the program as free and the sources as available; I recall the SourceForge project page (`sourceforge.net/projects/unrealspeccy/`) listing **GPL v2**, but I cannot verify this from here and cannot confirm whether it is "v2" or "v2 or later". Treat as **unverified**.
* **UnrealSpeccyP** (djdron, `github.com/djdron/UnrealSpeccyP`), a direct derivative of the same code base, is distributed under **GPL-3.0** and its file headers name `SMT, Dexus, Alone Coder, deathsoft, djdron, scor` as copyright holders. That is reasonably strong evidence that the original authors accepted GPL terms and that GPL-3 distribution of UnrealSpeccy-derived code is an accepted practice in that community — but it is evidence, not a license grant to this project.
* The tslabs/zx-evo (PentEvo) fork of UnrealSpeccy is also publicly maintained; its license file should be checked as a second data point.

**Action required (P1):** obtain the original `unreal_e.txt` / license text from a 0.37/0.38 source archive and record it (quote it in `THIRD_PARTY_NOTICES.md`). Outcomes:
  * GPL-2.0-or-later, or GPL-3 → compatible; add "Portions Copyright (C) SMT, Alone Coder, deathsoft (UnrealSpeccy)" to the notices and to the headers of the files listed above.
  * GPL-2.0-only → **incompatible with GPL-3**; would require written permission from the authors (Alone Coder and deathsoft are reachable in the ZX community) or a GPL-2.0-or-later license instead.
  * "Freeware / no license" → also requires permission (no license = no right to redistribute derivative code at all).

### 1.3 Other borrowed code inside "own" directories (no license file, needs notices)

| Path | Origin (from comments in file) | Upstream license | Status |
|------|-------------------------------|------------------|--------|
| `core/src/common/sound/filters/filter_interpolate.h:69-70` | "extracted from ayumi project" — `github.com/true-grue/ayumi` | **MIT** (upstream; unverified here — no MIT text in repo) | Compatible. Add the ayumi copyright + MIT notice to the file and to THIRD_PARTY_NOTICES. |
| `core/src/common/sound/filters/audio_character_chain.h:47` | "Ported from amiga-paula project (PWM renderer post-processing)" | **Unknown** — project not identified, no URL. | **Needs owner input.** If it is the owner's own project, say so in the header; if third-party, find and record its license. |
| `core/src/emulator/video/screen.cpp:442`, `config.cpp:553` | Timing constants "derived from MiSTer HDL ula.sv" | MiSTer ZX Spectrum core is GPL-licensed (v2 as I recall — unverified) | Only numeric hardware-timing facts were taken, not code — low risk. Note it in NOTICES as reference material. |
| `core/src/3rdparty/message-center/*` (14 files) | No notice at all. Believed to be the owner's own library (`alfishe/message-center`), used in 49 files. | — | Add a header/notice stating ownership or its license; treat as project code. |
| `core/src/3rdparty/blip_buf/blip_buf.{h,cpp}` | "Clean C++ port of Shay Green's blip_buf library" — no license text kept | Upstream blip_buf is **LGPL-2.1-or-later** | Compatible with GPL-3 (LGPL-2.1 §3 allows conversion to GPL "version 2 or any later version"). A port is still a derivative — restore the LGPL notice and Shay Green's copyright in both files. Used by `emulator/sound/beeper.cpp` and `covox.cpp`. |
| `core/src/debugger/analyzers/rom-print/zxspectrumfont.h` | "font is extracted from the 48K ROM at 0x3D00-0x3FFF" | Amstrad copyright (ROM data) | Covered by the Amstrad emulator-distribution permission (section 3), but it is ROM data compiled into a GPL binary. Mention in the ROM note. |
| `core/tests/z80/z80test/z80test_vectors.h` | Auto-generated from Patrik Rak's z80test (`raxoft/z80test`) test definitions | **MIT** (upstream; unverified here) | Compatible. Add notice. |
| `testdata/z80/fuse/tests.in`, `tests.expected` | FUSE emulator Z80 core tests | **GPL-2.0-or-later** (FUSE) | Compatible with GPL-3.0(-or-later). No reference to these files was found under `core/tests/z80/` — may be unused. Add notice or remove. |

---

## 2. Third-party component inventory

Legend for "Linkage": static = compiled into the core/app binaries; header-only; tool/test-only = not part of shipped emulator binaries; dynamic = linked at run time; process = invoked as a separate executable.

### 2.1 Compiled into the shipped emulator (core / recording / automation / Qt apps)

| Component | Path | Version | License (evidence) | Linkage | GPL-3 verdict |
|-----------|------|---------|--------------------|---------|---------------|
| zstd | `core/src/3rdparty/zstd/` | 1.5.7 (`lib/zstd.h:112-114`) | **BSD-3-Clause OR GPL-2.0** — `LICENSE` (BSD), `COPYING` (GPLv2 text); `README.md:113-115` says project takes BSD | static (`zstd::libzstd`), public dep of core | Compatible (use BSD option; say so in NOTICES) |
| LZMA SDK excerpt | `core/src/3rdparty/liblzma/` | 19.00 (`README.md:3`) | **Public domain** — `include/LzmaDec.h:2` "Igor Pavlov : Public domain" (no LICENSE file) | static (`lzma`), linked PRIVATE into core; only test code includes it | Compatible |
| miniaudio | `core/src/3rdparty/miniaudio/miniaudio.h` | 0.11.21 (`miniaudio.h:3724-3726`) | **Public domain OR MIT-0** — `LICENSE` | header-only (4 own files) | Compatible |
| blip_buf (C++ port) | `core/src/3rdparty/blip_buf/` | port of blip_buf 1.1.0 | **LGPL-2.1-or-later** upstream; **no notice in repo** | static | Compatible; **notice missing** (see 1.3) |
| lodepng | `core/src/3rdparty/lodepng/` | 20200306 (`lodepng.h:2`) | **zlib** — header in `lodepng.h:4-20` | static (2 own files) | Compatible |
| digestpp | `core/src/3rdparty/digestpp/` | unversioned | **Public domain** — every header: "written by kerukuro and released into public domain" | header-only (`rom.cpp`, `loader_scl_test.cpp`) | Compatible |
| simpleini | `core/src/3rdparty/simpleini/simpleini.h` | 4.17 (`simpleini.h:8`) | **MIT** — `simpleini.h:170-175` | header-only (3 own files) | Compatible |
| ConvertUTF | `core/src/3rdparty/simpleini/convertutf.{c,h}` | 2001-2004 | **Unicode Inc. legacy notice** with field-of-use clause (`convertutf.h:1-21`) | **not compiled**, not included | **INCOMPATIBLE (B3)** — delete |
| tinywav | `core/src/3rdparty/tinywav/` | 2015-2022 | **ISC** — `LICENSE` (Martin Roth) | static (3 own files) | Compatible |
| simple-fft | `core/src/3rdparty/simple-fft/` | 2013-2020 | **MIT** — `LICENSE.md` (Dmitry Ivanov) | header-only (1 own file); its own CMake mentions FFTW/OpenMP only for its unit tests, which are not built | Compatible |
| CLI11 | `core/src/3rdparty/cli11/CLI11.hpp` and duplicate `core/automation/cli/lib/cli11/CLI11.hpp` | 2.5.0 (`CLI11.hpp:1`) | **BSD-3-Clause** — header lines 8-30 (University of Cincinnati / Henry Schreiner) | header-only | Compatible (two identical copies — dedupe) |
| message-center | `core/src/3rdparty/message-center/` | — | **No notice** (believed owner's own code) | static, 49 own files | Compatible once notice added |
| gif.h / gif.cpp (Charlie Tangora) | `core/recording/src/3rdparty/gif/` | — | **Public domain** — `gif.h:4` | static (recording lib) | Compatible |
| NVIDIA nvEncodeAPI.h | `core/recording/src/platform/windows/nvEncodeAPI.h` | Video Codec SDK header, (c) 2010-2024 | **MIT** — header lines 1-26 ("This copyright notice applies to this header file only") | header; runtime library `nvEncodeAPI64.dll` is **loaded dynamically** via `LoadLibraryA` (`nvenc_encoder.cpp:113`, `nvenc_probe.cpp:23`) and never distributed | Compatible. The proprietary driver DLL is a user-installed system component (GPL "System Library" exception applies; nothing NVIDIA-proprietary is shipped). Do **not** vendor the rest of the Video Codec SDK (its samples/libs are under a proprietary license). |
| Windows Media Foundation, D3D11, DXGI | `core/recording/CMakeLists.txt:97,101` (`mfplat mfuuid mf d3d11 dxgi ole32`) | OS SDK | Proprietary OS components | dynamic (system) | Compatible via System Library exception |
| Apple VideoToolbox, AVFoundation, CoreMedia, CoreVideo, AudioToolbox, Foundation, CoreFoundation | `core/recording/CMakeLists.txt:72-78`, `core/src/CMakeLists.txt:151-152` | OS SDK | Proprietary OS frameworks | dynamic (system) | Compatible via System Library exception |
| ffmpeg | `core/recording/src/ffmpeg_probe.cpp`, `encoders/ffmpeg_pipe_encoder.cpp` | user-installed | LGPL-2.1+/GPL-2+ depending on build | **separate process** (pipe); not linked, not distributed | Compatible / no coupling. State in docs that ffmpeg is an optional external tool and not part of the distribution. |
| Drogon | `core/automation/webapi/lib/drogon/` | 1.9.11 (`CMakeLists.txt:26-28`) | **MIT** — `LICENSE`; `orm_lib/COPYING` MIT (ORM disabled: `BUILD_ORM OFF`) | static (25 own files include it) | Compatible |
| Trantor (Drogon dep) | `core/automation/webapi/lib/drogon/trantor/` | bundled | **BSD-3-Clause** — `trantor/License` (Tao An) | static | Compatible |
| wepoll (Trantor dep) | `.../trantor/third_party/wepoll/` | 2012-2020 | **BSD-2-Clause** — `LICENSE` (Bert Belder) | static, Windows only | Compatible |
| mman-win32 (Drogon dep) | `.../drogon/third_party/mman-win32/` | — | **MIT** — `README.md:10` (no LICENSE file) | static, Windows only | Compatible |
| Trantor bundled crypto | `.../trantor/trantor/utils/crypto/{md5,sha1,sha256,sha3,blake2}.cc` | — | md5/sha256: Brad Conte, public domain; sha1: "100% Public Domain"; blake2/sha3 no notice seen in first 20 lines (upstream: CC0/Apache-2.0/OpenSSL triple for BLAKE2) | static (used only when OpenSSL/Botan absent) | Compatible |
| jsoncpp | `core/automation/webapi/lib/jsoncpp/` | 1.9.5 (`include/json/version.h:12`) | **Public Domain / MIT** — `LICENSE` | static (19 own files) | Compatible |
| zlib | `core/automation/webapi/lib/zlib/` (+ generated `core/automation/webapi/zlib/zconf.h`) | 1.3.1 (`zlib.h:40`) | **zlib** — `LICENSE` | static (`zlibstatic`), consumed by Drogon | Compatible |
| **OpenSSL** | not vendored; `find_package(OpenSSL)` in `core/automation/webapi/CMakeLists.txt:27,57`, `cmake/DependencyCheck.cmake:56`, `core/automation/python/CMakeLists.txt:100`; Homebrew path hardcoded; `CMakeLists.txt:166-169` forces **static** OpenSSL on MSVC (`OPENSSL_USE_STATIC_LIBS`) | system (Homebrew/apt/vcpkg) | OpenSSL **3.x: Apache-2.0**; OpenSSL **1.1.x and older: OpenSSL/SSLeay dual license (GPL-incompatible)** | dynamic on macOS/Linux; **static on MSVC** | **Compatible with conditions**: (a) require OpenSSL >= 3.0 (Apache-2.0 is GPL-3-compatible, not GPL-2) and say so in README/CMake, and/or (b) add the standard "OpenSSL linking exception" to the LICENSE notice. Without one of these, the MSVC static build would be a GPL violation if built against OpenSSL 1.1. |
| Lua | `core/automation/lua/lib/lua/lua-5.4.7/` | 5.4.7 (`src/lua.h:19-24`) | **MIT** — `doc/readme.html` "License" section; copyright notice at end of `lua.h` | static (`lua::static`), 4 own files | Compatible (Lua asks for credit in docs — put it in NOTICES) |
| sol2 | `core/automation/lua/lib/sol2/sol2-3.5.0/` | 3.5.0 | **MIT** — `LICENSE.txt` (Rapptz, ThePhD) | header-only (5 own files) | Compatible |
| pybind11 | `core/automation/python/3rdparty/pybind11/` | 2.13.6 (`pybind11/_version.py`, `detail/common.h:12-14`) | **BSD-3-Clause** — `LICENSE` (Wenzel Jakob) | header-only (5 own files) | Compatible |
| CPython | not vendored; `ExternalProject_Add(python_build_ep GIT_REPOSITORY https://github.com/python/cpython.git GIT_TAG v3.13.1)` in `core/automation/python/CMakeLists.txt:131-133`, built `--disable-shared` and linked **statically**; system Python on MinGW | 3.13.1 | **PSF-2.0** (GPL-compatible per FSF) | static (macOS/Linux) or dynamic (MinGW) | Compatible. Note: network fetch at configure time; document that the resulting binary embeds CPython and carries the PSF notice. |
| python-cmake-buildsystem | `core/automation/python/3rdparty/python-cmake-buildsystem/` | — | **Apache-2.0** — `LICENSE_Apache_20` | build tooling only; referenced only in a comment (`python/CMakeLists.txt:25`) | Compatible with GPL-3 (not GPL-2). Currently unused — consider removing. |
| cmake-python-build | `core/automation/python/3rdparty/cmake-python-build/` | — | **Apache-2.0** — `LICENSE` | build tooling; **not referenced anywhere** | Compatible with GPL-3. Unused — consider removing. |
| Qt 6 | not vendored. `unreal-qt/CMakeLists.txt:185-188` (Gui, Multimedia, Widgets), `unreal-videowall/CMakeLists.txt:167` (Widgets), `unreal-screen-viewer/CMakeLists.txt:111` (Widgets, Network), `tools/poc/02-ttd-gui` (Widgets, Gui). Search paths reference 6.5.0/6.5.3/6.9.3; CI uses `jurplel/install-qt-action@v4`; docker installs via `aqtinstall`. | 6.5-6.9 | **LGPL-3.0** (Qt open-source edition; these modules are all LGPL — none of the GPL-only modules such as Charts/DataVisualization/VirtualKeyboard are used) | dynamic (frameworks/DLLs deployed alongside; `cmake/WinDeployQt.cmake`) | Compatible. LGPL-3 obligations: ship Qt as separate shared libraries (already the case), don't strip the user's ability to replace them, include Qt's LGPL notice in NOTICES. If a commercial Qt license were used instead, GPL is still fine (Qt commercial permits it). |
| QHexView | `unreal-qt/3rdparty/QHexView-5.0/` | 5.0 | **MIT** — `LICENSE` (Dax89) | static (`QHexView` target) | Compatible |

### 2.2 Vendored but currently unused / test-only

| Component | Path | Version | License (evidence) | Use | Verdict |
|-----------|------|---------|--------------------|-----|---------|
| **z80ex** (disassembler tables) | `core/src/3rdparty/z80ex/` | 0.16.x era | **GPL-2.0 (no "or later")** — `z80ex.h:7`, `z80ex_dasm.c:7`, `z80ex_common.h:7`, `typedefs.h:7`, `z80ex_dasm.h:7`: "Released under GNU GPL v2"; also "contains some code from the FUSE project" (FUSE is GPL-2.0-or-later) | **not compiled** (`.c` files, glob is `*.cpp`), zero includes | **INCOMPATIBLE as written (B2)** — remove. If the owner ever wants it back, confirm with the z80ex author (Pigmaker57/boo_boo) that "v2" means "v2 or later". |
| nlohmann/json | `core/src/3rdparty/json/single_include/nlohmann/json.hpp` | 3.12.0 (`json.hpp:3`, SPDX `MIT` at line 7) | **MIT** (SPDX in header) | header-only; **no includes found** in own code | Compatible; unused — remove or keep with notice |
| rapidyaml (+ c4core) | `core/src/3rdparty/rapidyaml/ryml_all.hpp` | ryml 0.9.0 / c4core 0.2.6 (`ryml_all.hpp:21332,203`) | **MIT** — `LICENSE.txt` (Joao Paulo Magalhaes) | header-only; only `core/tests/3rdparty/rapidyaml_test.cpp` | Compatible (test-only) |
| vcd-writer | `core/src/3rdparty/vcd-writer/` | port of PyVCD | **MIT** — `LICENSE` (Kirill Golikov) | `.cpp` files are compiled by the glob but nothing includes the headers | Compatible; unused |
| libwave (Audionamix) | `core/src/3rdparty/wave/` | 2020 | **MIT** — `LICENSE` | `.cc` files not globbed; no includes | Compatible; unused |
| googletest | `lib/googletest/` (git submodule, `.gitmodules`) | 1.15.2 (`CMakeLists.txt:7`; git `v1.14.0-163-gdf1544bc`) | **BSD-3-Clause** — `LICENSE` (Google) | test-only | Compatible |
| Google Benchmark | `lib/benchmark/` (git submodule) | 1.9.0 (`CMakeLists.txt:4`; git `v1.9.0-12-g761305e`) | **Apache-2.0** — `LICENSE` | benchmark-only, never shipped | Compatible with GPL-3 only (this alone rules out GPL-2 for the tree as a whole, though it never enters a shipped binary) |
| ownShell | `testclient/src/3rdparty/ownshell/` | 2015 | **MIT** — each file header ("see AUTHORS file" — **AUTHORS file is missing**) | static into `testclient` (dev tool) | Compatible; restore upstream AUTHORS/LICENSE file |
| FastLZ | `tools/poc/01-ttd-compression/cpp/vendored/fastlz/` | 0.5.0 (`fastlz.h:24-26`) | **MIT** — header (Ariya Hidayat) | PoC only | Compatible |
| lz4 / snappy / brotli / zlib (system) | `tools/poc/01-ttd-compression/cpp/CMakeLists.txt:73-108` (`find_library`) | system | BSD-2 / BSD-3 / MIT / zlib | PoC only, dynamic | Compatible |
| Python packages | `tools/python/requirements.txt` (psutil BSD-3, Pillow MIT-CMU/HPND, pywin32 PSF, posix_ipc BSD); `tools/verification/ttd-analyzer/requirements.txt` (zstandard BSD-3, pillow); `tools/verification/ttd-scrubber/requirements.txt` (**PySide6 LGPL-3**, requests Apache-2.0); `tools/verification/webapi/requirements.txt` (requests, pytest MIT, openapi-spec-validator Apache-2.0, prance MIT, jsonschema MIT, faker MIT); `tools/verification/videowall/requirements.txt` (requests) | — | as listed | tool-only, not vendored, installed by the user | Compatible (nothing is redistributed) |
| GitHub Actions | `.github/workflows/*.yml`: actions/checkout, upload/download-artifact, setup-python (MIT); docker/* actions (Apache-2.0); jurplel/install-qt-action (MIT); msys2/setup-msys2 (MIT); egor-tensin/vs-shell (MIT); softprops/action-gh-release (MIT) | — | — | CI only | N/A |
| Docker base | `docker/Dockerfile*`: `debian:testing-slim`, ICU 56.1 source (ICU license, BSD-like), `aqtinstall` (MIT) | — | — | CI/build image only | N/A |
| vcpkg / conan | No `vcpkg.json` or `conanfile` in the tree. vcpkg is only *suggested* for MSVC OpenSSL (`cmake/DependencyCheck.cmake:7-49`). | — | — | — | N/A |

### 2.3 Things checked and NOT found
No ImGui, SDL, portaudio, stb, OpenGL loaders, libpng, FFmpeg/x264 headers or libs, NVENC SDK samples, sqlite/hiredis (Drogon ORM disabled), brotli (`BUILD_BROTLI OFF`), yaml-cpp (`BUILD_YAML_CONFIG OFF`), Botan, or any "non-commercial"/"no modification" clause in any vendored file.

---

## 3. ROM images

ROMs are firmware, not part of the GPL'd program; they cannot be relicensed and must be documented separately. Nothing in the repo currently states their provenance or terms (no README/txt anywhere under `data/`).

### 3.1 Inventory — `data/rom/` (58 files, ~6 MB)

| Family | Files | Known position (from general knowledge — verify before publishing) |
|--------|-------|-----------------------------------------------------------------|
| Sinclair/Amstrad original ROMs | `48.rom`, `128.rom`, `128_low.rom`, `plus2.rom`, `plus2a.rom`, `plus3.rom`, `plus341.rom`, `1982.rom` (likely the 1982 48K ROM), `sos.rom` (48K BASIC "SOS" as used by Russian clones), `48for128.rom`, `service.rom` (128K test ROM?), `if1.rom` (Interface 1) | Amstrad plc (owner of the Sinclair ROM copyrights, now Sky) granted permission in 1999 (Cliff Lawson, comp.sys.sinclair) to distribute the 48K/128K/+2/+2A/+3 ROM images with emulators, provided Amstrad's copyright is acknowledged and no charge is made for the ROMs themselves. This is the basis every emulator (FUSE, ZEsarUX, ...) relies on. Interface 1 ROM is covered by the same statement. Document it verbatim in a ROMS note. |
| TR-DOS / Beta Disk | `trdos.rom`, `trdos503.rom`, `trdos504t.rom`, `trd504tm.rom`, `dos.rom`, `dos6_10e.rom` (TR-DOS 6.10E), `128tr!.rom` | Copyright Technology Research Ltd (defunct). No formal permission exists; universally distributed by emulators (FUSE ships `trdos.rom` in `fuse-emulator-roms`, Debian treats it as non-free). Flag as "distributed on the same customary basis as other emulators; no explicit license". 5.04T/5.04TM/6.xx are community patches (Vyacheslav Mednonogov et al.) with no license. |
| Pentagon | `pentagon.rom`, `pentagon128k.rom`, `glukpen.rom`, `glukpen_.rom` | Composite of Amstrad 48K/128K BASIC + TR-DOS + Mr Gluk's Reset Service. Amstrad part covered as above; Gluk Reset Service (Mr Gluk) — freely distributed by its author, no formal license. |
| Scorpion / KAY / ATM / Profi / other Russian clones | `scorpion.rom`, `scorp295.rom`, `scorp_prof401.rom` (ProfROM 4.01), `kay1024.rom`, `kay1024b.rom`, `atm1.rom`, `atm2.rom`, `glukatm.rom`, `atmtest3.rom`, `profi.rom`, `lsy256.rom`, `qu7v42.rom`, `qc_3_05.rom`, `madrom.rom`, `zxi1.rom`, `2006.rom`, `xbios135.rom`, `sgen.rom`, `gd.rom`, `gmx.rom`, `1993.rom` (2 MB), `ZXM-Phoenix_bios.bin` | Various hardware vendors (Scorpion ZS, NEMO, MicroArt, Kondor...) and individual authors; all include Amstrad BASIC portions. No licenses known. Customary distribution with UnrealSpeccy for 20+ years, but no rights statement exists. |
| ZX Evolution / TS-Conf | `zxevo.rom`, `ts-bios.rom`, `ts-bios-gluk.rom`, `ts-bios-qc311.rom`, `ts-bios-rc196.rom` | Produced by the NedoPC / tslabs community; the ZX-Evo firmware sources are public (tslabs/zx-evo on GitHub). Check that repo's license and cite it. |
| General Sound | `gs104.rom`, `gs105a.rom`, `bootGS.rom` | General Sound firmware (X-Trade / NedoPC). Sources are public in the NedoPC community; license unknown. |
| G-DOS / TK90X / TK95 / OpenSE | `gdos.rom`, `gdos-pd.rom` ("pd" = public domain variant), `tk90.rom`, `tk95.rom` (Microdigital Brazil), `opense.rom` (OpenSE BASIC, Andrew Owen) | `gdos-pd`: public domain per its name. TK90X/TK95: Microdigital ROMs — same status as Amstrad-derived clones, no permission. OpenSE BASIC was released under GPL (v2/v3 — verify; its successor SE Basic IV is Apache-2.0). |

`data/testrom/zx-diagnostics.rom` — Brendan Alford's ZX Diagnostics; I believe it is GPL-3 (unverified) — check its GitHub repo and cite.

`data/symbols/*.map`, `sos.l` — symbol/label tables for the 48K/128K ROMs. Derived from published disassemblies (Logan & O'Hara / SkoolKit-style labels). Facts/labels, low risk, but attribute the source.

`core/src/debugger/analyzers/rom-print/zxspectrumfont.h` — the 48K ROM character set embedded in the C++ binary (Amstrad data). Covered by the Amstrad permission for emulator use; mention it.

### 3.2 Recommendation
Do not delete anything. Add `data/rom/README-ROMS.md` (or `ROMS-LICENSE.md` at root) stating: (1) ROMs are not covered by the GPL; (2) the Amstrad permission text and acknowledgement "Amstrad have kindly given their permission for the redistribution of their copyrighted material but retain that copyright"; (3) a per-file table of origin/author/status as above, with "no explicit license, distributed on customary basis" where that is the truth; (4) a takedown contact. Consider whether `1993.rom` (2 MB) and other large exotic BIOSes are actually needed by the emulator configs (`data/configs/*/unreal.ini` reference only a handful).

---

## 4. Test data (`testdata/`, `data/testsoft`, `data/testsnapshots`, `data/testtapes`, `docs/`)

These are third-party copyrighted works used as fixtures. They are not "program" and would **not** be covered by the GPL; each needs its own statement.

### 4.1 Commercial / scene software (copyright of their authors, no license)
* `testdata/loaders/tap/DIZZY_X_*.tap` (7 files), `greenberet.tap`, `echology.tap`, `lphp.tap`, `traffic_lights.tap`, `insult.tap`, `earshaver.tap`, `action.tap`/`action.bas`
* `testdata/loaders/trd/EyeAche.trd`, `Satisfaction.trd`, `atarin.trd`, `zx-format8.trd`; `testdata/loaders/scl/eyeache2.scl`, `insult.scl`; `testdata/loaders/fdi/VORON1.FDI`, `VORON2.FDI`; `testdata/loaders/udi/Nasledie.udi`, `beta128-empty.udi`
* `testdata/loaders/sna/*.sna` — demos and games (7threality, across-the-edge, aleste1, atarin, atebit, earshaver, eyeache1, insult-1..3, multifix, s4b-1/2, sil4aaab, test4.30, vibrations, action, X/Y/2 ...), `testdata/loaders/z80/dizzyx.z80`, `BBG128.z80`, `newbench.z80`, `data/testsnapshots/I.z80`, `II.z80`, `III.z80`
* `testdata/ttd/*.ttd` (4 files, 0.5-3.6 MB) — TTD recordings that **embed full RAM images** of `7threality` and `across-the-edge` demos.
* `testdata/sound/turbosound/ts_my.trd`, `data/testtapes/2005-01-16.tap`
* `tools/verification/webapi/resources/test_disk.trd`, `test_snapshot.sna`, `test_tape.tap`
* `docs/inprogress/2026-08-26-wildplayer-analysis/*.asm` — disassemblies of a third-party music player.

Demoscene productions are customarily freely distributed (and "Dizzy X" / Green Beret are commercial games whose rights holders — Codemasters/Oliver Twins, Konami — have not released them). None of it is licensable by the project.

### 4.2 Test programs with known licenses (compatible; need attribution)
* `data/testsoft/ZEXALL/zex*.tap` — Frank Cringle's Z80 exerciser, **GPL-2.0-or-later** (the truncated `testdata/loaders/z80/invalid/truncated_v1.z80:4-16` even contains its GPL header).
* `data/testsoft/Test/z80bltst.{trd,tap}`, `z80_block_flags_test.asm` — Peter "Ped" Helcmanovsky, **MIT** (`z80_block_flags_test.asm:1`).
* `docs/inprogress/2026-01-18-z80-hidden-flags/Z80 XCF Flavor.{asm,sna}`, `testdata/loaders/sna/z80-xcf-flavor.sna` — Manuel Sainz de Baranda y Goñi, **GPL-3.0-or-later** (`CITATION.cff:19`, `.asm:5-13`).
* `testdata/loaders/sna/z80full.sna`, `z80flags.sna` — Patrik Rak's z80test, **MIT** (unverified here).
* `testdata/loaders/sna/IntTest+.sna`, `tap/IntTest+.tap`, `Timing_Tests-48k_v1.0.sna`, `aytest_0.2.sna`, `AYtest_v0.2.tap`, `aydetect.tap`, `data/testsoft/AccuracyCoinZX/*` — community test programs; authors/licenses not recorded in repo (**unknown**).
* `testdata/z80/fuse/tests.in`, `tests.expected` — FUSE, **GPL-2.0-or-later**.
* `testdata/analyzers/basic/*.B`, `testdata/loaders/*/invalid/*` — generated/own fixtures, fine.

### 4.3 Third-party documents in `docs/`
* `docs/rom/CompleteSpectrumROMDisassembly.md` (+ outline) — "The Complete Spectrum ROM Disassembly" by Logan & O'Hara (Melbourne House, 1983): a copyrighted book. The comp.sys.sinclair transcription circulates widely but there is no license. Flag; keep only if the owner accepts the risk, and mark it clearly as not under the project license.
* `docs/WD1793/datasheeets/*.pdf` (WD179X, FD179X-01, Fujitsu MB8876A, AM-210 manual, Computer Design 1980 article, `floppy.pdf`), `docs/z80/z80_last_secrets.pdf` (also duplicated under `docs/inprogress/...`), `docs/z80/z80-memptr.txt` — vendor datasheets and articles, copyright of their publishers. Customary to keep as reference; mark as "third-party documents, not covered by the license".

### 4.4 Recommendation
Option A (simplest): keep everything, add `testdata/NOTICE.md` (and a line in `THIRD_PARTY_NOTICES.md`) listing each fixture with author/origin and the statement that these files are the copyright of their respective authors, included solely for automated testing/interoperability, are not licensed under the GPL, and will be removed on request. Attribute ZEXALL, z80test, z80bltst, Z80 XCF Flavor, FUSE explicitly with their licenses.
Option B (cleaner for a public repo): move the commercial games (Dizzy X, Green Beret) and the multi-MB `.ttd`/`.udi`/`.fdi` images out of the public tree into a separate private "unreal-ng-testdata" repo or Git LFS bucket fetched by CI; keep only the openly licensed test programs and synthetic fixtures in-tree. This removes the least defensible items (commercial games) while keeping the test-suite reproducible.

---

## 5. Summary and recommendations

### 5.1 Blockers (must fix before LICENSE is added)
1. **`data/fonts/consolas.ttf`** — Microsoft proprietary; remove and replace with an SIL OFL 1.1 font (JetBrains Mono, Cascadia Code, Fira Mono, DejaVu Sans Mono, Inconsolata). Update `unreal-qt/src/main.cpp:23-56` and the stylesheet font-family lists (`speedcontrolwidget.cpp:43,106`).
2. **`core/src/3rdparty/z80ex/`** — GPL-2.0 (no "or later") and dead code; delete.
3. **`core/src/3rdparty/simpleini/convertutf.{c,h}`** — non-free Unicode notice and dead code; delete.
4. **UnrealSpeccy origin license (P1)** — verify and record; obtain permission if it is GPL-2.0-only or unlicensed. This is the only item that could genuinely prevent GPL-3.

### 5.2 Conditions / notices required
* OpenSSL: require >= 3.0 and/or add the OpenSSL linking exception paragraph to LICENSE/README (the MSVC build links it statically).
* Qt 6 (LGPL-3): keep dynamic linking; add Qt notice.
* Apache-2.0 components (Google Benchmark, python-cmake-buildsystem, cmake-python-build, OpenSSL 3): fine under GPL-3, incompatible with GPL-2 — one more reason for GPL-3.
* Add missing license text for: blip_buf (LGPL-2.1+, Shay Green), ayumi filter (MIT, Peter Sovietov), message-center (owner's notice), ownShell AUTHORS, z80test vectors (MIT), FUSE test vectors (GPL-2+), amiga-paula (unknown — owner to clarify), nlohmann/json/rapidyaml/vcd-writer/libwave (unused — remove or keep with notice), duplicate CLI11 copy (dedupe).
* Lua requests credit in the product docs; give it.
* MiSTer-derived timing constants: mention as reference source.

### 5.3 Files to add (proposed)
1. **`LICENSE`** — verbatim GPL-3.0 text (`https://www.gnu.org/licenses/gpl-3.0.txt`). Add a short preamble file or README section: "unreal-ng is Copyright (C) 2020-2026 Ilia Sharin and contributors. Portions derived from UnrealSpeccy, Copyright (C) SMT, Alone Coder, deathsoft [and others per the original readme]. This program is free software: ... either version 3 of the License, or (at your option) any later version." Plus the OpenSSL exception if chosen.
2. **`THIRD_PARTY_NOTICES.md`** — the table from section 2 with the full license text (or path to it) for every vendored component, including the "we take the BSD option" note for zstd, the LGPL notice for Qt and blip_buf, PSF for CPython, and the MIT statement for the `ttd.ksy` schema. Keep every upstream `LICENSE` file in place (they are already there for most).
3. **`data/rom/README-ROMS.md`** (or root `ROMS-LICENSE.md`) — section 3.2 content, Amstrad permission quoted.
4. **`testdata/NOTICE.md`** — section 4.4 content.
5. **SPDX headers**: recommended but optional. For a single-author project a one-line `// SPDX-License-Identifier: GPL-3.0-or-later` + `// Copyright (C) 2020-2026 Ilia Sharin` at the top of own `.h/.cpp/.mm/.py` files is cheap to add with a script and makes the UnrealSpeccy-derived files easy to mark separately ("Portions Copyright (C) SMT/Alone Coder/deathsoft, from UnrealSpeccy"). Do NOT add SPDX headers to vendored third-party files. Files intentionally under a different license (`ttd.ksy`, MIT) keep their own identifier.
6. Fix the docs that already point to a LICENSE file (`docs/emulator/design/control-interfaces/README.md:340`) once it exists, and add a "License" section to `README.md`.
7. `README.md` build docs: state that OpenSSL >= 3 and Qt 6 (LGPL) are external prerequisites and that ffmpeg is an optional external tool.

### 5.4 GPL-3.0-or-later vs GPL-3.0-only
Recommend **GPL-3.0-or-later**.
* Inbound code is GPL-2.0-or-later (FUSE vectors, ZEXALL), GPL-3.0-or-later (Z80 XCF Flavor), LGPL-2.1-or-later (blip_buf), LGPL-3 (Qt), Apache-2.0 (Benchmark, OpenSSL 3) and permissive — every one of these is compatible with GPL-3.0-or-later, and nothing found requires "-only".
* "-or-later" keeps the project compatible with future GPL versions and with the wider ZX emulator ecosystem (UnrealSpeccyP, ZX Diagnostics, Z80 XCF Flavor are all "or later"), which matters if code is ever exchanged with them.
* "-only" gives no benefit here: the sole argument for it (distrust of unknown future GPL text) is outweighed by the loss of compatibility with GPL-2.0-or-later-only inbound code in the future.
* Both options are equally affected by the UnrealSpeccy question: if the origin is GPL-2.0-only, neither GPL-3 variant works without permission (the fallback would be GPL-2.0-or-later, which in turn would exclude the Apache-2.0 Google Benchmark submodule and OpenSSL 3 static linking).

### 5.5 Full component table (condensed)

| Component | Version | License | Linkage | Verdict |
|-----------|---------|---------|---------|---------|
| UnrealSpeccy-derived core code | 0.36.6-0.38 era | **UNKNOWN in repo** (believed GPL; verify) | own code | **P1 — must resolve** |
| Qt 6 (Gui/Widgets/Multimedia/Network) | 6.5-6.9 | LGPL-3.0 | dynamic | compatible w/ conditions |
| QHexView | 5.0 | MIT | static | compatible |
| zstd | 1.5.7 | BSD-3 / GPL-2 dual | static | compatible (BSD) |
| LZMA SDK | 19.00 | public domain | static | compatible |
| miniaudio | 0.11.21 | PD / MIT-0 | header | compatible |
| blip_buf port | 1.1.0 | LGPL-2.1+ (notice missing) | static | compatible, add notice |
| lodepng | 20200306 | zlib | static | compatible |
| digestpp | — | public domain | header | compatible |
| simpleini | 4.17 | MIT | header | compatible |
| ConvertUTF | 2004 | Unicode legacy (field-of-use) | dead | **INCOMPATIBLE — delete** |
| tinywav | 2022 | ISC | static | compatible |
| simple-fft | 2020 | MIT | header | compatible |
| CLI11 (x2) | 2.5.0 | BSD-3 | header | compatible |
| nlohmann/json | 3.12.0 | MIT | unused | compatible |
| rapidyaml/c4core | 0.9.0/0.2.6 | MIT | test | compatible |
| vcd-writer | — | MIT | unused | compatible |
| libwave | 2020 | MIT | unused | compatible |
| z80ex | 0.16 | GPL-2.0 (no or-later) | dead | **INCOMPATIBLE — delete** |
| message-center | — | none (owner's) | static | add notice |
| gif.h (Tangora) | — | public domain | static | compatible |
| nvEncodeAPI.h | 2024 | MIT (runtime DLL proprietary, dlopen'd) | header/dynamic | compatible |
| MediaFoundation/D3D11/DXGI | OS | proprietary (system lib) | dynamic | compatible |
| VideoToolbox et al. | OS | proprietary (system lib) | dynamic | compatible |
| ffmpeg | user | LGPL/GPL | process | no coupling |
| Drogon | 1.9.11 | MIT | static | compatible |
| Trantor | bundled | BSD-3 | static | compatible |
| wepoll | 2020 | BSD-2 | static (win) | compatible |
| mman-win32 | — | MIT | static (win) | compatible |
| jsoncpp | 1.9.5 | PD / MIT | static | compatible |
| zlib | 1.3.1 | zlib | static | compatible |
| OpenSSL | system | Apache-2.0 (3.x) / OpenSSL (1.x) | dynamic; static on MSVC | compatible only with 3.x or exception |
| Lua | 5.4.7 | MIT | static | compatible |
| sol2 | 3.5.0 | MIT | header | compatible |
| pybind11 | 2.13.6 | BSD-3 | header | compatible |
| CPython | 3.13.1 | PSF-2.0 | static (fetched at configure) | compatible |
| python-cmake-buildsystem | — | Apache-2.0 | unused | compatible (GPL-3 only) |
| cmake-python-build | — | Apache-2.0 | unused | compatible (GPL-3 only) |
| googletest | 1.15.2 | BSD-3 | test | compatible |
| Google Benchmark | 1.9.0 | Apache-2.0 | benchmark | compatible (GPL-3 only) |
| ownShell | 2015 | MIT (AUTHORS missing) | testclient | compatible |
| FastLZ | 0.5.0 | MIT | PoC | compatible |
| ayumi filter code | — | MIT (notice missing) | own tree | compatible, add notice |
| amiga-paula port | — | **unknown** | own tree | needs owner input |
| z80test vectors | — | MIT | test | compatible, add notice |
| FUSE Z80 tests | — | GPL-2.0+ | testdata | compatible, add notice |
| ZEXALL | — | GPL-2.0+ | testdata | compatible, add notice |
| Z80 XCF Flavor | 1.5 | GPL-3.0+ | testdata/docs | compatible |
| z80bltst (Ped) | 2022 | MIT | testdata | compatible |
| Consolas font | — | Microsoft proprietary | data | **INCOMPATIBLE — replace** |
| ROM images (58 + 1) | — | Amstrad permission / none | data | document in ROMS note |
| Game/demo fixtures | — | third-party copyright | testdata | NOTICE or move out |
| ROM Disassembly book, datasheets | — | third-party copyright | docs | mark as not licensed |
