# HUD Layer — Implementation Plan

| Rev | Date | Description |
|-----|------|-------------|
| r1 | 2026-09-07 | Phased plan with deliverables, tests and risks. Architecture in `design.md`, rendering in `rendering.md`, interfaces in `api.md`. |

## Phase 0 — Groundwork (small, independent commits)

| Item | Deliverable | Test |
|------|-------------|------|
| New notifications | `NC_SPEED_CHANGED`, `NC_FILE_LOADED`, `NC_RECORDING_STATE` posted from the core with instance-tagged payloads (`api.md` §3) | core tests: each post carries the right emulator id and values |
| DPR plumbing in unreal-qt | `HudSurface` built from `contentFrame` / `DeviceScreen` geometry and `devicePixelRatioF()`; `screenChanged` already wired for refresh rate | manual on Retina + external 1x display |
| Feature flag | `Features::kHud` registered in `FeatureManager` (default on), `[hud]` in `features.ini`, `onFeatureChanged()` posts `NC_FEATURE_CHANGED` (generic, instance-tagged) | core tests: toggling any feature posts the notification with the right id / state; `feature hud off` via CLI works before any UI exists |

## Phase 1 — Model (`unreal-qt/src/hud/`, Qt-free, no rendering)

| Item | Deliverable | Test |
|------|-------------|------|
| `HudModel`, `HudSnapshot`, `HudElement` | `unreal-qt/src/hud/` (no Qt includes) with `MessageCenter` subscriptions gated by the `hud` feature via `NC_FEATURE_CHANGED`, producer API, atomic snapshot publish, changed callback | unit tests: event → element mapping table (`api.md` §3) row by row; TTL expiry on wall clock; priority ordering; dedup counter; queue limits and drop order; feature off leaves no observers (assert via `MessageCenter` observer count) |
| Zero-cost proof | benchmark: `Post` of HUD-relevant events with HUD disabled vs. compiled out | no measurable delta |

At the end of Phase 1 the model is fully unit-tested without a pixel drawn. Tests live in `unreal-qt` (a small test target is added for the Qt-free half; it links core and gtest like `core-tests`).

## Phase 2 — Layout, animation, theme, blend kernels (`unreal-qt/src/hud/`, Qt-free)

| Item | Deliverable | Test |
|------|-------------|------|
| `HudLayout` | em units, anchors, safe area, letterbox preference, stacking | unit tests with synthetic surfaces (720p, 1080p, 4K, DPR 1 / 2, letterboxed and pillarboxed) — placements are deterministic and snapped |
| `HudAnimator` | pure easing functions, enter / exit / pulse presets, reduced motion | unit tests: values at t=0, t=mid, t=end; no state across ticks |
| `HudTheme` | modern and retro value objects; icon name → glyph mapping | golden values |
| CPU blend kernels | scalar reference + NEON + SSE2/AVX2, runtime dispatch, premultiplied "over" | unit tests: every tier bit-identical to scalar on random buffers incl. edge widths; benchmark alongside the unit tests (moves to `core/benchmarks` with the model) |

## Phase 3 — unreal-qt overlay (Path A)

| Item | Deliverable | Test |
|------|-------------|------|
| `HudOverlay` | translucent sibling over `DeviceScreen`, `QPainterCompositor`, raster cache, animation timer armed only while animating | golden-image tests via `QWidget::grab` at DPR 1 and 2 for a fixed snapshot (toast + indicators, modern and retro) |
| Binding | `EmulatorBinding::hudModel()`, rebind on emulator switch, View → HUD = `setFeature("hud")` with the menu check following the model, presentation settings | manual: toggle during run, pause, turbo, full screen; `feature hud off` from the CLI hides the overlay and unchecks the menu; switch emulator |
| Overlay lifecycle | `HudOverlayHost`: destroy + re-create + re-attach on emulator rebind, `NC_VIDEO_MODE_CHANGED` and viewport preset change; resize only updates the surface | unit test with a fake `DeviceScreen`: counts overlay constructions per event (rebind = 1, mode change = 1, viewport = 1, resize = 0); manual: toggle Pentagon overscan with toasts on screen, switch instances from the videowall selection |
| First content | floppy indicators, pause / speed / turbo indicators, disk and file toasts | manual against the status bar (same events, same timing) |
| Dev stats group | emulated FPS, render cadence, present latency, audio ring occupancy | manual; values match the status bar tooltip |

## Phase 4 — Compositing paths B and C, and the move into core (when the presenters exist)

| Item | Deliverable |
|------|-------------|
| Move to core | Relocate the Qt-free half of `unreal-qt/src/hud/` into core as an extended presentation component next to the CRT pass; replace `NC_FEATURE_CHANGED` handling with the direct `UpdateFeatureCache()` hook; add `NC_HUD_CHANGED`; expose `api.md` §6 through CLI / REST / Lua / Python / MCP and update `docs/features/automation.md` and the parity matrix |
| `CpuBlendCompositor` | used by the SDL3 software presenter and by an optional encoder burn-in |
| `RhiCompositor` | atlas + single-draw HUD after the CRT pass; optional backdrop blur |
| Burn-in option | recording manager takes a `HudSnapshot` and composes it at recording resolution, off by default |

## Test strategy summary

- **Model**: pure unit tests, no UI, no timers (inject `now`).
- **Layout / animation**: pure functions, table-driven.
- **Blend kernels**: bit-exact against scalar, all tiers, plus benchmarks.
- **Overlay**: golden images per DPR / theme; a "no repaint when static" test counts `paintEvent` calls over 2 s with a static snapshot (expect 0 after the first).
- **Performance gates**: overlay repaint < 0.3 ms at 4K; no FPS or present-latency change with HUD animating; HUD disabled = no observers.

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Retina blur if any pixmap is created without DPR | every cache entry carries DPR; golden tests at DPR 2 |
| Qt overlay over a future GL widget shows garbage on some platforms | keep Path A as sibling widget (Qt 6 backing-store composition is supported); Path C exists as the primary route once GL presentation lands |
| Toast storms from chatty subsystems (FDD state at every sector) | model maps FDD *state* to an indicator, not toasts; dedup keys and queue limits for the rest |
| Overlay outlives the framebuffer it was created for (stale image rect / cache after mode change or rebind) | overlay is never resized across a framebuffer change: `HudOverlayHost` destroys and re-creates it on rebind, `NC_VIDEO_MODE_CHANGED` and viewport change; unit test counts constructions |
| Wall-clock TTL while the host sleeps (laptop lid) | expiry compares against `steady_clock`, so a suspended host expires everything on wake — acceptable |
| Text measurement differences across platforms shift layout | layout uses measured text from the client text engine; goldens are per platform where fonts differ, or use the bundled Consolas for tests |

## Open decisions to settle before Phase 1

1. ~~Model location~~ Decided: `unreal-qt/src/hud/`, Qt-free half relocatable to core later (Phase 4).
2. Auto-hide idle indicators when they overlap the picture (leaning yes, setting).
3. Toast body markup: plain text in r1 (leaning yes).
4. Shortcut for View → HUD.
