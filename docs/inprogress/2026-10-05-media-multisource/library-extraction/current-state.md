# Media library extraction: measured starting point

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Measured on** | `master` at `8d29e724` (+ this design branch). Re-measure at kickoff (phase X0): the multi-source work (C0-C9) adds code first |
| **Method** | `wc -l`, include-graph grep, symbol-use grep over `core/src`, `core/automation`, `unreal-qt`, `core/tests`, `core/benchmarks` |

All paths are relative to `core/src` unless stated otherwise.

## 1. Code that moves (the library's starting body)

### 1.1 Media manager and block / optical stack (in scope today)

| Area | Files | Lines | Emulator coupling |
|---|---|---|---|
| `emulator/media/` | 25 | 5 572 | **the coupling hotspot** (§3): `mediamanager.cpp`, `mediacontrol.cpp`, `modelswitch.cpp`, `mediatargets.cpp`, `mediawritegate.h`, `floppyformats.cpp`, `mediaformatregistry.cpp` |
| `emulator/io/storage/` root (`IBlockDevice`, raw, memory, session map, guards, taps, HDD formats) | 12 | 865 | none beyond `stdafx.h`, `filehelper`, `stringhelper` |
| `emulator/io/storage/cd/` | 11 | 2 648 | none (minimp3 implementation lives in a NeoGS file, §4) |
| `emulator/io/storage/chd/` | 14 | 3 842 | none |
| `emulator/io/storage/fat/` | 2 | 505 | none |
| `emulator/io/storage/hostfolder/` | 14 | 2 702 | `folderdiskbuilder.cpp` (TR-DOS formatter needs `EmulatorContext` for one value), `foldertapebuilder.cpp` (tape loaders) |
| **Subtotal** | **78** | **16 134** | |

### 1.2 Floppy and tape containers (move with the library to make it complete)

| Area | Location | Lines | Coupling |
|---|---|---|---|
| `DiskImage` floppy model | `emulator/io/fdc/diskimage.{h,cpp}` | 1 362 | `fdc.h` (`CRCHelper`, `MAX_*`, `FdcDataRate`), `trdos.h`, `dumphelper.h`; no `EmulatorContext` |
| Floppy loaders (TRD, SCL, FDI, UDI, DSK/EDSK, TD0, MGT/IMG, raw PC, HFE, SCP, Hobeta) | `loaders/disk/` | 7 857 | constructor `(EmulatorContext*, path)` on all; **one real use**: `config.trdos_interleave` in `LoaderTRD::format` (`loader_trd.cpp:217`, no null check) |
| Flux / MFM | `emulator/io/fdc/flux/`, `mfm_parser.h` | 1 090 | none |
| TR-DOS structures, read catalog, boot injector | `emulator/io/fdc/trdos.h`, `trdoscatalog.*`, `trdosbootinjector.*` | 604 | none |
| Tape model, catalog | `emulator/io/tape/tapetypes.h`, `tapecatalog.*` | 976 | none |
| Tape loaders and writers (TAP, TZX, registry) | `loaders/tape/` | 2 745 | registry context-free; a legacy `ctx` constructor remains; `loader_tzx.cpp` still includes `emulatorcontext.h` |
| Unicode / code pages | `common/unicodehelper.*` | 321 | none |
| **Subtotal** | | **~14 955** | |

**Total body: ~31 100 lines** (plus the multi-source code from C0-C9, estimated 9 000-12 000 lines with tests, to be measured in X0).

### 1.3 What stays in the emulator

| Code | Lines | Why |
|---|---|---|
| `ModelSwitch` (`modelswitch.*`) | 224 | drives `EmulatorManager` and the emulator lifecycle |
| Slot implementations (6 classes: floppy, tape, IDE unit, ZX-Evo SD, TS-Conf SD, NeoGS SD) | in their peripherals | they are the guest side of the seam |
| Peripherals (WD1793, uPD765, ATA, ATAPI, SD SPI, tape deck playback `tape.cpp` 1 762) | — | emulation |
| TR-DOS runtime analyzer (`debugger/analyzers/trdos/`, 1 637) | — | watches the Z80 and the FDC, not a file system |
| TTD (`TimeTravelManager`) | — | implements the library's journal and history ports |

## 2. Who uses it (consumers outside the scope)

41 non-test files, 59 include edges:

| Area | Files | Edges | Headers used |
|---|---|---|---|
| Port decoders (TS-Conf, ATM3) | 4 | 6 | mediaslot, mediaformatregistry, mediamanager |
| FDC slot | 2 | 3 | mediaslot, mediamanager, medium |
| Tape slot | 2 | 3 | mediaslot, mediamanager, medium |
| IDE / ATA / ATAPI / CD audio | 7 | 8 | mediaslot, mediamanager, medium, iblockdevice, cdimage |
| SD card SPI | 2 | 3 | iblockdevice, rawimage, sessionwritemap |
| NeoGS / sound | 4 | 5 | mediaslot, mediamanager, medium |
| Emulator core (`emulator.cpp`, `mainloop.cpp`, `config.h`, RZX) | 4 | 6 | floppyformats, mediamanager, mediaconfig, modelswitch |
| Debugger / TTD | 3 | 3 | mediareadjournal, mediamanager, cdimage |
| Snapshot loaders (SZX, machine state transfer, launcher) | 3 | 4 | mediamanager, medium, modelswitch |
| Automation: WebAPI, CLI, Lua, Python | 7 | 7 | mediacontrol, modelswitch |
| Automation: MCP | 0 | 0 | talks HTTP; keeps a copy of the verb table (`mcp-slots.h`) |
| Qt UI | 5 | 8 | mediatargets, mediacontrol, mediaformatregistry, modelswitch, mediamanager |
| Benchmarks | 1 | 3 | chdimage, chdwriter, rawimage |

