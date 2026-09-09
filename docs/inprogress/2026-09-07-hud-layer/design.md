# HUD Layer — On-Screen Overlay for the Emulator Presentation

| Rev | Date | Description |
|-----|------|-------------|
| r1 | 2026-09-07 | Initial design. Companion documents: `rendering.md` (compositing paths and performance), `api.md` (interfaces, notifications, automation), `implementation-plan.md` (phases, tests, risks). |

## TL;DR

- **The HUD is a presentation-space overlay, never framebuffer content.** The emulator framebuffer already has three consumers besides the window — the recording encoder, the shared-memory / IPC feed for `unreal-screen-viewer` and `unreal-videowall`, and the planned CRT shader pass. Anything painted into the framebuffer would be recorded, shipped to debugging viewers and post-processed with scanlines and bloom. Rendering the HUD at framebuffer resolution and stretching it with the frame is precisely the failure mode this design avoids.
- **Model and rendering are split.** A client-independent `HudModel` (core side, subscribed to `MessageCenter`) owns *what* is shown: persistent indicators and transient messages with priority and time-to-live. Each client renders an immutable `HudSnapshot` with its own presenter — QPainter in `unreal-qt`, a texture pass in a future SDL3 / GPU client, plain text in the telnet TUI. Visibility rules, priorities and lifetimes exist exactly once.
- **Output-space geometry, wall-clock time.** Layout is expressed in fractions of the output height ("em" units), anchored to corners with a safe area, and prefers letterbox bands when they exist. Animations and TTLs run on the host clock, so a toast fades identically at 1x, in turbo mode and while paused.
- **Optional with zero cost when off.** Inactive means: no subscriptions, no timers, no allocations, no work in the frame path — a single predicate per presented frame. Active means: cached rasterization, dirty-driven repaints, SIMD or GPU blending where the presenter composes the final surface, and a plain CPU fallback everywhere else.
- **First visible feature: framed messages in the style of Keynote presenter notes** — a rounded, translucent panel with a title and body, sliding and fading in and out — plus the persistent device indicators the status bar already knows about (floppy activity, recording, pause, speed).

## 1. Goals and non-goals

### Goals

| # | Goal |
|---|------|
| G1 | Show transient messages ("toasts") and persistent indicators over the emulator picture, in the window and in full screen, with animation. |
| G2 | Never alter the framebuffer or any of its consumers (recording, IPC viewers, CRT pass). |
| G3 | Crisp at any output size and device pixel ratio; proportions identical on a 720p handheld and a 4K monitor. |
| G4 | Optional: enable / disable at any moment, persisted; zero measurable cost when disabled. |
| G5 | Minimal cost when enabled: cached rasterization, dirty-driven updates, SIMD or GPU compositing where the presenter owns the surface, fast scalar fallback. |
| G6 | One model shared by all clients (Qt desktop app, future SDL3 player, telnet TUI, automation); rendering per client. |
| G7 | Scriptable: automation (REST / CLI / MCP / Lua / Python) can post messages and toggle the HUD. |

### Non-goals

- Burning the HUD into recordings. If ever needed it is an encoder-side option ("burn-in"), not a change to the frame source.
- Showing the HUD in `unreal-screen-viewer` / `unreal-videowall` frames. Those are diagnostic consumers of the clean frame; they may run their *own* HUD renderer over the same model if wanted.
- Interactive widgets (buttons, menus) in the overlay. The HUD is display-only and transparent to input.
- A general-purpose UI toolkit. Element types are a small closed set (§4).

## 2. Placement decision

The presentation pipeline, made explicit:

```
framebuffer (352x288 / 384x304, RGBA8888, emulator thread)
   │  latch (Screen::CopyPresentedFramebuffer, tear-free, A/V-sync delayed)
   ▼
viewport crop (DisplayViewport)                  ── recording, IPC feed branch off BEFORE here
   ▼
scale to output rect (fixed 352:288 frame, integer or fractional)
   ▼
CRT pass (planned: scanlines, mask, bloom)        ── operates on the picture only
   ▼
HUD pass (this design)                           ── output space, device pixels
   ▼
window / full-screen surface
```

Every alternative was weighed against the consumers:

