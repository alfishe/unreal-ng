# Recording Library Extraction — Design

**Status:** Draft
**Date:** 2026-07-16
**Branch:** recording
**Related docs:** [Recording System Architecture](../../emulator/design/recording/recording-system.md),
[Encoder Architecture](../../emulator/design/recording/encoders/encoder-architecture.md)

## Summary

Extract the video/audio recording subsystem (`core/src/emulator/recording/**`) out of the
`core` static library into a new, optional static library target `unreal-recording`,
following the same pattern already used for the automation modules
(`core/automation/{cli,lua,python,webapi}`). The `core` library must build and run with
zero recording code compiled in; when the library is present and linked, behavior is
byte-for-byte identical to today.

---

## 1. Goals / Non-goals

### Goals

| # | Goal |
|---|------|
| G1 | `core` compiles, links and runs with **no** recording sources, no Objective-C++ (`OBJCXX`), and no AVFoundation/VideoToolbox/CoreMedia/CoreVideo/AudioToolbox framework linkage. |
| G2 | When `ENABLE_RECORDING=ON` (default), **no behavior change**: same encoders, same feature-flag semantics, same UI. |
| G3 | Recording becomes a build-time option (`ENABLE_RECORDING`) orthogonal to the existing runtime `recording` feature flag in `FeatureManager`. |
| G4 | Apple-specific build machinery (`enable_language(OBJCXX)`, `.mm` sources, framework linkage) is fully contained inside the recording library target. |
| G5 | Same target/option conventions as automation: `option(ENABLE_*)` at root, `unrealng::` alias, conditional linking via generator expressions, `ENABLE_*` compile definitions in consumers. |

### Non-goals

- No plugin/`dlopen` mechanism; the library is a compile-time static library like `automation_lua`.
- No redesign of the encoder architecture (`EncoderBase`, `PlatformEncoder`, FFmpeg pipe, GIF) — files move, they do not change semantically.
- No change to the `FeatureManager` runtime toggle semantics when the library is present.
- No installation/packaging (CMake `install()`) changes beyond what the new target requires.

---

## 2. Current state (what couples recording to core)

### 2.1 Recording sources compiled into `core` today

`core/src/CMakeLists.txt` builds **everything** under `core/src` via
`file(GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)` (line 50), plus a macOS-only
`file(GLOB_RECURSE MM_FILES ${SOURCE_DIR}/*.mm)` (lines 59–65). That sweeps in:

```
core/src/emulator/recording/
├── recordingmanager.{h,cpp}        # RecordingManager — orchestrator, public API
├── encoder_base.h                  # EncoderBase abstract encoder interface
├── encoder_config.h                # EncoderConfig POD (std-only includes)
├── platform_encoder.{h,cpp}       # Native-encoder dispatch (VideoToolbox on macOS)
├── ffmpeg_probe.{h,cpp}           # ffmpeg binary discovery/capability probe
├── realtime_estimator.{h,cpp}     # Realtime-capability estimation
└── encoders/
    ├── gif_encoder.{h,cpp}         # Uses core/src/3rdparty/gif/gif.h
    ├── ffmpeg_pipe_encoder.{h,cpp} # Uses common/process.h + common/named_pipe.h
    └── videotoolbox_encoder.{h,mm} # Objective-C++, Apple frameworks (macOS only)
```

Supporting utilities added on this branch (generic, OS-level, no recording types):

```
core/src/common/named_pipe.{h,cpp}  # Cross-platform named pipe / FIFO
core/src/common/process.{h,cpp}    # Child-process spawn/stdin-pipe wrapper
```

Because of the VideoToolbox encoder, `core/src/CMakeLists.txt` currently does
`enable_language(OBJCXX)` on Apple and links **seven** frameworks `PUBLIC`
(Foundation, CoreFoundation, AVFoundation, CoreMedia, CoreVideo, VideoToolbox,
AudioToolbox). None of these were needed by core before this branch.

### 2.2 Core → recording references (the seams)

