# HUD Layer — Interfaces, Notifications and Automation

| Rev | Date | Description |
|-----|------|-------------|
| r1 | 2026-09-07 | Proposed C++ interfaces for the model, snapshot and presenter; event mapping; automation surface; settings. Architecture in `design.md`. |

All names are proposals for review; the shapes matter more than the identifiers.

## 1. Model types (`unreal-qt/src/hud/`, Qt-free)

```cpp
enum class HudKind : uint8_t { Toast, Indicator, Stats, Banner };
enum class HudAnchor : uint8_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
enum class HudPriority : uint8_t { Low, Normal, High, Critical };
enum class HudState : uint8_t { Off, Idle, Active, Alert };   // indicators
enum class HudAnimation : uint8_t { None, Fade, SlideFade, ScaleFade, Pulse };

using HudClock = std::chrono::steady_clock;

struct HudElement
{
    std::string id;            // stable key; toasts get "toast/<n>", indicators "ind/fdd/A"
    HudKind kind;
    HudAnchor anchor;
    HudPriority priority;
    HudClock::time_point created;
    std::chrono::milliseconds ttl{0};      // 0 = persistent
    HudAnimation enter, exit;

    std::string title;         // toast title / indicator label
    std::string body;          // toast body (plain text, may contain '\n')
    std::string icon;          // symbolic name ("floppy", "rec", "pause", "turbo", "file"), theme resolves it
    std::string value;         // indicator value ("A:12", "2x")
    HudState state{HudState::Idle};
    float progress{-1.f};      // 0..1 when >= 0 (toast progress bar)
    uint32_t coalesced{0};     // dedup counter shown as "x3"
    std::string dedupKey;      // equal keys coalesce
};

struct HudSnapshot
{
    uint64_t generation;
    HudClock::time_point produced;
    std::vector<HudElement> elements;      // sorted: indicators, then toasts by priority desc, age asc
};
```

## 2. HudModel (`unreal-qt/src/hud/`, Qt-free)

```cpp
class HudModel : public Observer
{
public:
    explicit HudModel(EmulatorContext* context);
    ~HudModel() override;

    // Activation follows the `hud` feature: the model reads FeatureManager::isEnabled("hud")
    // at bind and reacts to NC_FEATURE_CHANGED afterwards (subscribe / unsubscribe / clear).
    // There is no separate enable API, so the menu, CLI and REST share one switch.
    void onFeatureChanged(bool enabled);
    bool isEnabled() const;   // cached feature state (zero cost when off: no observers, empty snapshot)

    // Producer API (automation, host UI, core subsystems)
    std::string notify(const HudToastRequest& request);     // returns element id
    void dismiss(const std::string& id);
    void setIndicator(const std::string& key, HudState state, std::string value = {}, std::string label = {});
    void clearIndicator(const std::string& key);

    // Consumer API
    std::shared_ptr<const HudSnapshot> snapshot() const;    // atomic load, never blocks
    uint64_t generation() const;

    // Housekeeping (called by the presenter's tick or by a core timer; expires toasts)
    void expire(HudClock::time_point now);

    // Limits
    void setLimits(size_t visibleToasts, size_t queuedToasts);

private:
    // MessageCenter callbacks (emulator thread)
    void onFddState(int id, Message* message);
    void onFddDisk(int id, Message* message);
    void onEmulatorState(int id, Message* message);
    void onSystemReset(int id, Message* message);
    void onSpeedChanged(int id, Message* message);
    void onRecording(int id, Message* message);
    void onFileLoaded(int id, Message* message);

    void publish();   // rebuild + atomic swap + changed callback (NC_HUD_CHANGED once hosted by core)
};

struct HudToastRequest
{
    std::string title, body, icon, dedupKey;
    HudPriority priority{HudPriority::Normal};
    std::chrono::milliseconds ttl{3500};
    HudAnchor anchor{HudAnchor::Bottom};
    float progress{-1.f};
};
```

Thread-safety: producer calls take an internal mutex to mutate the collections, then build a new snapshot and swap the pointer. Consumers never lock. `expire()` is cheap (compare timestamps) and is what turns TTL into removal; presenters call it once per tick, the TUI on demand.

## 3. Notifications

New ids in `core/src/emulator/platform.h` (the only core changes in r1):

