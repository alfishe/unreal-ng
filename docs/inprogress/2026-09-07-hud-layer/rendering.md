# HUD Layer — Rendering and Performance

| Rev | Date | Description |
|-----|------|-------------|
| r1 | 2026-09-07 | Compositing paths, caching, HiDPI, budgets and the zero-cost-when-inactive contract. Architecture in `design.md`. |

## 1. Contract

| State | Cost |
|-------|------|
| HUD disabled | One boolean test per presented frame. No object, no subscription, no timer, no allocation. |
| HUD enabled, nothing animating, snapshot unchanged | Zero work per emulated frame. The overlay's cached content is reused by the window compositor. |
| HUD enabled, animation in flight | One overlay repaint per display refresh (capped at the VRR maximum), touching only the dirty rectangles of the animated elements. Target: < 0.3 ms per repaint at 4K on the CPU path. |
| Snapshot changed | Re-layout of the affected elements, re-rasterization only of elements whose text or size changed (cache keyed by content + em size + DPR + theme). |

The picture path is never delayed by the HUD: the overlay repaints on its own schedule and is composed by the platform / GPU, so emulated frame delivery and A/V sync are unaffected.

## 2. Three compositing paths

The presenter has one rasterization stage and three ways to put the result on screen. The path is selected once per surface, at presenter creation, from what the client owns.

```
snapshot ──▶ layout ──▶ raster cache (per element: premultiplied ARGB, device pixels)
                                  │
                ┌─────────────────┼──────────────────────┐
                ▼                 ▼                       ▼
     (A) platform overlay   (B) CPU composite         (C) GPU composite
     Qt raster backing      into the final RGBA       texture quads after
     store (unreal-qt r1)   buffer (SIMD + scalar)    the CRT pass (RHI / GL)
```

### 2.1 Path A — platform overlay (unreal-qt, r1)

`HudOverlay` is a translucent sibling widget over `DeviceScreen`. QPainter draws the cached element pixmaps (`drawPixmap` with per-element opacity) into the widget's backing store; the window system composes the overlay over the picture. Nothing in the picture path changes.

- Cost is bounded by the dirty region: `update(rect)` per animated element, `QRegion` union for stacks.
- Translucent panel blur ("frosted" look) is **not** done here; Qt has no cheap backdrop blur in raster widgets and sampling the picture underneath from the overlay is a copy per repaint. The modern theme uses plain translucency on this path; blur is a Path C feature.
- This is enough for r1 and for the widget-based CRT-less presentation.

### 2.2 Path B — CPU compositing into the final buffer

Used when the client owns the final pixel buffer: the SDL3 software presenter, a burn-in encoder option, or a future `unreal-qt` mode where `DeviceScreen` scales the frame itself into an output-sized `QImage` (then the HUD is blended into that image before `drawImage`, saving the platform composite).

Per element: a rectangle of premultiplied ARGB source over an RGBA destination:

```
dst = src + dst * (255 - src.a) / 255      (per channel, premultiplied "over")
```

Implementation tiers, dispatched once at start-up:

| Tier | Where | Notes |
|------|-------|-------|
| NEON | Apple silicon, ARM Linux, Windows on ARM | 8 or 16 pixels per iteration, `vmull_u8` / `vshrn_n_u16`. The codebase already has NEON paths in `ScreenZX` (`Batch8_NEON`) and `common/video/videoutils.h`; the blend kernel goes next to them. |
| SSE2 / SSSE3 / AVX2 | x86-64 | SSE2 baseline (every x86-64 CPU), AVX2 when detected. |
| Scalar | everything else, and the reference for tests | Plain loop; also used for rows narrower than the vector width. |

Rules: sources are cached premultiplied so the kernel has no division; rows are processed with the vector kernel and a scalar tail; fully opaque runs use `memcpy`; fully transparent runs are skipped via a per-row alpha summary computed when the element is rasterized. Budget: a 24 em x 6 em toast at 4K (roughly 1300 x 320 pixels) is ~0.4 M pixels — well under 0.1 ms with NEON / AVX2, ~0.4 ms scalar.

### 2.3 Path C — GPU compositing

Used when the picture is presented through a GPU surface (the planned CRT pass on `QRhiWidget` / `QOpenGLWidget`, the SDL3 GPU player).

