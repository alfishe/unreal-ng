# HUD Layer — Folder Guide and Detachment Recipe

| Document | Contents |
|----------|----------|
| `design.md` | Why the HUD is an output-space overlay and never framebuffer content; model / presenter split; layout, animation, themes, lifecycle; unreal-qt integration; relationship to other tracks |
| `rendering.md` | Cost contract, the three compositing paths (Qt overlay, SIMD CPU blend, GPU atlas), caches, HiDPI, repaint scheduling, measurement plan |
| `api.md` | Proposed types and interfaces, notifications, the (deferred) automation surface, feature flag |
| `implementation-plan.md` | Phases, tests, risks, open decisions |
| this file | How the code is laid out inside unreal-qt so that it can be detached from the Qt client later, and the exact recipe for doing it |

## 1. Decision recorded here

- r1 implements everything under **`unreal-qt/src/hud/`**. No new library, no HUD code in core.
- The folder is nevertheless built as **two halves with a one-way dependency**, so that the model, layout, animation and blend code can later be lifted into core as an extended presentation component (HUD and CRT next to the screen pipeline, controllable by automation) by *moving files*, not by rewriting them.
- Automation display / clear methods are **not** exposed in r1. They only make sense once the model is hosted by core; the intended shape is kept in `api.md` §6.

## 2. Folder layout

```
unreal-qt/src/hud/
├── core/                      Qt-free half — the part that moves later
│   ├── hudmodel.h/.cpp        HudModel: MessageCenter subscriptions, producer API, snapshot publish
│   ├── hudsnapshot.h          HudElement, HudSnapshot, enums, HudToastRequest
│   ├── hudlayout.h/.cpp       em units, anchors, safe area, letterbox preference, stacking
│   ├── hudanimator.h/.cpp     easing, presets, reduced motion
│   ├── hudtheme.h/.cpp        modern / retro value objects, icon name → glyph mapping
│   ├── hudsurface.h           HudSurface, HudDirtyRegion, HudPlacement, HudTransform (plain structs, own rect type)
│   ├── hudcompositor.h        HudCompositor interface + CpuBlendCompositor (SIMD tiers, scalar fallback)
│   └── blend/                 blend_scalar.cpp, blend_neon.cpp, blend_sse2.cpp, blend_avx2.cpp, dispatch
└── qt/                        Qt half — stays in the client
    ├── hudoverlay.h/.cpp      HudOverlay : QWidget (translucent sibling of DeviceScreen)
    ├── hudoverlayhost.h/.cpp  create / destroy / re-attach on rebind, video mode and viewport changes
    ├── qpaintercompositor.*   HudCompositor over QPainter / QPixmap cache
    └── hudsettings.*          QSettings-backed presentation preferences (theme, reduced motion, anchors)
```

## 3. Dependency rules (enforced from the first commit)

| Rule | How it is enforced |
|------|--------------------|
| `hud/core/` includes **no Qt headers** and no unreal-qt headers | `hud/core/` sources compile in a separate CMake object library `unreal-qt-hud-core` that links only `unrealng::core` (for `MessageCenter`, notifications, `FeatureManager`) and is compiled with `-DQT_NO_KEYWORDS`-free plain C++; a unit-test target links it without Qt at all, so any Qt include fails the test build |
| `hud/core/` has its **own geometry types** (`HudRect`, `HudPoint`, `HudMargins`, integers in device pixels) | `hud/qt/` converts to and from `QRect` at the boundary; `hud/core/` never sees `QRect` |
| `hud/core/` never names `MainWindow`, `DeviceScreen`, `EmulatorBinding` | it receives a `HudSurface` value and an `EmulatorContext*`; nothing else from the app |
| Text measurement and rasterization are behind the `HudCompositor` interface | the Qt compositor measures with `QFontMetrics` and rasterizes with `QPainter`; the CPU compositor receives pre-rasterized glyph bitmaps from whoever hosts it |
| Time is injected | every `hud/core/` function takes `HudClock::time_point now`; no function calls the clock itself, so tests and other hosts control time |
| Feature gating goes through `NC_FEATURE_CHANGED` | the model never touches Qt settings; the only enable switch is the `hud` feature |

The unit tests for `hud/core/` are the guarantee: if they build and pass without Qt on the link line, the half is detachable.

## 4. Recipe: detaching from the Qt client (Phase 4 in `implementation-plan.md`)

1. **Move** `unreal-qt/src/hud/core/` to `core/src/presentation/hud/` (CRT gets `core/src/presentation/crt/` beside it). Update include paths; no source edits are expected.
2. **Feature hook**: in `FeatureManager::onFeatureChanged()` add the direct `_context->pHudModel->UpdateFeatureCache()` call next to the sound / screen / time-travel hooks, and let `HudModel` drop its `NC_FEATURE_CHANGED` subscription. (`NC_FEATURE_CHANGED` itself stays — menus use it.)
3. **Ownership**: `EmulatorContext` gains `pHudModel`, created with the other peripherals; the Qt app's `EmulatorBinding::hudModel()` returns the context's model instead of constructing one.
4. **Wake-up**: replace the host callback with `NC_HUD_CHANGED` posted from `HudModel::publish()`; `HudOverlayHost` subscribes like the status bar does for FDD state (filter by emulator id, marshal to the GUI thread).
5. **Automation**: implement `api.md` §6 — `hud notify` / `hud state` in the CLI processor, `/api/v1/emulator/{id}/hud/...` in a new `hud_api.cpp`, Lua and Python bindings, MCP wrappers. All are thin calls into `HudModel`'s producer / consumer API.
6. **Tests**: move the `hud/core/` unit tests into `core/tests/presentation/`; keep the Qt overlay goldens in unreal-qt.
7. **Docs**: update `docs/features/automation.md` (new "HUD" subsection under each channel), the parity matrix in `docs/inprogress/2026-08-26-automation-gaps/feature-parity.md`, and mark this folder's `api.md` §6 as implemented.

What must **not** change during the move: the `HudSnapshot` shape (other clients may already consume it), the `HudCompositor` interface (the Qt compositor keeps working), and the lifetime rule that an overlay never outlives the framebuffer it was created for.

## 5. What a second client needs

To render the HUD elsewhere (SDL3 player, screen-viewer if ever wanted): link the Qt-free half, implement `HudCompositor` for the target surface (text rasterization + blend or texture draw), supply `HudSurface` and `now`, and react to the changed callback / `NC_HUD_CHANGED`. Nothing in `hud/qt/` is required.
