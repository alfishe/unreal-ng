#pragma once

#include "hudsnapshot.h"
#include "hudtiming.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/message-center/eventqueue.h"
#include "common/uuid.h"

class EmulatorContext;

/// @brief Qt-free state model for the emulator HUD overlay.
///
/// Subscribes to MessageCenter notifications (FDD, state, breakpoints, speed, file loads, recording),
/// aggregates active indicators, toasts, and picture augmentations (tiles, images, tilemaps, text),
/// deduplicates and expires them, and publishes atomic immutable HudSnapshot values for lock-free reading.
///
/// Fully detachable from the Qt client — depends only on core emulator types.
class HudModel : public Observer
{
public:
    explicit HudModel(EmulatorContext* context = nullptr);
    virtual ~HudModel();

    HudModel(const HudModel&) = delete;
    HudModel& operator=(const HudModel&) = delete;

    /// @brief Update activation state when the `hud` feature changes.
    /// When disabled, all observers are detached, collections cleared, and an empty
    /// snapshot published (zero-cost when off).
    void onFeatureChanged(bool enabled);

    /// @brief Check whether HUD overlay is enabled
    bool isEnabled() const;

    // --- Producer API: Toasts & Indicators ---

    /// @brief Request a toast notification. Returns assigned or coalesced element ID.
    std::string notify(const HudToastRequest& request);

    /// @brief Dismiss a toast notification by element ID
    void dismiss(const std::string& id);

    /// @brief Set or update a persistent or expiring indicator
    void setIndicator(const std::string& key, HudState state, std::string value = {}, std::string label = {},
                      const std::string& icon = {}, std::chrono::milliseconds ttl = std::chrono::milliseconds(0),
                      bool monospace = false);

    /// @brief Remove a persistent indicator
    void clearIndicator(const std::string& key);

    // --- Producer API: Picture Augmentations ---

    /// @brief Add or update an image element
    void setImage(const std::string& id, const HudImageRequest& req);

    /// @brief Add or update a tile element from a tileset
    void setTile(const std::string& id, const HudTileRequest& req);

    /// @brief Add or update a dense tilemap element
    void setTilemap(const std::string& id, const HudTilemapRequest& req);

    /// @brief Add or update arbitrary positioned text
    void setText(const std::string& id, const HudTextRequest& req);

    /// @brief Remove any element by ID (toast, indicator, image, tile, tilemap, text)
    void removeElement(const std::string& id);

    /// @brief Clear all picture augmentations (images, tiles, tilemaps, text)
    void clearAugmentations();

    /// @brief Transactional batching: begins batch (defers snapshot publishing)
    void beginBatch();

    /// @brief Transactional batching: ends batch (publishes snapshot if dirty)
    void endBatch();

    // --- Consumer API (presenters / renderers) ---

    /// @brief Obtain the latest immutable snapshot (atomic load, lock-free)
    std::shared_ptr<const HudSnapshot> snapshot() const;

    /// @brief Current snapshot generation counter
    uint64_t generation() const;

    // --- Housekeeping & Limits ---

    /// @brief Expire elapsed toasts against the provided time point
    void expire(HudClock::time_point now);

    /// @brief Configure visible and queued toast limits
    void setLimits(size_t visibleToasts, size_t queuedToasts);

    // --- Styling & HiDPI Scaling ---

    /// @brief Set active theme ID
    void setTheme(HudThemeId themeId);

    /// @brief Get active theme ID
    HudThemeId theme() const;

    /// @brief Set base HiDPI UI scale multiplier (default 2.0x)
    void setScaleFactor(float factor);

    /// @brief Get base HiDPI UI scale multiplier
    float scaleFactor() const;

    /// @brief Set default display position for toasts
    void setToastPosition(HudTilePosition pos);

    /// @brief Get default display position for toasts
    HudTilePosition toastPosition() const;

    /// @brief Set default display position for indicators
    void setIndicatorPosition(HudTilePosition pos);

    /// @brief Get default display position for indicators
    HudTilePosition indicatorPosition() const;

    /// @brief Register callback invoked whenever a new snapshot is published
    void setChangedCallback(std::function<void()> callback);

private:
    void subscribeFeatureObserver();
    void unsubscribeFeatureObserver();
    void subscribeObservers();
    void unsubscribeObservers();

    // MessageCenter callbacks
    void onFeatureNotification(int id, Message* message);
    void onFddState(int id, Message* message);
    void onFddDisk(int id, Message* message);
    void onEmulatorState(int id, Message* message);
    void onBreakpoint(int id, Message* message);
    void onCpuStep(int id, Message* message);
    void onSystemReset(int id, Message* message);
    void onSpeedChanged(int id, Message* message);
    void onRecording(int id, Message* message);
    void onFileLoaded(int id, Message* message);

    bool matchesInstance(const unreal::UUID& id) const;

    void publishLocked();

private:
    EmulatorContext* _context = nullptr;
    std::atomic<bool> _enabled{false};
    std::atomic<uint64_t> _generation{0};

    mutable std::mutex _mutex;
    std::shared_ptr<const HudSnapshot> _publishedSnapshot;

    std::vector<HudElement> _indicators;
    std::vector<HudElement> _toasts;
    std::vector<HudElement> _augmentations;

    size_t _visibleToasts = HudLimits::DefaultVisibleToasts;
    size_t _queuedToasts = HudLimits::DefaultQueuedToasts;
    uint64_t _nextToastId = 0;
    int _batchLevel = 0;
    bool _batchDirty = false;

    HudThemeId _themeId{HudThemeId::DarkGlass};
    float _scaleFactor{2.0f};
    HudTilePosition _toastPosition{HudTilePosition::MidBottom};
    HudTilePosition _indicatorPosition{HudTilePosition::TopRight};

    std::function<void()> _changedCallback;

    uint64_t _featureObserverId = 0;
    std::vector<std::pair<std::string, uint64_t>> _eventObserverIds;
};