| Option | Recording | IPC viewers | CRT pass | Crispness | Verdict |
|--------|-----------|-------------|----------|-----------|---------|
| Paint into framebuffer | polluted | polluted | text gets scanlines | 1 framebuffer pixel = 2–8 output pixels, bilinear smear | rejected ("zesarux lesson") |
| Paint into the CRT pass input | clean | clean | text gets scanlines | same as above | rejected |
| **Overlay in output space after CRT** | clean | clean | untouched | native device pixels | **chosen** |

The only argument for framebuffer placement — "visible in screen-viewer / videowall" — is void: those tools want the clean frame, and they can host a renderer over the same model if visibility is ever wanted.

## 3. Architecture

```
                 core (emulator process)                          client (unreal-qt / SDL3 / TUI)
┌──────────────────────────────────────────────┐        ┌───────────────────────────────────────┐
│ MessageCenter ──▶ HudModel                   │        │ HudPresenter (per client)             │
│   NC_FDD_*          ├─ indicators (persistent)│ snap-  │   ├─ HudLayout   (em units → pixels)  │
│   NC_EMULATOR_*     ├─ toasts (TTL, priority) │ shot   │   ├─ HudAnimator (wall-clock easing)  │
│   NC_SYSTEM_RESET   ├─ dev stats (opt-in)     │ ─────▶ │   ├─ HudRasterCache (glyphs, panels)  │
│   turbo / speed     └─ activation state       │        │   └─ HudCompositor (raster/SIMD/GPU)  │
│ automation ─▶ notify / enable / disable      │        │ output: overlay over the picture      │
└──────────────────────────────────────────────┘        └───────────────────────────────────────┘
```

### 3.1 HudModel (client independent, hosted in unreal-qt for now)

- **Location (r1): `unreal-qt/src/hud/`.** The folder is split in two halves: Qt-free C++ (`HudModel`, `HudSnapshot`, `HudLayout`, `HudAnimator`, `HudTheme`, blend kernels) that depends only on core headers (`MessageCenter`, notifications, `FeatureManager`), and the Qt presenter (`HudOverlay`, `HudOverlayHost`, `QPainterCompositor`). Nothing is extracted into a library and nothing is added to core beyond two small notifications. The Qt-free half is written so that it can later move into core as an "extended core component" (HUD and CRT alongside the screen pipeline, controllable by automation) by relocating files, not by rewriting them: no Qt types, no widget references, no `MainWindow` knowledge.
- One model per bound emulator (the Qt app creates it when it adopts an emulator, exactly like `EmulatorBinding`).
- Subscribes to `MessageCenter` **only while enabled**; disabling unsubscribes and clears. No polling anywhere — the same rule the status bar follows for FDD state.
- Holds two collections:
  - **Indicators**: keyed, persistent, stateful (`fdd_a` motor on/off with track, `rec`, `pause`, `speed 2x`, `turbo`). Updated in place; the renderer animates transitions (a motor LED pulses; REC blinks).
  - **Toasts**: transient, ordered by priority then age, each with `ttl`, `created` timestamp, optional progress and icon. Bounded queue (default 4 visible, 16 queued); lower-priority toasts are dropped first when full; identical `dedupKey` coalesces (a burst of "disk written" becomes one toast with a counter).
- Produces a `HudSnapshot`: an immutable value (vector of elements + generation counter). Renderers copy or reference it under a mutex-free handoff (atomic shared pointer swap). The model never calls into UI code.
- Publishes a generation counter and a `changed` callback (r1: a Qt signal emitted from the host, since the model lives in the app) so the presenter wakes only when something changed; it otherwise repaints purely from the animation clock. When the model moves into core this becomes `NC_HUD_CHANGED`.

### 3.2 HudPresenter (client)

- Owns layout, animation, rasterization cache and compositing for one output surface.
- Inputs per frame: the current snapshot, the output rect, the image rect (where the picture actually is inside the output), the device pixel ratio, and `now()` from a monotonic clock.
- Outputs: either paints onto a client surface (Qt overlay widget) or composes into the final pixel buffer / GPU frame (see `rendering.md`).
- Repaints only when (a) the snapshot generation changed, (b) an animation is in flight, or (c) the output geometry changed. A HUD with only static indicators costs nothing per emulated frame.

