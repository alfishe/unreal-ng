# HUD Overlay — Source Layout and Detachment Guide

> **Design documents:** [`docs/inprogress/2026-09-07-hud-layer/`](../../../docs/inprogress/2026-09-07-hud-layer/)
> (architecture rationale, rendering paths, API surface, implementation plan)

## Purpose

On-screen overlay for the emulator presentation: transient messages (toasts),
persistent indicators (floppy activity, pause, speed, recording) and optional
developer stats, rendered **in output space after the CRT pass** so the
framebuffer, recording encoder and IPC viewers are never polluted.

## Folder layout

```
unreal-qt/src/hud/
│
├── core/                         ◀ Qt-free half — moves into core later
│   ├── hudsnapshot.h             HudElement, HudSnapshot, enums, HudToastRequest
│   ├── hudmodel.h/.cpp           MessageCenter subscriptions, producer API,
│   │                             atomic snapshot publish, expire()
│   ├── hudlayout.h/.cpp          em units → device-pixel placement, anchors,
│   │                             safe area, letterbox preference, stacking
│   ├── hudanimator.h/.cpp        pure easing functions, enter / exit / pulse
│   │                             presets, reduced-motion collapse
│   ├── hudpresenter.h/.cpp       composition of model + layout + animator +
│   │                             compositor interface; tick / needsRepaint / paint
│   ├── hudtheme.h/.cpp           modern / retro value objects, icon → glyph map
│   ├── hudsurface.h              HudSurface  { HudRect outputRect, imageRect;
│   │                                           int dpr; HudMargins safeInsets; }
│   │                             HudRect, HudPoint, HudMargins (own types,
│   │                             integers in device pixels — never QRect)
│   │                             HudDirtyRegion, HudPlacement, HudTransform
│   ├── hudcompositor.h           HudCompositor interface (rasterize text,
│   │                             measure text, blend element)
│   │                             + CpuBlendCompositor (SIMD tiers + scalar)
│   └── blend/
│       ├── blenddispatch.h/.cpp  runtime ISA detection, function-pointer table
│       ├── blendscalar.cpp       portable reference kernel
│       ├── blendneon.cpp         Apple Silicon / ARM Linux / Windows on ARM
│       ├── blendsse2.cpp         x86-64 baseline
│       └── blendavx2.cpp         x86-64 fast path (Haswell+)
│
├── qt/                           ◀ Qt half — stays in unreal-qt
│   ├── hudoverlay.h/.cpp         HudOverlay : QWidget, translucent sibling of
│   │                             DeviceScreen, WA_TransparentForMouseEvents
│   ├── hudoverlayhost.h/.cpp     create / destroy / re-attach overlay on:
│   │                             emulator rebind, NC_VIDEO_MODE_CHANGED,
│   │                             viewport preset change, feature toggle
│   ├── qpaintercompositor.h/.cpp HudCompositor implemented over QPainter /
│   │                             QPixmap cache / QFontMetrics
│   └── hudsettings.h/.cpp        QSettings-backed presentation preferences
│                                 (theme, reduced motion, dev stats, anchors)
│
└── README.md                     ◀ this file
```

## Dependency rules

These are enforced **from the first commit** and verified by the build:

| Rule | Enforcement |
|------|-------------|
| **`core/` includes no Qt headers and no `unreal-qt` headers** | Compiled as a separate CMake object library (`unreal-hud-core`) that links **only** `unrealng::core` (`MessageCenter`, notifications, `FeatureManager`). A dedicated unit-test binary links it without Qt at all — any Qt `#include` fails the test build. |
| **`core/` has its own geometry types** | `HudRect`, `HudPoint`, `HudMargins` (integers in device pixels) are defined in `hudsurface.h`. The `qt/` layer converts to/from `QRect`, `QPoint`, `QMargins` at the boundary. `core/` never sees Qt geometry types. |
| **`core/` never names `MainWindow`, `DeviceScreen`, or `EmulatorBinding`** | It receives an `EmulatorContext*` (core type) and a `HudSurface` value. Nothing else from the application layer. |
| **Text measurement and rasterization are behind `HudCompositor`** | The Qt compositor measures with `QFontMetrics` and rasterizes with `QPainter`. The CPU compositor receives pre-rasterized glyph bitmaps. `core/` never calls a text API directly. |
| **Time is injected** | Every `core/` function that needs time takes `HudClock::time_point now`. No function calls the clock itself, so tests and other hosts control time completely. |
| **Feature gating uses `NC_FEATURE_CHANGED`** | The model reacts to the `hud` feature via the MessageCenter notification. It never reads Qt settings. The only enable switch is `FeatureManager::setFeature("hud", …)`. |

**The guarantee:** if the `core/` unit tests build and pass without Qt on the
link line, the half is detachable.

---

## Detaching from the Qt client

> Phase 4 in the [implementation plan](../../../docs/inprogress/2026-09-07-hud-layer/implementation-plan.md).
> This is the complete recipe for moving the Qt-free half into the emulator core.

### Prerequisites

- Phases 1–3 complete (model, layout, animation, blend kernels, Qt overlay all
  working and tested).
- The `core/` unit tests pass without Qt linked.

### Steps

