# Environment variables

Every switch in this project that is set through an environment variable, in one place. Variables are
grouped by the program that reads them: the application itself, the build wrappers, the test program
(`core-tests`), the benchmarks and the verification tools, and packaging / CI. Most of them are for
development only: a normal user of the emulator needs none of them. "Set" below means "present in the
environment"; for most on/off switches the value does not matter (`=1` is the convention), the
exceptions say so.

## How to set a variable

For one command (macOS / Linux, zsh or bash) put it in front of the command; it applies to that command
only:

```bash
UNREAL_WEBAPI_PORT=8190 ./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt
```

For the rest of the terminal session: `export UNREAL_WEBAPI_PORT=8190` (unset with `unset UNREAL_WEBAPI_PORT`).
Windows `cmd`: `set UNREAL_WEBAPI_PORT=8190` and then the command on the next line (PowerShell:
`$env:UNREAL_WEBAPI_PORT = "8190"`).

## Watch out

- **Variables that rewrite files in the repository.** `UNREAL_SNAPSHOT_GOLDEN_UPDATE`,
  `UNREAL_SLOTS_FITTED_UPDATE`, `UNREAL_SLOTS_MATRIX_UPDATE`, `UNREAL_ASM_UPDATE_GOLDEN` and the
  `UNREAL_*_EXPORT` family (`UNREAL_CTPROBE_EXPORT`, `UNREAL_FUSETEST_EXPORT`, `UNREAL_SNOWTEST_EXPORT`,
  `UNREAL_TURBOTEST_EXPORT`, `UNREAL_ZXTIME_EXPORT`) overwrite tracked expected-result files with whatever
  the current code produces. Never leave them exported in a shell; review `git diff` after using one.
- **Variables that turn checks off.** The "print" switches (`UNREALNG_RECORD_CORE_GOLDEN`,
  `UNREALNG_RECORD_FRAME_GOLDEN`, `UNREALNG_RECORD_SPG_GOLDEN`, `UNREALNG_RECORD_TSCONF_EQUIV`,
  `UNREAL_CONTENTION_GOLDEN_PRINT`, `GS_GOLDEN_PRINT`) print new expected values to paste into the source
  and skip the comparison, so those tests pass whatever the code does. The `*_UPDATE` switches also skip
  the comparing tests while they rewrite. A test run with one of them set proves nothing.
- **`UNREAL_TTD_BACKEND=v1`** is an emergency fallback, not a setting: it brings back the previous
  time-travel recorder (v1) for that one application process until v1 is removed. The new engine is the
  default. It has no effect in `core-tests` (see section 3).
- **Two prefixes for the same kind of switch:** `UNREAL_` and `UNREALNG_` (the older one, still used by a
  few test switches). A misspelled or wrongly prefixed name is silently ignored, nothing warns.
- **`UNREAL_SLOTS_...` means two unrelated things:** `UNREAL_SLOTS_DIR` / `UNREAL_SLOTS_BUILD` /
  `UNREAL_SLOTS_TEST` belong to the build wrappers (machine-wide job slots), `UNREAL_SLOTS_FITTED_UPDATE` /
  `UNREAL_SLOTS_MATRIX_UPDATE` to the expansion-slot tests (they rewrite files).
- **Port switches:** the six server ports each have their own variable. To run a second instance beside a
  first one, move all of them (example in [`tools/verification/unreal-asm/README.md`](../../tools/verification/unreal-asm/README.md)).

## 1. The application at run time