Fan-out of the shared types: `diskimage.h` is included by 23 production and 28 test files,
`tapetypes.h` by 13 and 4. `pMediaManager` is dereferenced 90+ times outside `emulator/media/`
(most in `emulator.cpp` 19, port decoders 18, NeoGS 8, TTD 8).

**Slots:** 6 `IMediaSlot` implementations, 7 `RegisterSlot` call sites, 4 test fakes.

## 3. Coupling to remove (every edge, with its replacement)

| # | Where | Uses | Replaced by (library port, [api-and-integration.md](api-and-integration.md) §3) |
|---|---|---|---|
| K1 | `mediamanager.cpp:671, 717, 1030` | `TimeTravelManager::IsRecording`, `IsReplayActive`, `StopRecording`, `InvalidateSession`, `RecordExternalEvent(DiskWrite)` | `IRecordingGuard` |
| K2 | `mediamanager.cpp:1024, 1110`; `mediacontrol.cpp:106` | `Emulator::IsRunning`, `IsPaused`, `Pause`, `Resume`, `WaitForPauseConfirmation`, `GetId` | `IMachineHost` (running state, pause / resume, machine id) |
| K3 | `mediamanager.cpp:1107-1126` | `MessageCenter::Post(NC_MEDIA_*, MediaSlotPayload)` | `IMediaEventSink` (typed events; the emulator adapter posts the same `NC_MEDIA_*` topics) |
| K4 | `mediamanager.cpp:1073` | `config.frame_duration_us` | `IMachineHost::FrameDurationUs()` |
| K5 | `mediawritegate.h` | `ctx.ttdReplayActive` | `IRecordingGuard::HostWritesAllowed()` |
| K6 | `mediamanager.cpp:97, 722, 1084` | `LOGWARNING` | `ILogSink` (default: stderr; emulator adapter: `Logger`) |
| K7 | `mediaconfig.cpp` | `IniFile` | `IConfigSource` (key / value lookup); the INI adapter stays in the emulator |
| K8 | `mediacontrol.cpp:705-713` | `IdeController::UnitForSlot`, `SetUnitKind` | a slot capability: `IMediaSlot::Reconfigure(kind)` implemented by `IdeUnitSlot` |
| K9 | `mediacontrol.cpp:903` | `config.mem_model == MM_PLUS3` | a slot descriptor field (`floppyController = upd765`) |
| K10 | `mediacontrol.{h,cpp}` | `StateNode`, `StateNodeToJsonText` | the library's neutral `umedia::Doc` tree + JSON writer; an adapter converts to `StateNode` |
| K11 | `mediatargets.cpp:305, 404`; `mediacontrol.cpp:191` | `ctx->pMediaManager` | a `MediaManager&` passed in |
| K12 | `floppyformats.*`, `mediaformatregistry.*`, `folderdiskbuilder.*` | `EmulatorContext*` passed through to the loaders | `FloppyFormatOptions{trdosInterleave, ...}` value |
| K13 | `loaders/disk/*` | `EmulatorContext*` constructor | context-free `Parse(span) / Serialize()`; the old constructors become emulator-side wrappers for one release |
| K14 | `mediamanager.cpp:930-933` | `TapArchiveWriter`, `TzxArchiveWriter` | moved into the library (tape container module) |
| K15 | `diskimage.h:5-7` | `DumpHelper`, `CRCHelper`, `MAX_*`, `FdcDataRate`, `TRDOSDirectoryEntryBase` | `umedia::crc`, `umedia::floppy::limits`, `trdos` types move with the library; `DumpHelper` call replaced by a free `HexDump` in the library |
| K16 | `audiofiledecoder.cpp` | minimp3 implementation compiled in `emulator/sound/chips/neogs/vs10xx.cpp` | the library compiles `MINIMP3_IMPLEMENTATION` itself; NeoGS links the library's symbol |
| K17 | every `.cpp` | `stdafx.h` PCH, global `using std::string/vector/...` | explicit includes and `std::` (a mechanical pass, §6) |
| K18 | 16 + 10 files | `filehelper` (12 functions), `stringhelper` (`ToLower`, `ToUpper`) | `umedia::host::Path*` and `umedia::text::*` in the library's base module; the emulator keeps its own helpers |
| K19 | `modelswitch.*` | `EmulatorManager` | stays in the emulator; uses the library's public API only |

## 4. Third-party code the library needs