### 3.3 Source of truth for content

| Content | Source event(s) | Element |
|---------|-----------------|---------|
| Floppy activity, selected drive, track / head / sector | `NC_FDD_STATE_CHANGED`, `NC_FDD_MOTOR_*` | indicator `fdd` (per drive letter) |
| Disk inserted / ejected / saved / pending write / save retargeted | `NC_FDD_DISK_*` | toast, priority normal; retargeted save = priority high with the reason |
| Emulator paused / resumed / stopped | `NC_EMULATOR_STATE_CHANGE` | indicator `pause`, toast on stop |
| Reset | `NC_SYSTEM_RESET` | toast, low priority, short TTL |
| Turbo on / off, speed multiplier | config change (`Core::EnableTurboMode`, `SetSpeedMultiplier`) — a small `NC_SPEED_CHANGED` notification is added | indicator `speed` |
| Recording started / stopped, file path | recording manager events | indicator `rec` (blinking), toast with path on stop |
| Snapshot / tape / disk file loaded | file loader results (main window today; a `NC_FILE_LOADED` notification is added so automation loads show too) | toast with file name |
| Developer stats: emulated FPS, render cadence, present latency (`pVideoPresentLatencyUs`), audio ring occupancy | sampled by the presenter from existing counters, opt-in "dev" indicator group | indicator group `stats` |
| Free-form message from a script | *deferred*: only once the model is hosted by core (§10) | toast with explicit title / body / TTL / priority |

The status bar keeps its own widgets; both consume the same events. Long term the status bar's FDD tooltip and the HUD indicator can share the cached `FDDStateInfo`, but that is not required for r1.

## 4. Element types

| Type | Persistent | Content | Visual (modern preset) |
|------|-----------|---------|------------------------|
| **Toast / framed message** | no (TTL) | title, body (plain or lightly formatted), optional icon, optional progress 0..1, priority | Rounded rectangle, 1.6 em corner radius, translucent dark panel with subtle border and blur where the compositor supports it, white title + secondary body text. This is the Keynote-style "presenter note" panel. |
| **Indicator** | yes | key, state (off / idle / active / alert), short label, optional value ("A:12") | Small pill or glyph, dimmed when idle, lit when active, pulsing on activity. Grouped along one edge. |
| **Stats group** (dev) | yes (opt-in) | table of label/value pairs | Monospace block, fixed width so values do not jitter; same rule as the status bar tooltips. |
| **Banner** (reserved) | until dismissed | one line, e.g. "Recording — 00:42" | Full-width strip along an edge. Not in r1. |

Every element carries: `id`, `kind`, `anchor`, `priority`, `created`, `ttl` (0 = persistent), `enterAnimation`, `exitAnimation`, `state`.

## 5. Coordinate system and layout

- **Unit**: `em = outputRect.height() / 40` in device pixels. All sizes, paddings, radii and font sizes are multiples of em, so a toast is the same fraction of the picture on every display. Font pixel size is rounded to an integer device pixel; panel geometry snaps to device pixels to keep edges crisp.
- **Anchors**: nine positions (three per edge plus centre). Defaults: toasts bottom-centre, indicators top-right, stats top-left, banner top.
- **Safe area**: `1 em` inset from the output rect, plus the platform safe insets in full screen where the OS reports them (notch, rounded corners).
- **Letterbox preference**: the presenter receives both `outputRect` and `imageRect`. If the band between them along an element's anchor edge is at least the element's height, the element is placed in the band (over black) so it never covers the picture. Otherwise it is placed over the picture, inside the safe area. Bands come and go with window aspect; the layout re-evaluates on geometry change, with a slide animation between the two placements.
- **Stacking**: toasts stack away from their anchor edge, newest nearest the edge, `0.5 em` gap; overflow beyond the visible count waits in the queue.
- **Text**: measured and wrapped at `min(24 em, outputRect.width() - 2 em)`; a body longer than four lines is truncated with an ellipsis (the full text remains available to the TUI / automation).

## 6. Animation