| File | Reference | Guarded on null? |
|------|-----------|------------------|
| `core/src/emulator/emulatorcontext.h:20,86` | `class RecordingManager;` forward decl + `RecordingManager* pRecordingManager = nullptr;` | n/a (forward decl only — no header include) |
| `core/src/emulator/cpu/core.h:12,54` | `#include "emulator/recording/recordingmanager.h"` + `RecordingManager* _recordingManager` member | — |
| `core/src/emulator/cpu/core.cpp:192–198` | `new RecordingManager(_context)`; `Init()`; publishes to `_context->pRecordingManager` | construction site |
| `core/src/emulator/cpu/core.cpp:338–342` | teardown: `delete _recordingManager` | yes |
| `core/src/emulator/cpu/core.cpp:443` | `_recordingManager->Reset()` in `Core::Reset()` | **no** (currently assumes always non-null) |
| `core/src/emulator/mainloop.cpp:142–144` | `IsRecording()` / `IsRealtimeCapable()` — skip realtime sync during non-realtime recording | yes |
| `core/src/emulator/mainloop.cpp:343–353` | `CaptureFrame(_context->pScreen->GetFramebufferDescriptor())` | yes |
| `core/src/emulator/sound/soundmanager.cpp:271–273` | `CaptureAudio(_outBuffer, SAMPLES_PER_FRAME * AUDIO_CHANNELS)` before mute | yes |
| `core/src/base/featuremanager.cpp:12,375–378` | `#include "emulator/recording/recordingmanager.h"`; `pRecordingManager->UpdateFeatureCache()` on feature change | yes |
| `core/src/emulator/platform.h:76,217–221` | `MODULE_RECORDING = 12`, `SUBMODULE_RECORDING_*` logging enums | n/a (plain enums) |

Every call site except `Core::Reset()` already null-guards `pRecordingManager` — the
runtime seam largely exists; only construction and the two headers need decoupling.

### 2.3 Consumers outside core

- **unreal-qt UI** (all include `emulator/recording/recordingmanager.h` and use the full
  `RecordingManager` API — `StartRecordingEx`, `SetVideoCodec`, `GetStatistics`,
  `AddAudioTrack`, `SetRecordingMode`, `PauseRecording`, …):
  - `unreal-qt/src/debugger/widgets/videorecordingwidget.{h,cpp}`
  - `unreal-qt/src/debugger/widgets/recordingpresets.{h,cpp}`
  - `unreal-qt/src/debugger/widgets/recordingstatuswidget.{h,cpp}`
  - `unreal-qt/src/debugger/widgets/multitrackdialog.{h,cpp}`
  - `unreal-qt/src/ui/recordingdialog.{h,cpp}`
  - menu entry points in `unreal-qt/src/menumanager.cpp`, `unreal-qt/src/mainwindow.cpp`
- **automation (cli/lua/python/webapi):** currently **no** references to
  `RecordingManager` — nothing to change there.
- **core/tests:** no recording references today.

### 2.4 The automation precedent

The pattern to replicate (from `CMakeLists.txt` root + `core/automation/CMakeLists.txt`):

- Root: `option(ENABLE_AUTOMATION ...)`, `add_subdirectory(core/automation)` inside
  `if (ENABLE_AUTOMATION)`.
- Each module is its own static lib target (`automation_lua`, `automation_webapi`, …)
  that links **against** `unrealng::core` (dependency direction: module → core, never
  core → module).
- Optional-dependency degradation: `core/automation/webapi/CMakeLists.txt` sets
  `set(ENABLE_WEBAPI_AUTOMATION OFF PARENT_SCOPE)` + `return()` when OpenSSL is missing;
  the parent then skips linking a target that was never created.
- Consumers get matching compile definitions
  (`$<$<BOOL:${ENABLE_LUA_AUTOMATION}>:ENABLE_LUA_AUTOMATION>`) to `#ifdef`-guard UI code
  — the comment in `core/automation/CMakeLists.txt:164` even mandates keeping them in
  sync with `unreal-qt/CMakeLists.txt`.

---

## 3. Proposed design

### 3.1 Dependency direction: `unreal-recording` → `core`