| Library | Used by | Today | In the library |
|---|---|---|---|
| zstd 1.5.7 | CHD codec, (S2 delta) | `core/src/3rdparty/zstd`, `zstd::libzstd` PUBLIC | required; `if(NOT TARGET zstd::libzstd) add_subdirectory(...)` |
| LZMA SDK | CHD codec | `3rdparty/liblzma`, target `lzma` | required (CHD module) |
| miniz | CHD codec, CRC | `3rdparty/miniz`, target `miniz` | required (CHD module) |
| digestpp (SHA-1) | CHD | header only | vendored header |
| rapidyaml | folder manifest, compose descriptor | single header + `ryml_impl.cpp` in core | the library compiles its own `ryml_impl.cpp` |
| minimp3 | audio CD folder | implementation in a NeoGS file (K16) | the library owns the implementation |
| dr_flac | audio CD folder | header, implementation in `audiofiledecoder.cpp` | unchanged |

## 5. Tests that move

| Group | Files | Lines | TESTs | Need an `Emulator` |
|---|---|---|---|---|
| Storage (block, CD, CHD, FAT, host folder) | 26 | 4 293 | 115 | 10 (folder disk builder 7, folder tape builder 1, audio folder disc 2) |
| Media (manager, control, registry, targets, formats, …) | 11 | 2 951 | 83 | 37 (mediacontrol 13, mediatargets 7, modelswitch 5, mediamanager 5, floppyformats 2, …) |
| `unicodehelper`, `DiskImage` | 2 | 1 101 | 37 | 0 |
| Floppy / tape loader tests (`core/tests/loaders/...`) | measured in X0 | | | mostly 0 |
| **Total known** | **39** | **8 345** | **235** | **~47** |

About 188 tests are pure units and move as they are. The ~47 that build an emulator stay in
`core-tests` as **integration tests** of the emulator adapter; most of them stop needing the emulator
once K11 / K12 / K13 are fixed (the folder disk builder needs a context only for K12).

## 6. Build facts that shape the plan

| Fact | Consequence |
|---|---|
| `core` is one STATIC library from a `GLOB_RECURSE`; `stdafx.h` is a PCH for every file; 268 of 454 core `.cpp` files never include it themselves | every moved file needs explicit includes and `std::` (K17); a compile of the library **without** a PCH is the check |
| Warnings are directory-scoped `-Wall -Wextra -Werror` | the library sets its own (same flags + `-Wpedantic` like `opl4`) |
| Precedents: `opl4`, `sam2695` (own `project()`, own tests, no core dependency), `eve-emu` (own repo, vendored, `EVE_EMU_DIR` points to an external checkout, own gtest / benchmarks) | the library follows the `eve-emu` pattern: an own CMake project with its own tests and benchmarks, in-tree first, a separate repository later as an option |
| CI runs unit tests on Linux gcc only; Windows / macOS are built, not tested | the library is pure: its test suite is cheap to run on all three (new CI matrix job, §test plan) |
| No `install()` / exported CMake config for core | the library gets one (`unrealng::media`, `find_package(UnrealMedia)`) |
| Lua / Python are embedded in the emulator; no standalone Python module exists | a standalone `umedia` Python module (pybind11) is new |

## 7. Guest file systems today

| File system | Code | Read | Write | Notes |
|---|---|---|---|---|
| TR-DOS | `TrdosCatalog` (read view), `LoaderTRD::format`, `LoaderHobeta::addFile`, `LoaderSCL::addFile`, `TrdosBootInjector`, `FolderDiskBuilder`; CLI and WebAPI each parse the catalog again | ✓ | add only (no delete / rename / move) | **catalog parsing exists 6 times, add-file 3 times** |
| FAT12/16/32 | `FatVolumeReader`, `HostFolderFat`, `FatNameMapper`; multi-source adds graft patching | ✓ | synthesis only (no in-place writer) | in-place write is non-goal NG-8 of multi-source; the library adds it |
| ISO 9660 | container probe only; `Iso9660Reader` / `IsoSynthVolume` come with multi-source C5 | (C5) | synthesis (C5) | |
| Tape (TAP / TZX as a sequence of files) | `TapeCatalogParser`, `FolderTapeBuilder` | ✓ | build from folder | |
| +3DOS / CP/M / AMSDOS | none (an all-`E5` disk from `LoaderDSK` is a valid empty CP/M disk) | — | — | Python `tools/diskinfo` detects CP/M directories heuristically |
| Profi CP/M HDD | none | — | — | needs the Profi BIOS disk parameter block |
| MB-02, D40/D80, +D / DISCiPLE, Opus, iS-DOS, MSX-DOS, Microdrive | none (MGT container exists) | — | — | |
| AmigaDOS, CBM DOS, others | none | — | — | |

Python tools duplicate part of this (`tools/diskconverter`: TRD / SCL / FDI / UDI / TD0 with a TR-DOS
module; `tools/diskinfo`: inspector with heuristic FS detection; `tools/machines/profi/pqdosimage`:
FAT16 reader). The library's CLI and Python module replace them ([extraction-plan.md](extraction-plan.md) X10).