- **Clock**: `std::chrono::steady_clock` (host wall clock), never the emulated frame counter — a toast must fade at the same speed in turbo mode and stay animated while the emulator is paused.
- **Curves**: `easeOutCubic` for entering, `easeInCubic` for leaving, linear for progress bars, sine for pulses. All curves are pure functions of `(now - start) / duration`; no per-frame state accumulation, so a dropped tick is invisible.
- **Presets**: enter = fade 0→1 with a `0.75 em` slide from the anchor edge and scale 0.96→1 (Keynote feel), 220 ms; exit = fade + slide back, 180 ms; indicator activity = 550 ms pulse (matches the status bar LED cadence); stack reflow = 160 ms.
- **Tick source**: the presenter asks its host for a repaint at the display refresh cadence *only while an animation is in flight*. When nothing animates there is no timer at all. `DisplayRefreshRate` (already in `unreal-qt`) provides the cadence and caps it at the VRR maximum.
- **Reduced motion**: a setting (and the OS "reduce motion" hint where available) collapses every animation to an instant switch with a 100 ms fade.

## 7. Styles

Two presets; the theme is a value object (`HudTheme`) so clients can add more.

| | modern (default) | retro |
|--|--|--|
| Font | system UI font (Qt default; Consolas is already bundled for the monospace stats block) | ZX ROM 8x8 glyphs drawn in output space at integer scale `max(1, round(outputHeight / 288))` — chunky but always sharp |
| Panels | translucent dark with 1 px hairline border, rounded | opaque black with 1-cell white border, square |
| Colours | white / secondary grey / accent for alerts | ZX palette (bright white on black, red / yellow for alerts) |
| Animation | full preset (§6) | fade only |

The "ugly" look this design avoids is not the pixel font itself; it is a font rasterized at framebuffer resolution and then bilinearly stretched. Both presets render in output space at device pixels.

## 8. Activation and lifecycle

- **Gate: the `hud` feature in `FeatureManager`** (`features.ini` `[hud] state = on`, alias `hud`). It is the single source of truth for whether the HUD exists, exactly like `sound`, `soundhq`, `screenhq` or `timetravel`. Because the model lives in unreal-qt, core cannot call it directly; instead `FeatureManager::onFeatureChanged()` posts a generic `NC_FEATURE_CHANGED` (feature id, enabled) and the HUD folder subscribes to it, reading the initial state from `isEnabled("hud")` at bind. The desktop app's View → HUD (checkable, shortcut TBD) simply calls `setFeature("hud", checked)`; the CLI `feature hud off`, REST and MCP get the same switch for free, and the state persists in `features.ini` with every other feature. (The generic notification is also what will let menus follow CLI-driven feature changes in general.) Presentation preferences that are not about *whether* the HUD runs — theme, reduced motion, dev stats, anchors — stay in the client's settings (`Hud/Theme`, `Hud/ReducedMotion`, ...).
- **Runtime toggle**: the feature turning on creates the model subscriptions and the client creates the presenter; turning off detaches the presenter (final exit animation optional), unsubscribes the model and frees caches. Toggling during full screen is supported.
- **Zero cost when disabled** is a hard requirement enforced by construction: the presenter object does not exist, the model has no observers, the frame path tests one cached boolean (the same pattern as `_feature_sound_enabled`).
- **Emulator switch**: the presenter rebinds to the new emulator's model exactly as the status bar rebinds its FDD cache; toasts from the previous instance are dropped, indicators re-read from the new model.
- **Framebuffer geometry change = overlay re-creation.** Whenever the framebuffer the picture comes from changes size or identity — rebind to a different emulator instance (`adoptEmulator`), a video mode switch inside the same instance (Pentagon overscan 352x288 ↔ 384x304, ATM / Profi / TS-Conf modes, `NC_VIDEO_MODE_CHANGED`), or a viewport preset change — the overlay is **destroyed and re-created**, not resized in place. The image rect, letterbox bands, em unit and raster cache all derive from that geometry, and a stale overlay attached to a detached `DeviceScreen` (which `init()`s a new backing image on every mode change) is exactly the kind of dangling relationship that produced the register-widget crash. Re-creation is cheap (a widget and an empty cache) and makes the lifetime rule trivial to audit: one overlay per (emulator instance, framebuffer geometry).

