# unreal-ng — native macOS front-end (POC)

A Qt-free proof of concept: SwiftUI app shell, Metal renderer, CoreAudio output, and a
thin Objective-C++ bridge to the existing C++ emulator core.

Nothing here depends on Qt. The Qt POC (`tools/poc/qt-gui`) was used only as a reference
for *how to talk to the core* (notification topics, framebuffer descriptor, audio pacing).

---

## Build

Swift **requires the Ninja or Xcode generator** — CMake's Makefile generator cannot build
Swift targets, so plain `cmake -B build` fails with an explicit error message.

```sh
cd tools/poc/macos-native
cmake -G Ninja -B build
cmake --build build -j8
open build/unreal-ng-macos.app
```

Requirements: macOS 13+, Xcode command line tools (Swift 5.9+), Ninja, CMake 3.26+.

The core is pulled in with `add_subdirectory(<root>/core/src)`, so editing core sources
rebuilds them as part of this project — no stale `libcore.a`.

### ENABLE_RECORDING

`ENABLE_RECORDING` changes the layout of `EmulatorContext` (it adds `pRecordingManager`).
If the app and `libcore.a` disagree, every member after that point is read at the wrong
offset and the process crashes. This project therefore does **not** define the macro by
hand — it sets the CMake option `ENABLE_RECORDING` (default `ON`, same as the repo root)
*before* adding core, adds `core/recording`, and lets core export the definition `PUBLIC`
through `unrealng::core`. Both sides then agree by construction.

---

## Architecture

```
 SwiftUI  App.swift ─────────── menus (Open / Machine: Start,Stop,Reset / View: zoom), drag&drop
          ContentView
             │
             ├─ MetalScreenView (NSViewRepresentable)
             │     └─ EmulatorMetalView : MTKView, MTKViewDelegate
             │          • MTLTexture upload from the core framebuffer (RGBA8)
             │          • one textured quad, nearest sampling, letterbox/pillarbox
             │          • keyDown / keyUp / flagsChanged capture
             │
             └─ EmulatorController (ObservableObject, UNEmulatorBridgeDelegate)
                   • transport, file loading, key state, published UI state
                       │
   ─────────── bridging header (pure Objective-C) ───────────
                       │
        UNEmulatorBridge (.h Objective-C / .mm Objective-C++)   ← only file that sees C++
             • EmulatorManager::CreateEmulator / StartEmulatorAsync / Stop / Remove
             • MessageCenter observers: NC_VIDEO_FRAME_REFRESH,
               NC_VIDEO_RESOLUTION_CHANGED, NC_EMULATOR_STATE_CHANGE
               (filtered by emulator UUID, marshalled to the main thread)
             • KeyboardEvent → MC_KEY_PRESSED / MC_KEY_RELEASED
             • FramebufferDescriptor access under a mutex
        UNAudioOutput (.h Objective-C / .mm Objective-C++)
             • AudioUnit (kAudioUnitSubType_DefaultOutput), 44100 Hz stereo
             • lock-free SPSC ring buffer, int16 → float32 in the render callback
             • posts NC_AUDIO_BUFFER_HALF_FULL, which is what paces the core's MainLoop
```

**The bridge header rule:** `UNEmulatorBridge.h`, `UNAudioOutput.h` and `UNZXKeys.h`
contain plain Objective-C only, because Swift's importer cannot parse C++. Every include
of a core header lives in the `.mm` files.

### Rendering

`CAMetalLayer` via `MTKView`, in on-demand mode (`isPaused = true`,
`enableSetNeedsDisplay = true`): the core's frame notification sets `needsDisplay`, so
presentation follows the emulator rather than a free-running timer. The quad is generated
from `vertex_id`; aspect correction is a `float2` scale uniform, so the picture is
letterboxed/pillarboxed inside any window shape. The sampler is `filter::nearest`, so
pixels stay crisp at any zoom. Shaders are compiled at runtime from a source string —
no `.metallib` build step.

Redraw is driven by `MTKViewDelegate.draw(in:)`. Overriding `MTKView.draw()` directly
was tried first and raced AppKit's own display pass ("Each CAMetalLayerDrawable can only
be presented once").

### Keyboard

`ZXKeyboardMap` maps macOS virtual key codes (`NSEvent.keyCode`, layout independent) to
the ZX matrix. Ported from the Qt POC's `KeyboardManager` table, with two deliberate
differences:

1. **Command is not mapped.** Qt maps `Qt::Key_Control` — which *is* Command on macOS —
   to SYM SHIFT. On AppKit every Command chord is routed to the menu bar as a key
   equivalent and the matching key-up is never delivered to the view, so SYM SHIFT stays
   latched and the machine looks dead. Control and Option carry SYM SHIFT instead.
2. Punctuation is mapped by physical key position, since `NSEvent.keyCode` is layout
   independent.