```
                 ┌──────────────────────┐
                 │      unreal-qt       │  #ifdef ENABLE_RECORDING around UI
                 └───┬────────────┬─────┘
                     │            │ (only when ENABLE_RECORDING=ON)
                     ▼            ▼
        ┌────────────────┐   ┌───────────────────┐
        │ unrealng::core │◄──┤ unrealng::recording│  links core, PUBLIC Apple frameworks
        └────────────────┘   └───────────────────┘
          core never links        RecordingManager : IRecordingManager
          or includes the         GifEncoder / FfmpegPipeEncoder /
          recording target        VideoToolboxEncoder / PlatformEncoder
```

The recording library **links `unrealng::core`** (exactly like `automation_lua` does),
because it legitimately consumes core types by value/reference:

- `FramebufferDescriptor` (`core/src/emulator/video/screen.h:227`) and `Screen`
- `EmulatorContext`, `Emulator`, `ModuleLogger`, `FeatureManager`
- `PlatformModulesEnum::MODULE_RECORDING` / `SUBMODULE_RECORDING_*` (`platform.h`)
- `core/src/3rdparty/gif/gif.h` (header-only, reached via core's PUBLIC include dirs)

The reverse direction (core → recording) is reduced to **one abstract interface header
and one factory hook**, both living in core and having zero link-time dependency on the
recording library. This is the only sound direction: making core link recording would
reintroduce the coupling we are removing, and inverting the type dependency (recording
not linking core) would require duplicating `FramebufferDescriptor` and the logging
plumbing for no benefit. Static libraries tolerate the "cycle" of
recording-includes-core-headers + core-holds-recording-pointer because the pointer side
is a pure abstract interface.

### 3.2 Target layout

New directory, sibling of `core/automation`:

```
core/recording/
├── CMakeLists.txt                  # defines target 'recording' + alias unrealng::recording
└── src/
    ├── recordingmanager.{h,cpp}    # moved from core/src/emulator/recording/
    ├── recording_factory.cpp       # NEW: implements the core factory hook (§3.4)
    ├── encoder_base.h
    ├── encoder_config.h
    ├── platform_encoder.{h,cpp}
    ├── ffmpeg_probe.{h,cpp}
    ├── realtime_estimator.{h,cpp}
    └── encoders/
        ├── gif_encoder.{h,cpp}
        ├── ffmpeg_pipe_encoder.{h,cpp}
        └── videotoolbox_encoder.{h,mm}
```

**Public headers / include paths:** the target exports
`target_include_directories(recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)` so
consumers write `#include "recordingmanager.h"` (or, to preserve existing UI includes
with minimal churn, keep the subpath `core/recording/src/emulator/recording/…` — see
Migration step 2; flat `src/` is recommended, updating the ~6 UI include lines).

**`named_pipe` / `process` stay in `core/src/common/`.** They are deliberately generic
OS wrappers (child-process spawning, cross-platform FIFOs) with no recording types in
their interfaces. The recording library is currently their only consumer
(`ffmpeg_pipe_encoder.h:15–16`), but they are exactly the kind of utility that
automation, the debugger, or future tooling will want (spawning external analyzers,
piping traces). Core already owns this altitude of code (`core/src/common/`), they add
~2 KB to the core lib, and keeping them there avoids a future re-extraction when a
second consumer appears. The recording lib reaches them through core's existing PUBLIC
include dir `${SOURCE_DIR}/common`.

### 3.3 The decoupling seam: `IRecordingManager` in core

Core keeps a minimal abstract interface — only the operations core itself calls — in a
new header **`core/src/emulator/recording_interface.h`** (std-lib includes only, no
`stdafx.h`, no encoder types):

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

struct FramebufferDescriptor;   // defined in emulator/video/screen.h

/// Minimal capture interface core uses to feed the (optional) recording subsystem.
/// Implemented by RecordingManager in the unreal-recording library.
class IRecordingManager
{
public:
    virtual ~IRecordingManager() = default;

    // Lifecycle (Core::Init / Core::Reset / Core teardown)
    virtual void Init() = 0;
    virtual void Reset() = 0;

    // FeatureManager notification (featuremanager.cpp:375-378)
    virtual void UpdateFeatureCache() = 0;

    // Hot-path queries (mainloop.cpp:142-144)
    virtual bool IsRecording() const = 0;
    virtual bool IsRealtimeCapable() const = 0;

    // Capture entry points (mainloop.cpp:347, soundmanager.cpp:273)
    virtual void CaptureFrame(const FramebufferDescriptor& framebuffer) = 0;
    virtual void CaptureAudio(const int16_t* samples, size_t sampleCount) = 0;
};
```

Changes in core:

- `EmulatorContext::pRecordingManager` becomes `IRecordingManager*` (stays `nullptr` by
  default — `emulatorcontext.cpp:32` already initializes it so). The forward declaration
  at `emulatorcontext.h:20` changes to `class IRecordingManager;`.
- `core/src/emulator/cpu/core.h:12` drops `#include "emulator/recording/recordingmanager.h"`;
  the member becomes `IRecordingManager* _recordingManager`.
- `featuremanager.cpp:12` drops the recordingmanager include; the null-guarded
  `UpdateFeatureCache()` call compiles against the interface.
- `mainloop.cpp` / `soundmanager.cpp` call sites compile unchanged (they already use
  only interface methods through the pointer, with null guards).
- `core.cpp:443` gains the missing null guard: `if (_recordingManager) _recordingManager->Reset();`.

`RecordingManager` in the library becomes
`class RecordingManager : public IRecordingManager { ... }` — its existing method
signatures (`IsRecording`, `CaptureFrame(const FramebufferDescriptor&)`,
`CaptureAudio(const int16_t*, size_t)`, `Init`, `Reset`, `UpdateFeatureCache`) already
match the interface; the inline hot-path getters become `override`s (negligible cost —
they are called ~50×/s and ~1×/frame, not per-instruction).

**UI keeps the full API.** unreal-qt links `unrealng::recording` directly and continues
to include the concrete `recordingmanager.h` (`StartRecordingEx`, `SetRecordingMode`,
`AddAudioTrack`, `GetStatistics`, `EncoderBackend`, …). Where the UI reads
`_context->pRecordingManager` it does
`auto* rm = static_cast<RecordingManager*>(_context->pRecordingManager);` — safe because
the only installer of that pointer is the recording library's own factory.

### 3.4 Construction: factory hook instead of `new RecordingManager`

`Core::Init()` (`cpu/core.cpp:192–198`) currently does `new RecordingManager(_context)`.
Replace with a registration hook declared in `recording_interface.h`:

```cpp
using RecordingManagerFactory = IRecordingManager* (*)(EmulatorContext* context);

namespace RecordingSubsystem
{
    void SetFactory(RecordingManagerFactory factory);  // implemented in core
    RecordingManagerFactory GetFactory();              // returns nullptr if none installed
}
```

- **Core side** (`core/src/emulator/recording_interface.cpp`, ~10 lines): stores the
  function pointer. `Core::Init()` becomes:

  ```cpp
  if (auto factory = RecordingSubsystem::GetFactory())
  {
      _recordingManager = factory(_context);
      _context->pRecordingManager = _recordingManager;
      _recordingManager->Init();
  }
  // else: pRecordingManager stays nullptr; every consumer already null-guards
  ```

- **Library side** (`core/recording/src/recording_factory.cpp`): provides
  `void InstallRecordingSubsystem();` which calls
  `RecordingSubsystem::SetFactory([](EmulatorContext* ctx) -> IRecordingManager* { return new RecordingManager(ctx); });`

- **Application side**: each executable that wants recording calls it once at startup,
  guarded by the compile definition:

  ```cpp
  #ifdef ENABLE_RECORDING
      InstallRecordingSubsystem();
  #endif
  ```

  Call sites: `unreal-qt` `main.cpp`, `core/automation/automation-main.cpp`,
  `testclient` (optional).

Explicit installation is chosen over static self-registration because static libraries
notoriously drop unreferenced translation units containing self-registering globals
(would require `-Wl,--whole-archive` / `-force_load` / `/WHOLEARCHIVE` per platform).
One `#ifdef`'d line per executable is simpler and predictable on all three toolchains
(AppleClang, GCC/MinGW, MSVC).

### 3.5 CMake wiring

**Root `CMakeLists.txt`** — alongside the automation options (line ~24):

```cmake
option(ENABLE_RECORDING "Enable video/audio recording subsystem" ON)
...
if (ENABLE_RECORDING)
    message(STATUS ">>> Building recording library")
    add_subdirectory(core/recording)
endif ()
```

and in the summary block: `message(STATUS "  ENABLE_RECORDING:  ${ENABLE_RECORDING}")`.

**`core/recording/CMakeLists.txt`** (new — mirrors `automation_*` conventions):

```cmake
cmake_minimum_required(VERSION 3.16)
project(recording LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE RECORDING_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

if (APPLE)
    enable_language(OBJCXX)
    file(GLOB_RECURSE RECORDING_MM ${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm)
    list(APPEND RECORDING_SOURCES ${RECORDING_MM})
endif()

add_library(${PROJECT_NAME} STATIC ${RECORDING_SOURCES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Recording depends on core (FramebufferDescriptor, EmulatorContext, ModuleLogger,
# 3rdparty/gif, common/named_pipe, common/process). PUBLIC so consumers inherit
# core's include dirs transitively.
target_link_libraries(${PROJECT_NAME} PUBLIC unrealng::core)

# Apple frameworks contained HERE, PUBLIC so final executables linking this
# static lib inherit them (static libs don't carry link deps in the archive).
if (APPLE)
    foreach(FW Foundation CoreFoundation AVFoundation CoreMedia CoreVideo
               VideoToolbox AudioToolbox)
        find_library(${FW}_LIB ${FW} REQUIRED)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${${FW}_LIB})
    endforeach()
endif()
```

**`core/src/CMakeLists.txt`** — three reversions/changes:

1. Exclude the recording tree from the glob, next to the existing liblzma exclusion
   (line 53) — until the sources physically move, and as a permanent guard afterwards:

   ```cmake
   EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "emulator/recording/" FALSE OUTVAR CPP_FILES)
   ```

2. Delete the `enable_language(OBJCXX)` block (lines 8–11) and the `MM_FILES` glob
   (lines 58–65) — the only `.mm` in core is `videotoolbox_encoder.mm`, which moves.

3. Revert the Apple block (lines 97–115) to its pre-branch form
   (`target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)`): none of the seven
   frameworks is needed by core once recording is out (verified: the only framework
   users under `core/src` are `videotoolbox_encoder.mm` and the header-only
   miniaudio, which loads CoreAudio at runtime via dlopen and needs no link flags).

**`unreal-qt/CMakeLists.txt`** — mirror the automation-definition pattern
(lines 254–262):

```cmake
if (ENABLE_RECORDING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_RECORDING)
    target_link_libraries(${PROJECT_NAME} PRIVATE unrealng::recording)
else()
    # unreal-qt also globs (line 217: file(GLOB_RECURSE CPP_FILES ...)) — exclude the
    # recording UI when the subsystem is off:
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "debugger/widgets/videorecording" FALSE OUTVAR CPP_FILES)
    # …same for recordingpresets, recordingstatuswidget, multitrackdialog, ui/recordingdialog
endif()
```

Cleaner than filename-pattern excludes: move the five recording widget/dialog source
pairs into `unreal-qt/src/recording/` and exclude that one directory. Menu/toolbar
entry points in `menumanager.cpp` / `mainwindow.cpp` get `#ifdef ENABLE_RECORDING`
guards (same style as the existing `ENABLE_LUA_AUTOMATION` guards).

Same conditional-link treatment for `core/automation/CMakeLists.txt`'s standalone
executable branch and `testclient`, if they are expected to record.

### 3.6 Feature-flag interaction

The runtime switch is unchanged: `FeatureManager` registers
`Features::kRecording` (`"recording"`, alias `"rec"`, `featuremanager.cpp:246–248`) and
`RecordingManager::UpdateFeatureCache()` re-reads it on change.

Build-time is orthogonal:

- **ENABLE_RECORDING=ON:** identical to today. Feature defaults per `features.ini`;
  toggling notifies the manager through `IRecordingManager::UpdateFeatureCache()`.
- **ENABLE_RECORDING=OFF (or factory never installed):** `pRecordingManager == nullptr`.
  The feature stays registered (registration is pure string metadata in core and keeps
  `features.ini` parsing, CLI `feature list`, and WebAPI responses stable) but is
  effectively inert: enabling it changes nothing because every capture site guards on
  the null pointer. Recommended polish: extend `FeatureManager::FeatureInfo`
  (`featuremanager.h:90`) with `bool available = true;` and register `kRecording` with
  `available = (RecordingSubsystem::GetFactory() != nullptr)` so `feature list` and the
  WebAPI report `recording: unavailable (not built)` instead of silently no-op'ing.
  This is a small, optional follow-up — not required for correctness.

### 3.7 What stays in core (deliberately)

| Item | Why it stays |
|------|--------------|
| `MODULE_RECORDING` / `SUBMODULE_RECORDING_*` in `platform.h:76,217–221` | ModuleLogger's module registry is a closed core-owned enum (used by `monitoring/manifest.h`, log viewer, WebAPI). A hole in the numbering or a satellite-registered module ID is far more invasive than 6 enum lines. Harmless when recording is absent — the module simply never logs. |
| `Features::kRecording*` constants + registration (`featuremanager.h:29,41,55`) | String metadata only; keeps `features.ini` schema stable across build configurations. |
| `common/named_pipe.*`, `common/process.*` | Generic OS utilities (§3.2). |
| `recording_interface.h` + factory storage (~60 lines) | The seam itself; std-includes only. |
| `mainloop.cpp` / `soundmanager.cpp` capture call sites | Already null-guarded; they are the *ports*, not the implementation. |

---

## 4. Migration plan

| Step | Work | Effort |
|------|------|--------|
| 1 | Add `core/src/emulator/recording_interface.{h,cpp}` (interface + factory storage). Switch `EmulatorContext::pRecordingManager`, `Core::_recordingManager`, `featuremanager.cpp` to `IRecordingManager`; add null guard at `core.cpp:443`; make `RecordingManager` derive from `IRecordingManager`. Build still monolithic — pure refactor, all tests green. | 0.5 day |
| 2 | `git mv core/src/emulator/recording/* core/recording/src/` (and `encoders/`); fix relative includes (`../../../common/process.h` → `common/process.h` via core's include dirs; `3rdparty/gif/gif.h` unchanged). Update the 6 unreal-qt include lines. | 0.5 day |
| 3 | Create `core/recording/CMakeLists.txt` (§3.5); add root `option(ENABLE_RECORDING)` + `add_subdirectory`; strip OBJCXX/frameworks/`.mm` glob from `core/src/CMakeLists.txt` and add the `emulator/recording/` glob exclusion. | 0.5 day |
| 4 | Add `recording_factory.cpp` + `InstallRecordingSubsystem()`; call it (guarded) from `unreal-qt` main, `automation-main.cpp`, `testclient`. | 0.25 day |
| 5 | unreal-qt: move recording UI into `src/recording/`, conditional glob-exclude + `ENABLE_RECORDING` define + `#ifdef` guards in `menumanager.cpp`/`mainwindow.cpp`; link `unrealng::recording`. | 0.5–1 day |
| 6 | CI matrix: add one `-DENABLE_RECORDING=OFF` configuration (Linux is cheapest) to prevent regression of the core-only build; verify macOS packaging (`package_suite_macos`) still signs/links. | 0.5 day |
| 7 | Docs: update `docs/emulator/design/recording/README.md` + `recording-system.md` with the new target layout. | 0.25 day |

Total: ~3 developer-days. Steps 1 and 2–3 are independently landable; each leaves a
green build.

## 5. Risks

| Risk | Mitigation |
|------|-----------|
| **GLOB pitfalls.** `file(GLOB_RECURSE)` in `core/src/CMakeLists.txt:50` doesn't rerun on file add/remove without reconfigure; a stale cache could keep compiling moved files or (worse) compile them **twice** once the new dir exists. | Land the move + the `EXCLUDE_FILES_FROM_DIR_IN_LIST(... "emulator/recording/" ...)` exclusion in the *same* commit; the exclusion also protects against files reappearing there. Note in the PR that a clean reconfigure is required. |
| **Duplicate symbols if both layouts coexist** during a bisect across step 2. | Same-commit rule above; steps 2+3 are one atomic commit. |
| **`emulatorcontext.h` ripple.** It is included ~everywhere; changing the pointer type recompiles the world and any third-party/automation code that dereferenced `pRecordingManager` expecting the concrete type breaks. | Grep confirmed: no automation module touches it; only the 5 unreal-qt UI files do, and they get the `static_cast` (§3.3). One-time full rebuild is acceptable. |
| **Recording→core "circularity"** (recording includes core headers, core holds a recording pointer). | Not a link cycle: core's side is a pure abstract interface + function pointer defined *in core*. Link order for static libs: `unreal-recording` before `core` on the link line (CMake orders this automatically from the target graph since recording links core). |
| **Apple framework linkage loss.** Frameworks move from core (PUBLIC) to recording (PUBLIC); an executable that links core but *not* recording no longer gets them — that is exactly the goal, but any non-recording core code that silently relied on e.g. CoreFoundation would fail to link. | Verified none exists today (only `videotoolbox_encoder.mm` uses them; miniaudio dlopens at runtime). CI on macOS with `ENABLE_RECORDING=OFF` catches regressions. |
| **`Core::Reset()` null deref** (`core.cpp:443` has no guard today). | Fixed in step 1; without the fix, a recording-less build crashes on first reset. |
| **`stdafx.h` PCH.** `recordingmanager.h:9` includes `stdafx.h` from core's source tree; the new target doesn't use core's PCH. | `stdafx.h` resolves through core's PUBLIC include dir `${SOURCE_DIR}/` so it compiles as a plain include; optionally give `unreal-recording` its own `target_precompile_headers`. |
| **Option-name drift** (root vs. `unreal-qt` FORCE-cached options, as bit Python automation before). | Follow the fixed convention from this branch: root `CMakeLists.txt` is the single source of truth; `unreal-qt` must not `set(... FORCE)` `ENABLE_RECORDING`. |

## 6. Alternatives considered

### A. Keep recording in core behind `#ifdef ENABLE_RECORDING`

Wrap `emulator/recording/**` and all call sites in preprocessor guards inside the core
target. Rejected: core would still need conditional `OBJCXX`, conditional framework
linkage and conditional globs — all the CMake complexity stays *inside* core, every
consumer of `core` gets two ABI-incompatible flavors of the same target depending on a
definition, and ifdef'd call sites rot silently (the OFF path is rarely compiled).
The null-pointer seam gives the same zero-cost OFF behavior with both paths always
compiled.

### B. Runtime plugin (`dlopen` / shared library)

Ship recording as `unreal-recording.so/.dylib/.dll` loaded on demand. Rejected: the
project is otherwise fully static (automation, lzma); a plugin adds a C-ABI boundary
(`FramebufferDescriptor`, `EmulatorContext` would need stable ABI or a flattened C
API), packaging/signing complexity on macOS (`codesign --deep` of bundled dylibs,
`@rpath` handling in `macdeployqt`), and MinGW/MSVC dual-toolchain DLL headaches — all
to gain a dynamism nobody asked for. The static-library-plus-factory approach reuses
the proven automation pattern verbatim.

### C. Recording lib with *no* core dependency (core → recording interface headers only, recording self-contained)

Would require recording to define its own framebuffer/audio descriptors and core to
translate at the seam. Rejected: `FramebufferDescriptor` (`screen.h:227`) is already a
plain descriptor struct; duplicating it creates a synchronization hazard for zero
architectural gain, since the extraction goal is "core usable without recording", not
"recording usable without core".