## 9. Integration in unreal-qt

- `DeviceScreen` stays a pure presenter of the picture. It resizes itself to the 352:288 frame inside `contentFrame` (no layout), so the black bands belong to `contentFrame`, not to the widget.
- A new `HudOverlay : QWidget`, child of `contentFrame`, raised above `DeviceScreen`, `WA_TransparentForMouseEvents`, `WA_NoSystemBackground`, `WA_TranslucentBackground`. It covers `contentFrame` and takes `deviceScreen->geometry()` as the image rect. Keyboard focus never goes to it.
- Full screen: `contentFrame` fills the window; the overlay follows; safe insets are applied.
- HiDPI: the overlay paints at `devicePixelRatioF()`; all cached pixmaps carry the DPR. Today `unreal-qt` never touches DPR anywhere, so this is the first place that does, and it must be right from the start or Retina text over a crisp frame will look blurred.
- Lifecycle: `MainWindow` owns the overlay through a small `HudOverlayHost` that listens to the three geometry sources — `adoptEmulator` / `releaseEmulator`, `NC_VIDEO_MODE_CHANGED` (already handled by `handleVideoModeChanged`, which re-`init()`s `DeviceScreen`), and `handleViewportChanged` — and on each of them does `destroy(); create(); attach(deviceScreen)`. The overlay never outlives the `DeviceScreen` backing image it was created for. Enable / disable uses the same create / destroy pair.
- Repaint rule: `update()` on snapshot change or animation tick only; never coupled to the 50 Hz frame refresh. With a static HUD the overlay does not repaint when the picture does (Qt composes the backing store; the overlay's cached content is reused).
- Future `QRhiWidget` / `QOpenGLWidget` presenter for the CRT pass: transparent sibling widgets over GL widgets are composed through the backing store in Qt 6, so `HudOverlay` keeps working unchanged; the GPU compositing path in `rendering.md` is an optimization, not a prerequisite.

## 10. Other clients

- **SDL3 / GPU player**: same `HudModel`, a presenter that renders panels and text into a texture atlas and draws them as quads after the CRT pass. Layout and animation code is shared C++ (`hud/` library, no Qt dependency).
- **Telnet TUI (CLI automation)**: renders the snapshot as text (indicators as a status line, toasts as a message log). Free.
- **Automation (REST / MCP / Lua / Python)**: *not in r1*. While the model is hosted by unreal-qt there is nothing for the core automation modules to call. Enable / disable is already reachable through the `hud` feature (`feature hud on|off`). Display / clear methods are added only if and when the model moves into core; `api.md` §6 keeps the intended shape so the move does not change it.
- **screen-viewer / videowall**: intentionally no HUD; they could instantiate a presenter over the IPC frame if a use case appears.

## 11. Interaction with other tracks

| Track | Relationship |
|-------|--------------|
| `2026-08-14-av-sync-and-presentation` | The HUD consumes the presented (A/V-synced) frame geometry only; it adds no latency to the picture path because it composes after presentation and repaints independently of frame refresh. Present latency is exposed as a dev stat. |
| CRT shader pass (planned) | HUD is strictly after the pass. When the picture moves to a GPU widget, the overlay remains a sibling; the GPU compositing path can later fold the HUD into the same render pass. |
| Turbo render decimation (`MainLoop`) | Unaffected; the HUD does not depend on emulated frame delivery. Turbo state is shown as an indicator. |
| Status bar / toolbar | Same events, separate views. Both stay. |
| Recording | Clean frames guaranteed; a future encoder-side "burn-in" option would take a `HudSnapshot` and rasterize it at recording resolution. |

## 12. Open questions

1. ~~Model location~~ Decided: `unreal-qt/src/hud/` for everything in r1, Qt-free half kept relocatable into core later as an extended core component (HUD + CRT), at which point the automation surface in `api.md` §6 is exposed.
2. Default visibility of persistent indicators over the picture when no letterbox band exists: always, or only on activity (auto-hide after 2 s idle)? Leaning: auto-hide, with a setting.
3. Whether the toast body supports a tiny markup subset (bold, monospace) or stays plain text. Leaning: plain in r1.