Read by the emulator process: `unreal-qt` and every other program built on the core library (the
automation servers live inside it).

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREAL_WEBAPI_PORT` | 1-65535 (8090) | Port of the WebAPI (HTTP/REST) server. Invalid values are ignored. The MCP server's internal client follows it too. | WebAPI server, `core/automation/webapi/src/automation-webapi.cpp:204`; MCP's client `core/automation/mcp/src/webapi-client.cpp:39` |
| `UNREAL_MCP_PORT` | 1-65535 (8092) | Port of the MCP server (for AI agents). | `core/automation/mcp/src/automation-mcp.cpp:111` |
| `UNREAL_CLI_PORT` | 1-65535 (8765, the CLI module's default) | Port of the text command-line (telnet-style) server. | `core/automation/cli/src/automation-cli.cpp:81` |
| `UNREAL_GDB_PORT` | 1-65535 (2000) | Port of the GDB remote debugging server. | `core/automation/gdb/src/automation-gdb.cpp:26` |
| `UNREAL_DEZOG_PORT` | 1-65535 (12000) | Port of the DeZog debugger server (DZRP protocol). Used when the caller does not pass a port. | `core/automation/dezog/src/automation-dezog.cpp:33`, name in `core/automation/dezog/include/automation-dezog.h:47` |
| `UNREAL_DEZOG_TARGET_WAIT_MS` | 1-60000 ms (2000) | How long the DeZog server waits for an emulator instance before a connection fails. Mainly for tests that want a short failure. | `core/automation/dezog/src/automation-dezog.cpp:59` |
| `UNREAL_DEZOG_HISTORY` | `0` = off, anything else = on (on) | Turns off the step-back history that DeZog's reverse debugging uses. | `core/automation/dezog/src/dezogdebugadapter.cpp:60` |
| `UNREAL_ZRCP_PORT` | 1-65535 (10000) | Port of the ZEsarUX-compatible remote control server (ZRCP). Used when the caller does not pass a port. | `core/automation/zesarux/src/automation-zesarux.cpp:33`, name in `core/automation/zesarux/include/automation-zesarux.h:48` |
| `UNREAL_TTD_BACKEND` | `v1` or unset (unset = the engine) | Which time-travel recorder new emulator instances use. Only the exact value `v1` matters: it selects the previous recorder for the whole process, kept as an emergency fallback until v1 is removed (v1 has no black box). Anything else: the engine. | `core/src/emulator/emulator.cpp:2025` (read once at start-up) |
| `UNREAL_AUDIO_DIAG` | set / unset (off) | Temporary diagnostics: prints frame timing lateness and audio buffer fill to stderr about every 5 seconds. | `core/src/emulator/mainloop.cpp:92` |
| `ZXDLSS_THREADS` | number >= 1 (logical cores, at most 8) | Number of threads for the ZX-DLSS GigaScreen de-flicker filter (`mod-tpgw`). | `core/src/emulator/video/zxdlss/mod_tpgw.cpp:78` |
| `UNREAL_MCP_URL` | URL (`http://127.0.0.1:8092/mcp`) | Where the `unreal-mcp-bridge` (stdio-to-HTTP bridge for MCP clients) connects. The `--url` argument wins. | `core/automation/mcp/bridge/src/main.cpp:34` |

Docs: ports in [`.recipe/_common/setup.md`](../../.recipe/_common/setup.md) and
[`core/automation/gdb/README.md`](../../core/automation/gdb/README.md); the bridge in
[`core/automation/mcp/bridge/README.md`](../../core/automation/mcp/bridge/README.md); DeZog history in
[`reverse-debugging.md`](../inprogress/2026-08-27-dezog-integration/reverse-debugging.md); the TTD
backend in [`testdata/ttd/README.md`](../../testdata/ttd/README.md).

## 2. Building and the build / test wrappers

Read by the scripts in `tools/build/` ([`tools/build/README.md`](../../tools/build/README.md)).

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `BUILD_DIR` | a folder (`cmake-build-agent-release` in the repository root) | Build directory to configure and build in. | `tools/build/build.sh:18`, `tools/build/test.sh:12` |
| `UNREAL_JOBS` | number (half the logical cores, at least 1) | Parallel compile jobs (`ninja -j`) and the job count of the parallel test run. | `tools/build/build.sh:20`, `tools/build/test.sh:14` |
| `UNREAL_NO_CONFIGURE` | `1` = skip (configure every time) | Skips the CMake configure step that `build.sh` runs before every build. Only for a tight edit-compile loop on a tree that did not change. | `tools/build/build.sh:25` |
| `UNREAL_NICE` | `0` = normal priority (lowered, `nice 10`) | Runs builds and tests at normal priority. For benchmarks / timing runs on a quiet machine. | `tools/build/slot.py:225` |
| `UNREAL_SLOTS_BUILD`, `UNREAL_SLOTS_TEST`, `UNREAL_SLOTS_<POOL>` | positive number (build 2, test 1, others 1) | How many builds / test runs may run at once machine-wide. Not to be confused with the slot tests' variables (section 4). | `tools/build/slot.py:58` |
| `UNREAL_SLOTS_DIR` | a folder (`$XDG_CACHE_HOME/unreal-ng/slots`, else `~/.cache/unreal-ng/slots`) | Where the lock files of the build / test slots live. Everything sharing a folder shares the limits. | `tools/build/slot.py:50` |
| `TEST_SHARDS` | number or `auto` (`auto` = logical cores) | Number of parallel shards; the script's second argument wins. | `scripts/run-tests-parallel.sh:40` ([`core/tests/README.md`](../../core/tests/README.md)) |

CMake reads a few standard variables while configuring: `VCPKG_ROOT` (Windows dependencies,
`cmake/ToolchainDetection.cmake:268`, `cmake/DependencyCheck.cmake:20`), `QT_INSTALL_PATH`, `QTDIR`,
`Qt6_DIR`, `CMAKE_PREFIX_PATH` (finding Qt, `cmake/QtDiscovery.cmake`, `cmake/WinDeployQt.cmake`),
`OPENSSL_ROOT_DIR` (`core/automation/webapi/CMakeLists.txt:50`), `MACOSX_DEPLOYMENT_TARGET`
(`cmake/MacOSDeploymentTarget.cmake:8`), `CC` and the Visual Studio variables (`VSINSTALLDIR`,
`VCToolsInstallDir`, `VSCMD_ARG_TGT_ARCH`) for compiler detection (`cmake/ToolchainDetection.cmake:137`), and
`DESTDIR` at install time (`cmake/LinuxPackaging.cmake:21`). These have their usual CMake meaning.