| Id | Payload | Emitted by | Purpose |
|----|---------|------------|---------|
| `NC_FEATURE_CHANGED` | `FeatureChangedPayload { emulatorId, featureId, enabled }` | `FeatureManager::onFeatureChanged` | lets app-side consumers (the HUD model, later menus) follow feature toggles from any channel |
| `NC_SPEED_CHANGED` | `SpeedChangedPayload { emulatorId, multiplier, turbo }` | `Core::EnableTurboMode / DisableTurboMode`, `Emulator::SetSpeedMultiplier` | speed indicator (also useful to the status bar) |
| `NC_FILE_LOADED` | `FileLoadedPayload { emulatorId, kind, path, ok }` | snapshot / tape / disk loaders in `Emulator` | file toasts for both UI and automation loads |
| `NC_RECORDING_STATE` | `RecordingStatePayload { emulatorId, recording, path }` | recording manager | REC indicator, stop toast |
| `NC_HUD_CHANGED` (*later*, when the model is hosted by core) | `HudChangedPayload { emulatorId, generation }` | `HudModel::publish` | wake presenters in other clients |
| `NC_VIDEO_MODE_CHANGED` (existing) | `EmulatorFramePayload { emulatorId }` | `Screen` on mode switch | overlay re-creation (`HudOverlayHost::onFramebufferGeometryChanged`) |

Existing ids consumed unchanged: `NC_FDD_STATE_CHANGED`, `NC_FDD_MOTOR_*`, `NC_FDD_DISK_INSERTED / EJECTED / PENDING_WRITE / WRITTEN / SAVE_RETARGETED`, `NC_EMULATOR_STATE_CHANGE`, `NC_SYSTEM_RESET`.

Event → element mapping (defaults, theme independent):

| Event | Element | Title / body | Priority, TTL |
|-------|---------|--------------|---------------|
| FDD motor on | indicator `fdd/<letter>` → Active, value `track` | — | persistent |
| FDD motor off | indicator → Idle | — | persistent |
| Disk inserted | toast, icon floppy | "Drive A" / file name | Normal, 3.5 s |
| Disk ejected | toast | "Drive A" / "Disk ejected" | Low, 2 s |
| Disk written | toast, dedup `disk-written/<letter>` | "Drive A" / "Saved <name>" | Normal, 2.5 s |
| Save retargeted | toast | "Drive A" / reason + new path | High, 6 s |
| Pending write | indicator `fdd/<letter>` → Alert (dot) | — | persistent until written |
| Paused / resumed | indicator `pause` Active / Off | — | persistent |
| Stopped | toast | "Emulator stopped" | Normal, 2 s |
| Reset | toast | "Reset" | Low, 1.5 s |
| Turbo on / off, speed | indicator `speed` value "turbo" / "2x" / Off at 1x | — | persistent |
| Recording start / stop | indicator `rec` Active (pulse) / Off; toast with path on stop | "Recording saved" / path | Normal, 5 s |
| File loaded | toast, icon file | "Loaded" / file name | Normal, 3 s |
| Load failed | toast | "Load failed" / file name | High, 5 s |

## 4. Presenter (`unreal-qt/src/hud/`: Qt-free layout / animation / compositor interface, plus the Qt overlay)

```cpp
struct HudSurface
{
    QRect  outputRect;   // device pixels; QRect or the client's rect type
    QRect  imageRect;    // where the picture sits inside outputRect
    qreal  dpr;
    QMargins safeInsets;
};

class HudPresenter
{
public:
    HudPresenter(std::shared_ptr<HudModel> model, HudTheme theme, HudCompositor& compositor);
    void setSurface(const HudSurface& surface);      // geometry / DPR change → relayout, cache invalidation
    void setTheme(const HudTheme& theme);
    void setReducedMotion(bool on);

    bool needsRepaint(HudClock::time_point now) const;   // snapshot changed or animation active
    HudDirtyRegion tick(HudClock::time_point now);       // advance animations, expire toasts, return dirty rects
    void paint(HudPaintTarget& target, const HudDirtyRegion& region);   // draws cached elements via the compositor

    bool hasActiveAnimations() const;    // host arms / disarms its tick timer from this
};
```

`HudCompositor` is the strategy for the three paths (`rendering.md` §2): `QPainterCompositor` (Path A), `CpuBlendCompositor` (Path B, SIMD-dispatched), `RhiCompositor` (Path C).

`HudLayout` (pure, unit-tested): `std::vector<HudPlacement> layout(const HudSnapshot&, const HudSurface&, const HudTheme&)`, where a placement is `{ elementId, deviceRect, inLetterboxBand }`.