1. **Move files**

   ```
   unreal-qt/src/hud/core/  →  core/src/presentation/hud/
   ```

   The CRT pass (when it exists) goes beside it as `core/src/presentation/crt/`.
   Update `#include` paths in the moved files and their consumers. **No source
   edits are expected** — the code has no Qt or `unreal-qt` dependencies by
   construction.

2. **Update CMake**

   - Remove `unreal-hud-core` object library from `unreal-qt/CMakeLists.txt`.
   - Add the moved sources to `core/CMakeLists.txt` (or a
     `core/src/presentation/CMakeLists.txt` sub-directory).
   - The `qt/` half in `unreal-qt` links against core as before.

3. **Feature hook**

   In `FeatureManager::onFeatureChanged()` (`core/src/base/featuremanager.cpp`),
   add the direct `UpdateFeatureCache()` call:

   ```cpp
   // Update HUD model feature cache
   if (_context && _context->pHudModel)
   {
       _context->pHudModel->UpdateFeatureCache();
   }
   ```

   This goes next to the existing Sound / Screen / TTD / PortDecoder hooks.
   `HudModel` then drops its `NC_FEATURE_CHANGED` subscription (the notification
   itself stays — menus and other app-side consumers still use it).

4. **Ownership**

   `EmulatorContext` (`core/src/emulator/emulatorcontext.h`)
   gains a `HudModel* pHudModel` member, created alongside the other peripherals
   in the emulator constructor. The Qt app's `EmulatorBinding::hudModel()` returns
   the context's model instead of constructing one.

5. **Wake-up mechanism**

   Replace the host callback (r1: a Qt signal emitted from the binding) with
   `NC_HUD_CHANGED` posted from `HudModel::publish()`:

   ```cpp
   // In HudModel::publish() — after atomic snapshot swap:
   messageCenter.Post(NC_HUD_CHANGED,
       new HudChangedPayload(_context->emulatorId, _generation));
   ```

   `HudOverlayHost` subscribes to `NC_HUD_CHANGED` filtered by emulator ID and
   marshals to the GUI thread via `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`,
   the same pattern the status bar uses for `NC_FDD_STATE_CHANGED`.

6. **Automation surface**

   Implement the deferred API from
   `api.md` §6 (`docs/inprogress/2026-09-07-hud-layer/api.md`):

   | Channel | Entry point |
   |---------|-------------|
   | CLI (telnet) | `hud notify "…" "…" [ttl]`, `hud state` in the command processor |
   | REST (WebAPI) | `POST /api/v1/emulators/{id}/hud/notify`, `GET .../hud/state`, `DELETE .../hud/toasts/{id}` in a new `hud_api.cpp` |
   | Lua / Python | `hud.notify(title, body, ttl)`, `hud.indicator(key, state, value)` |
   | MCP | `hud_notify`, `hud_state` tools (thin wrappers over REST) |

   All are thin calls into `HudModel`'s producer / consumer API. Enable/disable
   is already available via `feature hud on|off` on every channel.

7. **Move tests**

   Move the `core/` unit tests into `core/tests/presentation/hud/`. Keep the Qt
   overlay golden-image tests in `unreal-qt`.

8. **Update documentation**

   - `docs/features/automation.md` — add "HUD" subsection under each channel.
   - `docs/inprogress/2026-08-26-automation-gaps/feature-parity.md` — update the
     parity matrix.
   - `docs/inprogress/2026-09-07-hud-layer/api.md` §6 — mark as implemented.

### Invariants that must not change during the move

| Invariant | Reason |
|-----------|--------|
| `HudSnapshot` shape (fields, sort order) | Other clients may already consume it |
| `HudCompositor` interface | The Qt compositor (`QPainterCompositor`) keeps working unchanged |
| Overlay lifetime rule: never outlives the framebuffer it was created for | `HudOverlayHost` destroy/recreate cycle depends on this |
| `HudClock` = `std::chrono::steady_clock` | Timestamps in the snapshot use this; consumers evaluate expiry against the same clock |

---

## Adding a second client (SDL3 player, screen-viewer, etc.)

A non-Qt client needs only the `core/` half. The recipe:

1. **Link** `core/src/presentation/hud/` (or the `unreal-hud-core` object library
   before the move).

2. **Implement `HudCompositor`** for the target surface:
   - Text measurement (font metrics for the client's font engine).
   - Text rasterization (draw strings into premultiplied ARGB bitmaps).
   - Element blending (the CPU blend kernels are reusable; a GPU client draws
     textured quads instead).

3. **Supply `HudSurface`** on every geometry change:
   ```cpp
   HudSurface surface;
   surface.outputRect = { 0, 0, windowWidthPx, windowHeightPx };
   surface.imageRect  = { bandLeft, bandTop, pictureW, pictureH };
   surface.dpr        = displayScale;
   surface.safeInsets  = { top, right, bottom, left };
   presenter.setSurface(surface);
   ```

4. **Drive the tick loop**:
   ```cpp
   if (presenter.needsRepaint(now))
   {
       auto dirty = presenter.tick(now);
       presenter.paint(target, dirty);
   }
   ```
   Arm a timer at the display refresh rate only while
   `presenter.hasActiveAnimations()` returns true.

5. **React to model changes**: subscribe to `NC_HUD_CHANGED` (after the core
   move) or poll the generation counter, and call `presenter.tick()`.

Nothing in `qt/` is required. The `core/` half is the complete model + layout +
animation + compositor-interface stack.