## 3. core-tests: switches that change behavior

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREAL_TEST_SCRATCH_DIR` | a folder (`scratch/` in the repository root) | Moves the tests' scratch folder off the project tree. The Linux Docker build sets it (`docker/linux/build.sh:92`) because some file operations fail on a macOS shared folder. | `core/tests/_helpers/testpathhelper.h:277` ([`docker/linux/README.md`](../../docker/linux/README.md)) |
| `UNREAL_TEST_KEEP_SCRATCH` | set / unset (deleted) | Keeps each run's per-process scratch folder at exit, to look at what the tests wrote. | `core/tests/_helpers/testpathhelper.h:56` ([`core/tests/README.md`](../../core/tests/README.md)) |
| `UNREAL_TIMING_SUITES` | set / unset (off) | Turns on the long timing suites (contention probe sweeps, the full 128K timing suite, ctprobe / snowtest machine sweeps). Seconds to minutes. | `core/tests/emulator/video/contentionprobe_test.cpp:247`, `ctprobe_test.cpp:839`, `snowtest_test.cpp:349` |
| `UNREAL_TAPE_SWEEP` | set / unset (off) | Creates the tape loading sweep cases (every tape on 48K and Pentagon). Normally run through `tools/verification/tape/tape-sweep.sh`. | `core/tests/emulator/io/tape/tapeloadingsweep_integration_test.cpp:68` |
| `MOONSOUND_FULL_SWEEP` | set / unset (fast subset) | Runs the full 11-patch MoonSound sweep with long windows instead of the fast subset. | `core/tests/emulator/sound/chips/soundchip_moonsound_test.cpp:1657` |
| `PAIR_LONG_RUN_FRAMES` | number (1000) | Length of the YM2203 pair long-run drift test. | `core/tests/emulator/sound/tsfm/ym2203pair_test.cpp:892` |
| `UNREAL_RZX_FULL` | exactly `1` (off) | Plays the whole RZX Archive recordings (minutes) instead of the first 300 frames. | `core/tests/emulator/rzx/rzxsession_test.cpp:824` |
| `UNREAL_RZX_CORPUS` | a folder of `.rzx` files (test skipped) | Plays every recording in the folder to the end (and the TTD seek check on them). | `core/tests/emulator/rzx/rzxsession_test.cpp:855`, `core/tests/debugger/ttd/timetravelmanager_rzxplayback_test.cpp:294` ([`tools/verification/rzx/README.md`](../../tools/verification/rzx/README.md)) |
| `UNREAL_RZX_FILE`, `UNREAL_RZX_ORACLE`, `UNREAL_RZX_STOP` | `.rzx` path, `.z80` path, frame count (skipped; STOP 0 = whole) | Plays one recording up to a frame and compares the machine with a reference snapshot (finding the first frame where two emulators disagree). | `core/tests/emulator/rzx/rzxsession_test.cpp:960` |
| `UNREAL_SPRINTER_HDD` | path to the raw `sp_hdd_sys.img` (tests skipped) | Sprinter hard-disk boot tests, Flex Navigator and ZX-mode tests with the MAME pack's system disk. | `core/tests/emulator/machines/sprinter/sprinter_boot_test.cpp:962`, `sprinterzxsession.h:153` |
| `UNREAL_SPRINTER_HDD_MEDIA` | path (`sp_hdd_media.img` next to the system disk) | Data disk for drive D in the Flex Navigator test. | `sprinter_boot_test.cpp:1565` |
| `UNREAL_SPRINTER_HDD_VHD` | path to `sp_disk1.vhd` (skipped) | DSS 1.62 boot from a ZXMAK2 VHD image. | `sprinter_boot_test.cpp:984` |
| `UNREAL_SPRINTER_HDD_CHD` | path to `sp_hdd_sys.chd` (skipped) | DSS 1.71 boot straight from the MAME CHD image. | `sprinter_boot_test.cpp:1055` |
| `UNREAL_TTD_DIVERGENCE` | any value (unset) | Diagnosis in the Sprinter DSS session tests: the network kits record with a whole-session write journal, and before the replay checks the first checkpoint where a replay from the start leaves the recording is printed - the CPU, every device and memory region that differ, the first port-journal divergence, sync misses, the media read cursors and the first memory write of the frame before that differs. | `core/tests/emulator/machines/sprinter/sprinterzxsession.h` (`FirstDivergence`), `sprinternetworkkit_test.cpp` |
| `UNREAL_SPRINTER_ZX_BIOS` | path to a BIOS image (the repository's 3.06) | Another BIOS for the Sprinter ZX-mode session tests (exploration). | `core/tests/emulator/machines/sprinter/sprinterzxsession.h:156` |
| `UNREAL_SPRINTER_ZX_FULLSTART` | set / unset (fast start) | Runs the Sprinter's full firmware start in the ZX-mode tests. | `sprinterzxsession.h:173` |
| `UNREAL_SPRINTER_NGS_FLASH` | path to a NeoGS flash image (built-in) | Uses another NeoGS firmware in the ProPlay sound tests (comparison with MAME). | `core/tests/emulator/machines/sprinter/sprintergeneralsound_test.cpp:314` |
| `UNREAL_NEXT_TESTS` | a ZXSpectrumNextTests folder with `release/!Z80N.snx` and `release/!Z80Nc2.snx` (tests skipped) | Real-board acceptance of the Z80N CPU library: runs the two programs (checked on real boards) on a bare host and expects no `ERR` result. Sub-second. | `core/tests/3rdparty/unreal-next-z80/z80nrealboard_test.cpp` |
| `UNREAL_NEXT_TESTS` (also) | the same folder | `NextRealBoard_Test`: the real-board programs (`release/*.snx`) on the whole NEXT machine - NextReg defaults without error cells, NextReg #69 with its ports (border green), Z80N rows without ERR. Skipped without it. | `core/tests/emulator/machines/next/nextrealboard_test.cpp` |
| `UNREAL_CHDMAN` | path to MAME's `chdman` (skipped) | Checks that `chdman` accepts the CHD files we write. | `core/tests/emulator/io/storage/chd/chdwriter_test.cpp:228` ([`chd.md`](../file-formats/disk-images/chd.md)) |
| `COEMU_OUT`, `COEMU_PROGRAM`, `COEMU_MACHINES`, `COEMU_MAX_FRAMES` | out folder, program base path, machine list (`48k`), frames (60000) | The co-emulation runner test; skipped without the first two. Set by `tools/verification/coemu/unreal-ng/run.sh`. | `core/tests/emulator/coemurunner_test.cpp:85` |

Set by the test programs themselves (not switches): `core-tests` pins `TZ=EST5EDT` for the whole process
(`core/tests/main.cpp:56`) so the real-time-clock pictures match on any machine; `unreal-qt-tests` sets
`QT_QPA_PLATFORM=offscreen` (`unreal-qt/tests/qt/qttestmain.cpp:10`). `GTEST_TOTAL_SHARDS` (set by the sharded
run) relaxes a wall-clock budget in `core/tests/common/timehelper_test.cpp:48`.

**`UNREAL_TTD_BACKEND` in core-tests:** the variable has no effect there. `core-tests` and `unreal-qt-tests` record
with the engine; the test files that drive v1 directly are listed in `core/tests/_helpers/ttdv1tests.h`, and a
listener in `core/tests/main.cpp` selects v1 before each of their suites and tests.

## 4. Regenerating expected results (golden files) - rewrite or print

All off by default. "Rewrites" = writes tracked files; "prints" = prints values to paste into the source
and skips the comparison.

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREAL_SNAPSHOT_GOLDEN_UPDATE` | set / unset | **Rewrites** `testdata/loaders/golden/commit-digests.txt` and `save-digests.txt`; the comparing tests skip. Run with `--gtest_filter='SnapshotGolden*Rewrite*'`. | `core/tests/loaders/snapshot/snapshotgolden_test.cpp:145`, `snapshotsavegolden_test.cpp:168` ([`testdata/loaders/golden/README.md`](../../testdata/loaders/golden/README.md)) |
| `UNREAL_SLOTS_FITTED_UPDATE` | set / unset | **Rewrites** each folder's block in `testdata/slots/fitted-devices.txt` (the test then skips). | `core/tests/emulator/slots/slotmanager_test.cpp:217` |
| `UNREAL_SLOTS_MATRIX_UPDATE` | set / unset | **Rewrites** the generated tables in the slot compatibility matrix document. | `core/tests/emulator/slots/slotmatrix_test.cpp:90` ([`compatibility-matrix.md`](../inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md)) |
| `UNREAL_ASM_UPDATE_GOLDEN` | set / unset | **Rewrites** the assembler's expected `dialects/thelink/*.asm` test data. | `core/src/3rdparty/unreal-asm/tests/dialects_test.cpp:296` (`unreal-asm-tests`) |
| `UNREAL_CTPROBE_EXPORT` | set / unset | **Rewrites** `tools/verification/contention/ctprobe/ctprobe.tap/.trd/.sym` from the source. | `core/tests/emulator/video/ctprobe_test.cpp:800` |
| `UNREAL_CTPROBE_EXPORT_DIR` | a folder (the files above) | With `UNREAL_CTPROBE_EXPORT`: writes there instead, to try a change. | `ctprobe_test.cpp:809` |
| `UNREAL_FUSETEST_EXPORT` | set / unset | **Rewrites** `tools/verification/contention/fusetest/fusetest-coemu.*`. | `core/tests/emulator/video/fusetest_test.cpp:342` |
| `UNREAL_SNOWTEST_EXPORT` | set / unset | **Rewrites** `tools/verification/contention/snowtest/snowtest.*`. | `core/tests/emulator/video/snowtest_test.cpp:265` |
| `UNREAL_TURBOTEST_EXPORT` | set / unset | **Rewrites** `tools/verification/contention/turbotest/turbotest.*`. | `core/tests/emulator/memory/scorpion/turbotest_test.cpp:385` |
| `UNREAL_ZXTIME_EXPORT` | set / unset | **Rewrites** `testdata/machines/sprinter/zx-timing/zxtime.*`. | `core/tests/emulator/machines/sprinter/sprinterzxtiming_test.cpp:127` |
| `UNREALNG_RECORD_CORE_GOLDEN` | set / unset | **Prints** the CPU/RAM golden table of every model; no comparison. | `core/tests/emulator/cpu/core_golden_test.cpp:128` |
| `UNREALNG_RECORD_FRAME_GOLDEN` | set / unset | **Prints** the rendered-frame hashes of every classic model; no comparison. | `core/tests/emulator/video/screenzxframes_test.cpp:130` |
| `UNREALNG_RECORD_SPG_GOLDEN` | set / unset | **Prints** the depacked `.spg` block hashes; no comparison. | `core/tests/emulator/machines/tsconf/loaderspg_test.cpp:75` |
| `UNREALNG_RECORD_TSCONF_EQUIV` | set / unset | **Prints** the TS-Conf renderer hash; no comparison. | `core/tests/emulator/machines/tsconf/screentsconf_test.cpp:434` |
| `UNREAL_CONTENTION_GOLDEN_PRINT` | set / unset | **Prints** the per-model timing fingerprint table; no comparison. | `core/tests/emulator/video/contentionregression_test.cpp:283` |
| `GS_GOLDEN_PRINT` | set / unset | **Prints** the General Sound digests; no comparison. | `core/tests/emulator/sound/chips/soundchip_gs_golden_test.cpp:222` |
| `TTD_FIXTURE_OUT` | file path (`./ttd_fixture.ttd`) | Where the TTD dump-format test writes its fixture session. | `core/tests/debugger/ttd/ttddumpformat_test.cpp:413` |
| `UNREALNG_SZX_EXPORT_DIR` | a folder (skipped / not copied) | Saves our `.szx` / `.z80` files there for the libspectrum interoperability check. | `core/tests/loaders/snapshot/szx/loaderszx_test.cpp:304`, `loader_z80_test.cpp:938` ([`tools/verification/szx/README.md`](../../tools/verification/szx/README.md)) |

## 5. Diagnostics and probes inside tests

These only add output (pictures, dumps, traces) or drive manual probes; results are not affected.

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREALNG_DUMP_TSCONF_FRAMES` | set / unset | Saves TS-Conf frames as raw RGBA to the test scratch folder. | `core/tests/emulator/machines/tsconf/tsconf_boot_test.cpp:110`, `loaderspg_test.cpp:265` |
| `UNREAL_FUSETEST_PRINT` | set / unset | Prints the whole fusetest report screen. | `core/tests/emulator/video/fusetest_test.cpp:255` |
| `UNREAL_HALT2INT_PRINT` | set / unset | Prints the HALT2INT result screen. | `core/tests/emulator/video/contentionprobe_test.cpp:558` |
| `TURBOTEST_SCREEN` | set / unset | Prints the turbo test's screen. | `core/tests/emulator/memory/scorpion/turbotest_test.cpp:312` |
| `SNOWTEST_DUMP` | file path | Writes a raw RGBA frame of the snow test there. | `core/tests/emulator/video/snowtest_test.cpp:171` |
| `ZXPOLY_DUMP_PNG` | set / unset | Saves ZX-Poly composed pictures as PNG to scratch. | `core/tests/emulator/zxpoly/zxpolygroup_test.cpp:80` |
| `ZXPOLY_PORT_CHECK` | set / unset | Turns on the ZX-Poly port-read consistency check. | `zxpolygroup_test.cpp:187` |
| `ZXPOLY_M1_TRACE` | set / unset | Turns on the ZX-Poly instruction trace. | `zxpolygroup_test.cpp:270` |
| `UNREAL_MOUSE_DUMP_DIR` | a folder | Saves Sprinter mouse-test pictures as PNG there. | `core/tests/emulator/machines/sprinter/sprinter_boot_test.cpp:1390` |
| `UNREAL_SPRINTER_ZX_SHOTS` | a folder (scratch) | Keeps Sprinter ZX-mode screenshots and video RAM dumps there. | `sprinterzxsession.h:286`, `sprinterzxtiming_test.cpp:343` |
| `UNREAL_SPRINTER_PROPLAY_WAV` | a folder | Saves the ProPlay General Sound capture as WAV there. | `sprintergeneralsound_test.cpp:396` |
| `UNREAL_SPRINTER_NET_TRACE` | set / unset | Prints screens and network exchanges of the Sprinter network kit tests. | `sprinternetworkkit_test.cpp:71` |
| `SPRINTER_BIOS_PROBE_DIR` | folder of BIOS images (skipped) | Manual probe: boots every BIOS image in the folder (minutes each). | `sprinterbiosversions_test.cpp:275` |
| `SPRINTER_BIOS_PROBE_FRAMES` | number (3000) | Frames per image for the probe above. | `sprinterbiosversions_test.cpp:286` |
| `SPRINTER_BIOS_PROBE_OUT` | a folder (scratch) | Keeps the probe's screenshots there. | `sprinterbiosversions_test.cpp:264` |
| `TSFM_DIAG_WAV`, `TSFM_DIAG_FRAMES` | WAV path (skipped), frames (750) | Renders a TurboSound FM tune to a WAV file. | `core/tests/emulator/sound/tsfm/tsfm_render_diag_test.cpp:47` |
| `PROFI3_PROBE_ROM`, `PROFI3_PROBE_KEYS` | ROM path, key list | Disabled Profi v3 probes: another BIOS ROM, keys to tap in the menu. | `core/tests/emulator/profi3_boot_test.cpp:255` |
| `PROFI_PROBE_DISK`, `PROFI_PROBE_KEYS` | disk path, key list | Disabled Profi v5 probes: boot with a disk; keys to tap in the BIOS menu. | `core/tests/emulator/profi_boot_test.cpp:244`, `:419` |
| `FT812_CHART_PNG`, `FT812_CHART_SCALED_PNG` | PNG path | Saves the FT812 line-budget chart pictures. | `unreal-qt/tests/qt/ft812linebudgetview_test.cpp:111` (`unreal-qt-tests`) |

**Profi program runner** (`ProfiBoot_Test.DISABLED_RunProgram`, `core/tests/emulator/profi_boot_test.cpp:440`;
run with `--gtest_also_run_disabled_tests`). Runs a program on a Profi as a user would:

| Variable | Values (default) | What it does |
|---|---|---|
| `PROFI_PROGRAM` | `.tap`/`.tzx`/`.trd`/`.udi`/`.fdi`/`.td0` path, or `none` (skipped) | The program to load; `none` = only the BIOS. |
| `PROFI_MODEL` | `PROFI`, `PROFI3`, or another model (`PROFI`) | The machine. |
| `PROFI_ROM` | ROM path | Another system ROM. |
| `PROFI_HDD` | image path | Hard disk on `ide0.master` (the image is written to: pass a copy). |
| `PROFI_TURBO`, `PROFI_CPM` | `1` | Press the TURBO / CP/M front-panel switch. |
| `PROFI_CONTENTION` | `0` | Turns memory wait states off. |
| `PROFI_BOOT_FRAMES` | number (600) | Frames to wait in the BIOS menu. |
| `PROFI_BOOT_KEYS`, `PROFI_REF_KEYS` | key list (Enter; the reference machine's menu) | Menu keys for a boot disk / for a reference machine. |
| `PROFI_KEYS`, `PROFI_KEYS_AT`, `PROFI_KEYS_EVERY` | key list, frame (0 = at once), frames (0 = once) | Keys to tap after the load, when, and how often. |
| `PROFI_FRAMES` | number (1500) | Frames to run after the load starts. |
| `PROFI_SHOTS`, `PROFI_NAME` | comma list of frames, name (`program`) | Screenshots `scratch/profi/<name>-<frame>.png`. |
| `PROFI_PORTTRACE` | file path | Writes every IN / OUT there. |
| `PROFI_FDCTRACE` | set | Prints floppy controller register changes. |
| `PROFI_BDOSLOG`, `PROFI_BDOSLOG_ENTRY`, `PROFI_BDOSLOG_MATCH` | file path, number (1), hex bytes | Logs CP/M BDOS calls of a `.COM` program. |
| `PROFI_UNTIL_PC`, `PROFI_UNTIL_HIT` | hex address, number (1) | Keeps stepping until PC reaches the address (the N-th time). |
| `PROFI_TRACE` | number of steps | Instruction trace after the frames. |
| `PROFI_WATCH`, `PROFI_WATCH_RECT`, `PROFI_WATCH_PAGES` | set, `col,row,w,h`, hex pages (`6,3A`) | Steps until a hi-res screen area changes, printing PC and registers. |
| `PROFI_DUMP`, `PROFI_DUMP_PAGES`, `PROFI_DUMP_RAM`, `PROFI_DUMP_MEM` | set, file prefix, file, file | Dumps registers / screen pages / all RAM / the 64K view at the end. |
| `UNREAL_NEXT_FIRMWARE` | folder path (`testdata/machines/zxnext/card`) | The SD card folder of the ZX Spectrum Next firmware tests (TBBLUE.FW, `machines/next`, `nextzxos`, ...): another distribution's tree instead of the one in the repository. | `core/tests/emulator/machines/next/nextfirmware_test.cpp` |
| `UNREAL_NEXT_HUNT` | frames (unset: tests skip) | Runs the personality-ROM bring-up hunts (`NextFirmware_Test.PersonalityRomStallHunt` / `PersonalityRomRestartHunt`): frames to run after the firmware's soft reset; they print the registers and ports the ROM uses to stderr. | `core/tests/emulator/machines/next/nextfirmware_test.cpp` |
| `UNREAL_NEXT_DUMP` | folder path | With the stall hunt: writes the ULA screen, the Layer 2 banks and RAM page 7 as raw files there (scratch conversion to PNG). | `core/tests/emulator/machines/next/nextfirmware_test.cpp` |
| `UNREAL_NEXT_SPACE` | frames after the soft reset | With the stall hunt: holds SPACE for 8 frames from there (the NextZXOS welcome screen's "start"). | `core/tests/emulator/machines/next/nextfirmware_test.cpp` |
| `UNREAL_NEX` | path to a `.nex` | `LoaderNexRun_Test`: runs the file on the NEXT machine (`UNREAL_NEX_FRAMES`, default 200; `UNREAL_NEX_OUT` = folder for `frame.rgba`). | `core/tests/loaders/nex/loadernex_test.cpp` |
| `UNREAL_NEX_NR` | `6B=00,15=1C` | `LoaderNexRun_Test`: NextREG writes (hex) before the frame is taken, to look at one layer. | `core/tests/loaders/nex/loadernex_test.cpp` |
| `UNREAL_NEX_SPRITES` | any | `LoaderNexRun_Test`: prints sprite 0-3 attributes, pattern RAM use, the DMA state, the PC and the stack at the end. | `core/tests/loaders/nex/loadernex_test.cpp` |
| `UNREAL_NEXT_COSIM` | folder path (unset: test skips) | `NextCosim_Test`: writes the boot trace in the co-simulation format (`tools/verification/next-cosim/trace-format.md`) to that folder; with `UNREAL_NEXT_FIRMWARE` the card. | `core/tests/emulator/machines/next/nextcosim_test.cpp` |
| `UNREAL_NEXT_COSIM_FRAMES` | frames (1500) | Length of the co-simulation run. | `core/tests/emulator/machines/next/nextcosim_test.cpp` |

## 6. Benchmarks and verification tools

**TTD benchmark matrix** (`core-benchmarks`, `core/benchmarks/debugger/ttd/ttd_matrix_benchmark.cpp`; full
description in [`tools/verification/ttd-bench/README.md`](../../tools/verification/ttd-bench/README.md)):

| Variable | Values (default) | What it does |
|---|---|---|
| `UNREAL_TTD_BENCH_SET` | `ci`, `turbo`, `full` (`ci`) | Which set of configurations to measure. |
| `UNREAL_TTD_BENCH_ENGINE` | `v1`, `all`, or a comma list (`v1`) | Which recorders to measure. |
| `UNREAL_TTD_BENCH_PERIPHERALS` | `none` or a list like `ay+gs512+moon` (the matrix's own) | Peripheral overlay for every configuration. |
| `UNREAL_TTD_BENCH_FRAMES` | number (each workload's own) | Recorded frames. |
| `UNREAL_TTD_BENCH_SEEKS` | number (40 for `ci`, else 200) | Random seek positions. |
| `UNREAL_TTD_BENCH_OVERHEAD` | `0` = skip (on) | The recording-overhead measurement (four extra runs per case). |
| `UNREAL_TTD_BENCH_DIRTY` | `1` = on (off) | Adds the dirty-page sweep. |
| `UNREAL_TTD_BENCH_KEEP_SESSIONS` | a folder (deleted) | Keeps each case's saved `.ttd` session. |
| `UNREAL_TTD_BENCH_JOURNAL_MB` | number of MB (0) | Journal size limit for the run. |
| `UNREAL_TTD_E7_DIR` | a folder (off) | Registers experiment E7 benchmarks for the sessions in it. |
| `UNREAL_TTD_BENCH_SLOW_FRAMES` | number (0) | Prints the N slowest captures to stderr (`core/src/debugger/ttd/bench/ttdbench.cpp:262`). |
| `UNREAL_TTD_BENCH_DEVICE_BYTES` | set | Prints each device's stored bytes to stderr (`ttdbench.cpp:727`). |
| `UNREAL_TTD_ENGINE_SNAPSHOT_INTERVAL` | number (engine default) | Full reference table every N checkpoints, to compare settings (`ttdbench.cpp:673`). |

The last three are in the shared bench code, so they also act in `core-tests`' `ttdbench_test`.

**Scripts and tools:**

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREAL_API_URL` | URL (`http://localhost:8090`) | Emulator the WebAPI pytest suite talks to. | `tools/verification/webapi/src/conftest.py:13` ([README](../../tools/verification/webapi/README.md)) |
| `UNREAL_WEBAPI_URL` | URL (`http://127.0.0.1:8090`) | Emulator the Wireshark capture plugin talks to. | `tools/wireshark/unreal-ng-extcap.py:26` ([README](../../tools/wireshark/README.md)) |
| `UNREAL_ASM_EMULATOR_URL` | URL (default `http://localhost:<--port>`, port 8090) | Emulator the assembler scripts talk to (another host); `--port` is enough on this one. | `tools/verification/unreal-asm/lib/emulator.py` |
| `UNREAL_ASM_ZXASM`, `UNREAL_ASM_SJASMPLUS`, `UNREAL_ASM_PASMO`, `UNREAL_ASM_Z80ASM` | binary paths (names on `PATH`; in `unreal-asm-tests` the tests skip) | External assemblers for the cross-checks, in `tools/verification/unreal-asm/*/*.py` and in `unreal-asm-tests`. | `tools/verification/unreal-asm/checks/crosscheck.py:115`, `core/src/3rdparty/unreal-asm/tests/*_test.cpp` ([README](../../core/src/3rdparty/unreal-asm/README.md)) |
| `UNREAL_TTD_PERIPHERAL_HEADER` | header path (the repository's `ttdserializable.h`) | Where the TTD analyzer reads device ids from, when run outside the tree. | `tools/verification/ttd-analyzer/src/ttd_format.py:216` |
| `UNREAL_BUILD` | build folder (`cmake-build-agent-release`) | Build with `core-tests` for the co-emulation runner. | `tools/verification/coemu/unreal-ng/run.sh:8` |
| `PROGRAM`, `OUT`, `MAX_FRAMES` | program base path (the contention probe), out folder, frames (60000) | Co-emulation harness: what to run, where results go, when to give up. | `tools/verification/coemu/common/common.sh:23` |
| `FUSE_BIN`/`FUSE_DIR`, `MAME_BIN`/`MAME_ROMPATH`, `ZESARUX_BIN`/`ZESARUX_PORT`, `XPECCY_DIR`, `XPECCY_UPSTREAM_DIR`/`XPECCY_ROMS`, `ZXMAK2_DIR`/`ZXMAK2_BIN`, `KOZYNAX_DIR`/`KOZYNAX_BIN`, `SPEC_CHUM_DIR`/`SPEC_CHUM_BIN`, `SKOOLKIT_PYTHON`, `M8XXX_DIR`/`M8XXX_ROMS`, `CHROME_BIN`, `*_BUILD` | paths | Where each co-emulation runner finds the other emulator and builds its harness. | `tools/verification/coemu/*/run.sh` ([coemu README](../../tools/verification/coemu/README.md)) |
| `SPC_*` (`SPC_MODE`, `SPC_OUT`, `SPC_END`, `SPC_ROM`, `SPC_BIOS`, `SPC_FLOP1/2`, `SPC_HARD1/2`, `SPC_WAV`, ...), `ZXK_*`, `MAME_BIN`, `MAME_ROMPATH` | various | Sprinter capture scripts for MAME (reference recordings). | `tools/machines/sprinter/mame-capture/*.sh`, `*.lua` ([README](../../tools/machines/sprinter/mame-capture/README.md)) |
| `TSCONF_RTL_DIR`, `TSU_CYCLES`, `CPU_SIM_PIN_DELAY` | RTL source path; set; number | TS-Conf hardware-description simulator: source folder, sprite cycle print, CPU pin delay. | `tools/machines/tsconf/rtl-sim/build.sh:14`, `harness.cpp:599`, `cpuharness.cpp:933` ([README](../../tools/machines/tsconf/rtl-sim/README.md)) |
| `MCP_URL` | URL (`http://localhost:8092/mcp`) | MCP endpoint for the curl example. | `docs/features/mcp/examples/curl-session.sh:14` |

## 7. Packaging, release and CI

| Variable | Values (default) | What it does | Read by |
|---|---|---|---|
| `UNREAL_PACKAGE_VERSION` | `YYYYMMDD.HHMMSS` (required) | Version of the Linux AppImage; the release workflow sets it and also passes it to CMake. | `tools/package-appimage.sh:31`, `.github/workflows/release.yml:118` ([README](../../README.md)) |
| `CI_MIN_INTERVAL_MINUTES` | minutes (60; 0 = off) | GitHub repository variable: pushes to master build at most once per interval. | `.github/workflows/cmake-ci.yml:44` |
| `BUILD_TYPE`, `QT_VERSION`, `MACOS_DEPLOYMENT_TARGET`, `OPENSSL_VERSION`, `OPENSSL_SHA256`, `PACKAGE_SYMBOLS` | Release, 6.9.3, 12.0, ... | Workflow-level settings of the CI and release builds (edited in the workflow, not set by hand). | `.github/workflows/cmake-ci.yml:26`, `release.yml:77` |
| `REGISTRY`, `IMAGE_NAME` | `ghcr.io`, the repository | Where the Docker build image is pushed. | `.github/workflows/docker-build.yml:10` |

The CI Docker image (`docker/Dockerfile.universal:105`) sets `CC`, `CXX`, `QT_ROOT_DIR`, `QT_INSTALL_PATH`,
`Qt6_DIR`, `CMAKE_PREFIX_PATH` and `APPIMAGE_EXTRACT_AND_RUN` for the builds inside it.

Not listed as switches: operating-system variables read only to find folders or programs (`HOME`,
`USERPROFILE`, `HOMEDRIVE`/`HOMEPATH`, `TMPDIR`, `XDG_CACHE_HOME`, `PATH`, `SystemRoot`), and the throwaway
experiments under `tools/poc/` and `docs/inprogress/*/tools/`.