`HudAnimator` (pure, unit-tested): per element `{ start, duration, kind, from, to }` → `HudTransform { opacity, offset, scale }` at `now`.

## 5. unreal-qt glue

- `HudOverlay : QWidget` — creates a `HudPresenter` with `QPainterCompositor`; `paintEvent` → `presenter.paint`; a `QTimer` armed by `hasActiveAnimations()` at `DisplayRefreshRate::query(screen()).renderCapHz()`; `setSurface` on `resizeEvent` (window resize only: same framebuffer, new output size) and on `QWindow::screenChanged` (DPR / refresh rate).
- `HudOverlayHost` (owned by `MainWindow`) — the only object allowed to create or destroy an overlay:

```cpp
class HudOverlayHost : public QObject
{
public:
    HudOverlayHost(QFrame* contentFrame, DeviceScreen* deviceScreen, QObject* parent);
    void onHudFeatureChanged(bool enabled);                     // mirrors the `hud` feature (menu, CLI, REST)
    void rebind(std::shared_ptr<HudModel> model);               // adoptEmulator / releaseEmulator (nullptr)
    void onFramebufferGeometryChanged();                        // NC_VIDEO_MODE_CHANGED, viewport preset change
private:
    void recreate();   // destroy(); if (enabled && model) create(); attach();
    std::unique_ptr<HudOverlay> _overlay;
};
```

  `rebind()` and `onFramebufferGeometryChanged()` both end in `recreate()`. A resize of the window does *not* recreate (the framebuffer is unchanged); a change of the framebuffer always does, even when the new size happens to equal the old one, because the `DeviceScreen` backing image is new.
- `MainWindow` — owns the overlay host (`HudOverlayHost`) as a child of `contentFrame`; binds the model of the adopted emulator (`EmulatorBinding` gains `hudModel()`); View → HUD calls `FeatureManager::setFeature(Features::kHud, checked)` and mirrors the feature state into the menu check (the feature can also change from the CLI / REST, so the menu follows the model's `isEnabled()`, not the other way round). The host creates / destroys the overlay from the model's enabled state.
- Settings (`QSettings("Unreal", "Unreal-NG")`) hold presentation preferences only: `Hud/Theme` ("modern"), `Hud/ReducedMotion` (false), `Hud/DevStats` (false), `Hud/ToastAnchor` ("bottom"), `Hud/IndicatorAnchor` ("topRight"). Enablement is the `hud` feature (§7).

## 6. Automation surface (deferred — applies once the model is hosted by core)

Not part of r1: with the model inside unreal-qt there is nothing for the core automation modules to reach, and adding a GUI-side IPC just for this would be a second automation channel. The table records the intended shape so that moving the model into core later exposes it without redesign. Enable / disable is available today through the `hud` feature on every channel.

| Channel | Operation | Notes |
|---------|-----------|-------|
| REST (`webapi`) | `POST /api/v1/emulators/{id}/hud/notify` `{title, body, ttl_ms, priority, icon}` → `{id}`; `DELETE .../hud/toasts/{id}`; `GET .../hud/state` → snapshot JSON; enable / disable through the existing feature endpoint (`feature hud on|off`) | mirrors `HudModel` producer / consumer API |
| CLI (telnet) | `hud notify "<title>" "<body>" [ttl]`, `hud state`; `feature hud on|off` (existing command) | `hud state` doubles as the TUI renderer |
| MCP tool | `hud_notify`, `hud_state` | thin wrappers over REST |
| Lua / Python | `hud.notify(title, body, ttl)`, `hud.indicator(key, state, value)` | scripts announce their own progress (e.g. tape-to-audio import) |

Automation-originated toasts are ordinary elements; there is no separate rendering path.

## 7. Feature flag (the only enable switch)

- `Features::kHud = "hud"` (alias `hud`, description "On-screen HUD: indicators and messages over the emulator picture"), registered in `FeatureManager` with default `on`; `features.ini` gains `[hud] state = on`.
- `FeatureManager::onFeatureChanged()` posts `NC_FEATURE_CHANGED`; the HUD model (in unreal-qt) subscribes and calls its own `onFeatureChanged(enabled)`. Turning the feature off is what makes the HUD free: no observers, empty snapshot, presenter destroyed by the client. When the model moves into core the direct `UpdateFeatureCache()` hook replaces the notification for it.
- There is deliberately no second switch (no `Hud/Enabled` setting, no `setEnabled` API): desktop menu, CLI `feature hud off`, REST feature endpoint, MCP and scripts all flip the same flag, and it persists with the other features.