Every held key is tracked in the bridge (`std::set<uint8_t>`, which also swallows auto
repeat), and `releaseAllKeys()` is called on window resign-key, app resign-active,
miniaturise, first-responder loss, before the Open panel, and before load/reset/stop.
Caps Lock is deliberately unmapped — macOS reports it as a latching state.

### Audio

The core produces interleaved int16 stereo at 44100 Hz (`AUDIO_SAMPLING_RATE`). The
emulator's audio callback writes into a lock-free ring (8 frames of slack); the CoreAudio
render thread drains it and converts to float32. When the ring falls below half full the
render thread posts `NC_AUDIO_BUFFER_HALF_FULL` — the core's `MainLoop` blocks on that
notification, so **audio is what paces emulation**. Underruns are filled with silence,
overruns drop the oldest samples rather than block the emulator thread.

---

## What works

- Boots straight into the machine on launch (Pentagon 512K config, verified rendering the
  128 BASIC menu).
- Metal rendering of the live framebuffer, aspect preserved, nearest-neighbour, resizes
  with the window; follows `NC_VIDEO_RESOLUTION_CHANGED` (texture is reallocated).
- Keyboard into the ZX matrix, with the stuck-modifier defence described above.
- Audio playback and audio-driven frame pacing.
- Drag & drop of `.sna .z80 .tap .tzx .trd .scl` (also `.fdi .td0 .udi`) onto the window,
  auto-starting the machine; **File ▸ Open…** (⌘O) does the same.
- **Machine** menu: Start (⌘R), Stop (⌘.), Reset (⇧⌘R).
- **View** menu: integer zoom 1x–4x (⌘1–⌘4) sized on the content rect.
- Proper `.app` bundle; `unreal.ini` (from `data/configs/pentagon512k`) and `data/rom`
  are copied into `Contents/Resources`, where `FileHelper::GetResourcesPath()` finds them.

## What is stubbed / missing

- **No pause UI.** `pause`/`resume` exist on the bridge but are not wired to a menu item.
- **No machine model picker** — the bundled `unreal.ini` decides; no settings UI at all.
- **No status bar / FPS / tape / disk indicators**, no toolbar, no fullscreen handling
  (the plain macOS green-button fullscreen works, but there is no custom transition
  logic like the Qt POC's `FullscreenHelper`).
- **No app icon**, no code signing, no recording/RZX UI (recording is only linked in for
  ABI compatibility).
- **Single window only** — the bridge supports one emulator instance; `WindowGroup` would
  happily open more and they would all share it.
- Drag & drop accepts the first item only; multi-file drops are ignored.
- Tape/disk auto-run relies entirely on the core's loaders; no "press any key" automation.

## Known rough edges

- The link step prints `ld: warning: ignoring duplicate libraries: '-lc++'` — swiftc and
  CMake's implicit C++ link line both add it. Harmless.
- CMake's Swift support drops a `main.d` dependency file inside `Contents/MacOS`.
  Cosmetic; a release script should strip it.
- On startup the core logs `Config::LoadConfig - File '…/Contents/MacOS/unreal.ini' does
  not exist` before falling back to `Contents/Resources`. That is core behaviour, not a
  failure.

## API notes / mismatches worked around

- `Emulator::GetUUID()` returns a `unreal::UUID`, **not** a `std::string` — it converts
  implicitly through `operator std::string()`. Notification payloads
  (`EmulatorFramePayload`, `VideoResolutionPayload`) carry `unreal::UUID` too, so
  filtering compares UUIDs, while `KeyboardEvent` wants a `std::string` target id.
- `EmulatorManager` calls take `Emulator::GetId()`; message filtering uses `GetUUID()`.
  They are different strings — mixing them silently does nothing.
- `MessageCenter::AddObserver` needs an `Observer` subclass and a pointer-to-member.
  Objective-C objects cannot be `Observer`s, so the bridge holds a small C++ `CoreObserver`
  member that forwards into Objective-C.
- The ZX key enum is duplicated in `UNZXKeys.h` (Swift cannot see C++ headers).
  `UNEmulatorBridge.mm` carries `static_assert`s against `ZXKeysEnum`, so drift is a
  compile error rather than a wrong keypress. Note core orders `ZXKEY_I` (0x48) before
  `ZXKEY_H` (0x49) — the mirror keeps that quirk.
- Enum case names are `UNZXKeyLetterA` / `UNZXKeyDigit0` rather than `UNZXKeyA` / `UNZXKey0`:
  the Swift importer refuses to synthesise single-character case names from the former.
- Objective-C selectors that Swift would rename unpredictably (`loadFile:`, `pressKey:`,
  `releaseKey:`, `accessFramebuffer:`) are pinned with `NS_SWIFT_NAME`.