- Element pixmaps live in a small texture atlas (one 2048² RGBA8 page is enough for a full theme's glyphs and panels at 4K). Atlas updates are sub-image uploads on cache misses only.
- Each element is one quad with per-vertex opacity; the whole HUD is one draw call with alpha blending, issued after the CRT pass and before present.
- Backdrop blur for the frosted modern theme: a two-tap separable Gaussian on a downscaled copy of the picture behind each panel, sampled in the panel shader. Optional, off on integrated GPUs below a threshold.
- Fallback: if the client has a GPU surface but no atlas support (unlikely), Path A / B.

## 3. Rasterization cache

Two-tier cache with strict aliasing prevention:

| Tier | Contents | Key | Eviction | Budget |
|------|----------|-----|----------|--------|
| **Panel shapes** | Rounded rectangles per size class (width bucket × height bucket × corner radius × theme × DPR) | `(widthBucket, heightBucket, radius, themeId, dpr)` — width/height buckets are `round(emSize * 2)` so panels within ½ em share shapes | 8 entries | ~200 KB at 4K |
| **Text runs** | Premultiplied ARGB bitmaps per string content | `(contentHash, fontSizePx, themeId, dpr)` — `contentHash` is `std::hash` of the rendered string; `fontSizePx` is integer device pixels | 32 entries LRU | ~2 MB at 4K |

**Total budget:** ~2.5 MB at 4K DPR 1, ~4 MB at Retina 5K DPR 2.

**Aliasing prevention rules:**
- **DPR in key:** DPR is part of every cache key — a Retina entry is never served to a 1x display.
- **Integer device pixel font size:** Font size in the key is the final integer device-pixel size (after rounding), not the em multiplier — two slightly different output heights that round to the same px size share entries correctly; different px sizes never collide.
- **Snapping buckets:** Width/height buckets snap to half-em increments so resize jitter does not thrash the cache, but visually distinct sizes receive distinct entries.
- **Theme invalidation:** Theme ID is part of the key — switching modern ↔ retro invalidates all cached entries.
- **Content-based text caching:** `std::hash` of the string content, not the element ID — identical text on two different toasts shares the entry. Toasts with a counter ("Disk written x3") change content and re-rasterize only their text line.
- **Invalidation events:** Theme change, DPR change (window moved between displays — `QWindow::screenChanged`), or em change (output height changed by more than one device pixel of em).

## 4. HiDPI

- All geometry is computed in device pixels: `em = outputHeightDevicePixels / 40`. Logical coordinates appear only at the boundary where Qt needs them (`QWidget::geometry` in logical units; painting is done with `painter.scale(1/DPR)` or by setting the pixmaps' `devicePixelRatio`).
- Fonts: pixel size = `round(em * factor)` in device pixels; hinting left to the platform so text matches other native UI.
- Snapping: element rects snap to integer device pixels; sub-pixel animation offsets are allowed only during motion (they are what makes a slide look smooth), the final resting position is snapped.
- This is the first DPR-aware code in `unreal-qt`; `DeviceScreen` itself still paints in logical units, which is fine for the picture (it is scaled anyway) but would be wrong for text.

## 5. Repaint scheduling

- Sources of repaint: snapshot generation change (from `NC_HUD_CHANGED`, marshalled to the UI thread), animation tick, geometry change, theme / DPR change.
- Animation tick: a single `QTimer` (Path A) or the presenter's frame callback (Paths B / C) armed only while `HudAnimator::hasActiveAnimations()`. Interval = `1000 / DisplayRefreshInfo::renderCapHz()`, so a 120 Hz VRR panel gets 120 Hz motion and a 60 Hz panel 60 Hz; the timer is stopped when the last animation ends.
- Dirty regions: each animated element reports its previous and current device rect; the union is the repaint region. Static elements are never repainted.
- Coalescing: model changes arriving faster than the display refresh are folded into the next tick (generation counter compare), so an event burst (e.g. 30 FDD state changes in one second) costs at most refresh-rate repaints.

## 6. Threading

- `HudModel` is updated on the emulator / message-center thread; it publishes snapshots by atomically swapping a `shared_ptr<const HudSnapshot>`. Readers (presenters) take the pointer, never lock.
- Presenters run on the UI thread (Path A) or the client's render thread (B / C). No shared mutable state besides the snapshot pointer and the enable flag (atomic).
- Wall-clock timestamps in the snapshot (`created`, `ttl`) come from `steady_clock` on the producer; consumers evaluate expiry against the same clock, so there is no cross-thread time sync.

## 7. Zero cost when inactive — enforcement

- Turning the `hud` feature off makes `FeatureManager::onFeatureChanged()` call `HudModel::UpdateFeatureCache()`, which removes every `MessageCenter` observer, clears the collections and publishes an empty snapshot. `MessageCenter::Post` then has no HUD observer to call.
- The client destroys the presenter (overlay widget, caches, timer). The only remaining HUD reference is `std::shared_ptr<HudModel>` in the binding, tested with one cached boolean on the paths that would feed it — the same pattern as `_feature_sound_enabled`.
- A unit test asserts that with the feature off, posting each HUD-relevant notification produces no observer calls and no snapshot generation change; a benchmark asserts the presented-frame path has no measurable delta between "feature off" and "HUD compiled out".

## 8. Measurement plan

| Metric | How | Target |
|--------|-----|--------|
| Overlay repaint time (Path A) | `QElapsedTimer` around `paintEvent`, logged via module logger at debug level | < 0.3 ms at 4K, 2 toasts + 6 indicators |
| Blend kernel throughput (Path B) | benchmark in `core/benchmarks` over 1920x1080 and 3840x2160 surfaces, all tiers | NEON / AVX2 >= 4x scalar |
| Emulated frame delivery with HUD animating | existing FPS readout and present-latency stat, compare HUD on / off | no change beyond noise |
| Memory | cache size counter exposed in dev stats | < 4 MB (~2.5 MB typical at 4K) |
