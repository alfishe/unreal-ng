#include "hudmodel.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

HudModel::HudModel(EmulatorContext* context)
    : _context(context)
{
    auto initialSnapshot = std::make_shared<HudSnapshot>();
    initialSnapshot->generation = 0;
    initialSnapshot->produced = HudClock::now();
    std::atomic_store(&_publishedSnapshot, std::shared_ptr<const HudSnapshot>(initialSnapshot));

    bool enabled = true;
    if (_context && _context->pFeatureManager)
    {
        enabled = _context->pFeatureManager->isEnabled(Features::kHud);
    }
    _enabled.store(enabled, std::memory_order_release);

    subscribeFeatureObserver();

    if (enabled)
    {
        subscribeObservers();
    }
}

HudModel::~HudModel()
{
    unsubscribeObservers();
    unsubscribeFeatureObserver();
}

void HudModel::onFeatureChanged(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_enabled.load(std::memory_order_relaxed) == enabled)
        return;

    _enabled.store(enabled, std::memory_order_release);

    if (enabled)
    {
        subscribeObservers();
        publishLocked();
    }
    else
    {
        unsubscribeObservers();
        _toasts.clear();
        _indicators.clear();
        _augmentations.clear();
        publishLocked();
    }
}

bool HudModel::isEnabled() const
{
    return _enabled.load(std::memory_order_acquire);
}

std::string HudModel::notify(const HudToastRequest& request)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return {};

    // Deduplication / coalescing
    if (!request.dedupKey.empty())
    {
        auto it = std::find_if(_toasts.begin(), _toasts.end(), [&](const HudElement& el) {
            return el.dedupKey == request.dedupKey;
        });

        if (it != _toasts.end())
        {
            if (request.coalesceCount)
            {
                it->coalesced = (it->coalesced < 2) ? 2 : (it->coalesced + 1);
            }
            else
            {
                it->coalesced = 0;
            }
            it->created = HudClock::now();
            it->ttl = request.ttl;
            if (!request.title.empty()) it->title = request.title;
            if (!request.body.empty()) it->body = request.body;
            if (!request.icon.empty()) it->icon = request.icon;
            it->priority = request.priority;
            it->progress = request.progress;

            publishLocked();
            return it->id;
        }
    }

    // Enforce queue limit: hard eviction of lowest priority / oldest toast
    while (_toasts.size() >= _queuedToasts)
    {
        auto lowest = std::min_element(_toasts.begin(), _toasts.end(), [](const HudElement& a, const HudElement& b) {
            if (static_cast<uint8_t>(a.priority) != static_cast<uint8_t>(b.priority))
                return static_cast<uint8_t>(a.priority) < static_cast<uint8_t>(b.priority);
            return a.created < b.created;
        });
        if (lowest != _toasts.end())
        {
            _toasts.erase(lowest);
        }
        else
        {
            break;
        }
    }

    HudElement el;
    el.id = "toast/" + std::to_string(++_nextToastId);
    el.kind = HudKind::Toast;
    el.position = (request.position != HudTilePosition::Custom) ? request.position : _toastPosition;
    el.anchor = (request.anchor != HudAnchor::None) ? request.anchor : HudTilePositionToAnchor(el.position);
    el.margins = request.margins;
    el.priority = request.priority;
    el.created = HudClock::now();
    el.ttl = request.ttl;
    el.enter = HudAnimation::SlideFade;
    el.exit = HudAnimation::Fade;
    el.title = request.title;
    el.body = request.body;
    el.icon = request.icon;
    el.progress = request.progress;
    el.coalesced = 0;
    el.dedupKey = request.dedupKey;
    el.zIndex = 200; // toasts float above augmentations and indicators

    std::string assignedId = el.id;
    _toasts.push_back(std::move(el));

    publishLocked();
    return assignedId;
}

void HudModel::dismiss(const std::string& id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = std::remove_if(_toasts.begin(), _toasts.end(), [&](const HudElement& el) {
        return el.id == id;
    });
    if (it != _toasts.end())
    {
        _toasts.erase(it, _toasts.end());
        publishLocked();
    }
}

void HudModel::setIndicator(const std::string& key, HudState state, std::string value, std::string label,
                            const std::string& icon, std::chrono::milliseconds ttl, bool monospace)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    std::string id = "ind/" + key;
    auto it = std::find_if(_indicators.begin(), _indicators.end(), [&](const HudElement& el) {
        return el.id == id;
    });

    if (it != _indicators.end())
    {
        it->state = state;
        it->value = std::move(value);
        it->created = HudClock::now();
        it->ttl = ttl;
        it->monospace = monospace;
        if (!label.empty())
        {
            it->title = std::move(label);
        }
        if (!icon.empty())
        {
            it->icon = icon;
        }
    }
    else
    {
        HudElement el;
        el.id = id;
        el.kind = HudKind::Indicator;
        el.position = _indicatorPosition;
        el.anchor = HudTilePositionToAnchor(_indicatorPosition);
        el.priority = HudPriority::Normal;
        el.created = HudClock::now();
        el.ttl = ttl;
        el.enter = HudAnimation::Fade;
        el.exit = HudAnimation::Fade;
        el.title = std::move(label);
        el.value = std::move(value);
        el.icon = icon;
        el.monospace = monospace;
        el.state = state;
        el.zIndex = 100; // indicators float above picture augmentations
        _indicators.push_back(std::move(el));
    }

    publishLocked();
}

void HudModel::clearIndicator(const std::string& key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::string id = "ind/" + key;
    auto it = std::remove_if(_indicators.begin(), _indicators.end(), [&](const HudElement& el) {
        return el.id == id;
    });
    if (it != _indicators.end())
    {
        _indicators.erase(it, _indicators.end());
        publishLocked();
    }
}

void HudModel::setImage(const std::string& id, const HudImageRequest& req)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    auto it = std::find_if(_augmentations.begin(), _augmentations.end(), [&](const HudElement& el) {
        return el.id == id;
    });

    if (it == _augmentations.end())
    {
        HudElement el;
        el.id = id;
        el.kind = HudKind::Image;
        _augmentations.push_back(el);
        it = _augmentations.end() - 1;
    }

    it->kind = HudKind::Image;
    it->image = req.image;
    it->rect = req.rect;
    it->srcRect = req.srcRect;
    it->position = req.position;
    it->margins = req.margins;
    it->coordSpace = req.coordSpace;
    it->referenceSize = req.referenceSize;
    it->zIndex = req.zIndex;
    it->opacity = req.opacity;
    it->color = req.tint;
    it->filter = req.filter;
    it->styleId = req.styleId;

    publishLocked();
}

void HudModel::setTile(const std::string& id, const HudTileRequest& req)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    auto it = std::find_if(_augmentations.begin(), _augmentations.end(), [&](const HudElement& el) {
        return el.id == id;
    });

    if (it == _augmentations.end())
    {
        HudElement el;
        el.id = id;
        el.kind = HudKind::Tile;
        _augmentations.push_back(el);
        it = _augmentations.end() - 1;
    }

    it->kind = HudKind::Tile;
    it->image = req.tileset;
    it->rect = req.rect;
    it->srcRect = req.srcRect;
    it->position = req.position;
    it->margins = req.margins;
    it->coordSpace = req.coordSpace;
    it->referenceSize = req.referenceSize;
    it->zIndex = req.zIndex;
    it->opacity = req.opacity;
    it->color = req.tint;
    it->filter = req.filter;
    it->styleId = req.styleId;

    publishLocked();
}

void HudModel::setTilemap(const std::string& id, const HudTilemapRequest& req)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    auto it = std::find_if(_augmentations.begin(), _augmentations.end(), [&](const HudElement& el) {
        return el.id == id;
    });

    if (it == _augmentations.end())
    {
        HudElement el;
        el.id = id;
        el.kind = HudKind::Tilemap;
        _augmentations.push_back(el);
        it = _augmentations.end() - 1;
    }

    it->kind = HudKind::Tilemap;
    it->tilemap = req.tilemap;
    it->rect = req.destRect;
    it->position = req.position;
    it->margins = req.margins;
    it->coordSpace = req.coordSpace;
    it->referenceSize = req.referenceSize;
    it->zIndex = req.zIndex;
    it->opacity = req.opacity;
    it->filter = req.filter;
    it->styleId = req.styleId;

    publishLocked();
}

void HudModel::setText(const std::string& id, const HudTextRequest& req)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    auto it = std::find_if(_augmentations.begin(), _augmentations.end(), [&](const HudElement& el) {
        return el.id == id;
    });

    if (it == _augmentations.end())
    {
        HudElement el;
        el.id = id;
        el.kind = HudKind::Text;
        _augmentations.push_back(el);
        it = _augmentations.end() - 1;
    }

    it->kind = HudKind::Text;
    it->title = req.text;
    it->rect = req.rect;
    it->position = req.position;
    it->margins = req.margins;
    it->coordSpace = req.coordSpace;
    it->referenceSize = req.referenceSize;
    it->zIndex = req.zIndex;
    it->opacity = req.opacity;
    it->color = req.color;
    it->bgColor = req.bgColor;
    it->textAlign = req.align;
    it->fontSize = req.fontSize;
    it->styleId = req.styleId;

    publishLocked();
}

void HudModel::removeElement(const std::string& id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    bool changed = false;
    auto eraseFrom = [&](std::vector<HudElement>& vec) {
        auto it = std::remove_if(vec.begin(), vec.end(), [&](const HudElement& el) {
            return el.id == id;
        });
        if (it != vec.end())
        {
            vec.erase(it, vec.end());
            changed = true;
        }
    };

    eraseFrom(_toasts);
    eraseFrom(_indicators);
    eraseFrom(_augmentations);

    if (changed)
    {
        publishLocked();
    }
}

void HudModel::clearAugmentations()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_augmentations.empty())
        return;

    _augmentations.clear();
    publishLocked();
}

void HudModel::beginBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _batchLevel++;
}

void HudModel::endBatch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (--_batchLevel <= 0)
    {
        _batchLevel = 0;
        if (_batchDirty)
        {
            _batchDirty = false;
            publishLocked();
        }
    }
}

std::shared_ptr<const HudSnapshot> HudModel::snapshot() const
{
    return std::atomic_load(&_publishedSnapshot);
}

uint64_t HudModel::generation() const
{
    return _generation.load(std::memory_order_acquire);
}

void HudModel::expire(HudClock::time_point now)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled.load(std::memory_order_relaxed))
        return;

    bool changed = false;
    auto it = std::remove_if(_toasts.begin(), _toasts.end(), [&](const HudElement& el) {
        return el.ttl.count() > 0 && (now - el.created) >= el.ttl;
    });

    if (it != _toasts.end())
    {
        _toasts.erase(it, _toasts.end());
        changed = true;
    }

    auto itInd = std::remove_if(_indicators.begin(), _indicators.end(), [&](const HudElement& el) {
        bool expired = el.ttl.count() > 0 && (now - el.created) >= el.ttl;
        // Reset exec state when pause indicator expires
        if (expired && el.id == "ind/pause")
        {
            _execState = ExecState::Idle;
            _execStateExpiry = {};
        }
        return expired;
    });

    if (itInd != _indicators.end())
    {
        _indicators.erase(itInd, _indicators.end());
        changed = true;
    }

    if (changed)
    {
        publishLocked();
    }
}

void HudModel::setLimits(size_t visibleToasts, size_t queuedToasts)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _visibleToasts = (visibleToasts > 0) ? visibleToasts : 1;
    _queuedToasts = std::max(_visibleToasts, queuedToasts);
    publishLocked();
}

void HudModel::setTheme(HudThemeId themeId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_themeId == themeId)
        return;
    _themeId = themeId;
    publishLocked();
}

HudThemeId HudModel::theme() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _themeId;
}

void HudModel::setScaleFactor(float factor)
{
    std::lock_guard<std::mutex> lock(_mutex);
    float clamped = std::clamp(factor, 0.5f, 5.0f);
    if (std::abs(_scaleFactor - clamped) < 1e-3f)
        return;
    _scaleFactor = clamped;
    publishLocked();
}

float HudModel::scaleFactor() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _scaleFactor;
}

void HudModel::setToastPosition(HudTilePosition pos)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_toastPosition == pos)
        return;
    _toastPosition = pos;
    for (auto& t : _toasts)
    {
        t.position = pos;
        t.anchor = HudTilePositionToAnchor(pos);
    }
    publishLocked();
}

HudTilePosition HudModel::toastPosition() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _toastPosition;
}

void HudModel::setIndicatorPosition(HudTilePosition pos)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_indicatorPosition == pos)
        return;
    _indicatorPosition = pos;
    for (auto& ind : _indicators)
    {
        ind.position = pos;
        ind.anchor = HudTilePositionToAnchor(pos);
    }
    publishLocked();
}

HudTilePosition HudModel::indicatorPosition() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _indicatorPosition;
}

void HudModel::setChangedCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _changedCallback = std::move(callback);
}

void HudModel::subscribeFeatureObserver()
{
    unsubscribeFeatureObserver();
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    _featureObserverId = mc.AddObserver(NC_FEATURE_CHANGED, [this](int id, Message* msg) {
        onFeatureNotification(id, msg);
    });
}

void HudModel::unsubscribeFeatureObserver()
{
    if (_featureObserverId != 0)
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        mc.RemoveObserverById(NC_FEATURE_CHANGED, _featureObserverId);
        _featureObserverId = 0;
    }
}

void HudModel::subscribeObservers()
{
    unsubscribeObservers();

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    auto add = [&](const std::string& topic, ObserverCallbackFunc cb) {
        uint64_t id = mc.AddObserver(topic, cb);
        _eventObserverIds.push_back({topic, id});
    };

    add(NC_FDD_STATE_CHANGED, [this](int id, Message* msg) { onFddState(id, msg); });
    add(NC_FDD_DISK_INSERTED, [this](int id, Message* msg) { onFddDisk(id, msg); });
    add(NC_FDD_DISK_EJECTED, [this](int id, Message* msg) { onFddDisk(id, msg); });
    add(NC_FDD_DISK_WRITTEN, [this](int id, Message* msg) { onFddDisk(id, msg); });
    add(NC_FDD_DISK_SAVE_RETARGETED, [this](int id, Message* msg) { onFddDisk(id, msg); });
    add(NC_FDD_DISK_PENDING_WRITE, [this](int id, Message* msg) { onFddDisk(id, msg); });
    add(NC_EMULATOR_STATE_CHANGE, [this](int id, Message* msg) { onEmulatorState(id, msg); });
    add(NC_EXECUTION_BREAKPOINT, [this](int id, Message* msg) { onBreakpoint(id, msg); });
    add(NC_EXECUTION_CPU_STEP, [this](int id, Message* msg) { onCpuStep(id, msg); });
    add(NC_SYSTEM_RESET, [this](int id, Message* msg) { onSystemReset(id, msg); });
    add(NC_SPEED_CHANGED, [this](int id, Message* msg) { onSpeedChanged(id, msg); });
    add(NC_RECORDING_STATE, [this](int id, Message* msg) { onRecording(id, msg); });
    add(NC_FILE_LOADED, [this](int id, Message* msg) { onFileLoaded(id, msg); });
}

void HudModel::unsubscribeObservers()
{
    if (_eventObserverIds.empty())
        return;

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    for (const auto& [topic, id] : _eventObserverIds)
    {
        mc.RemoveObserverById(topic, id);
    }
    _eventObserverIds.clear();
}

bool HudModel::matchesInstance(const unreal::UUID& id) const
{
    if (!_context || _context->emulatorId.isNil())
        return true;
    return id == _context->emulatorId;
}

void HudModel::publishLocked()
{
    if (_batchLevel > 0)
    {
        _batchDirty = true;
        return;
    }

    auto snapshot = std::make_shared<HudSnapshot>();
    snapshot->generation = ++_generation;
    snapshot->produced = HudClock::now();
    snapshot->themeId = _themeId;
    snapshot->scaleFactor = _scaleFactor;
    snapshot->toastPosition = _toastPosition;
    snapshot->indicatorPosition = _indicatorPosition;

    if (_enabled.load(std::memory_order_relaxed))
    {
        // 1. Augmented background / picture elements
        for (const auto& aug : _augmentations)
        {
            snapshot->elements.push_back(aug);
        }

        // 2. Indicators (active only)
        for (const auto& ind : _indicators)
        {
            if (ind.state != HudState::Off)
            {
                snapshot->elements.push_back(ind);
            }
        }

        // 3. Toasts: priority descending, then age ascending
        std::vector<HudElement> sortedToasts = _toasts;
        std::stable_sort(sortedToasts.begin(), sortedToasts.end(), [](const HudElement& a, const HudElement& b) {
            if (static_cast<uint8_t>(a.priority) != static_cast<uint8_t>(b.priority))
                return static_cast<uint8_t>(a.priority) > static_cast<uint8_t>(b.priority);
            return a.created < b.created;
        });

        size_t count = std::min(sortedToasts.size(), _visibleToasts);
        for (size_t i = 0; i < count; ++i)
        {
            snapshot->elements.push_back(std::move(sortedToasts[i]));
        }

        // Final sort by zIndex ascending (preserves stable order for identical zIndex)
        std::stable_sort(snapshot->elements.begin(), snapshot->elements.end(),
            [](const HudElement& a, const HudElement& b) {
                return a.zIndex < b.zIndex;
            });
    }

    std::atomic_store(&_publishedSnapshot, std::shared_ptr<const HudSnapshot>(snapshot));

    if (_changedCallback)
    {
        _changedCallback();
    }
}

void HudModel::onFeatureNotification(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<FeatureChangedPayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    if (p->featureId == Features::kHud)
    {
        onFeatureChanged(p->enabled);
    }
    else if (p->featureId.empty())
    {
        bool enabled = true;
        if (_context && _context->pFeatureManager)
        {
            enabled = _context->pFeatureManager->isEnabled(Features::kHud);
        }
        onFeatureChanged(enabled);
    }
}

void HudModel::onFddState(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<FDDStatePayload*>(message->obj);
    if (!p || !matchesInstance(p->_emulatorId))
        return;

    std::string key = "fdd";
    if (!p->_state.motorOn)
    {
        clearIndicator(key);
        return;
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%c: H:%u T:%02u S:%02u",
                  p->_state.getDriveLetter(),
                  p->_state.side ? 1 : 0,
                  p->_state.track,
                  p->_state.sector);

    setIndicator(key, HudState::Active, std::string(buf), "", "floppy", std::chrono::milliseconds(0), true);
}

void HudModel::onFddDisk(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<FDDDiskPayload*>(message->obj);
    if (!p || !matchesInstance(p->_emulatorId))
        return;

    char driveLetter = static_cast<char>('A' + (p->_driveId & 0x03));
    std::string driveTitle = std::string("Drive ") + driveLetter;
    std::string fname = std::filesystem::path(p->_diskPath).filename().string();

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    int tid = message->tid;

    if (tid == mc.ResolveTopic(NC_FDD_DISK_INSERTED))
    {
        HudToastRequest req;
        req.icon = "floppy";
        req.title = "Disk inserted to disk drive " + std::string(1, driveLetter);
        req.body = fname;
        req.dedupKey = std::string("fdd-disk/") + driveLetter;
        req.coalesceCount = false;
        req.priority = HudPriority::Normal;
        req.ttl = HudTiming::ToastDiskInserted;
        notify(req);
    }
    else if (tid == mc.ResolveTopic(NC_FDD_DISK_EJECTED))
    {
        HudToastRequest req;
        req.icon = "floppy";
        req.title = driveTitle;
        req.body = "Disk ejected";
        req.dedupKey = std::string("fdd-disk/") + driveLetter;
        req.coalesceCount = false;
        req.priority = HudPriority::Low;
        req.ttl = HudTiming::ToastDiskEjected;
        notify(req);
    }
    else if (tid == mc.ResolveTopic(NC_FDD_DISK_WRITTEN))
    {
        HudToastRequest req;
        req.icon = "floppy";
        req.title = driveTitle;
        req.body = fname.empty() ? "Disk saved" : ("Saved " + fname);
        req.dedupKey = std::string("disk-written/") + driveLetter;
        req.priority = HudPriority::Normal;
        req.ttl = HudTiming::ToastDiskWritten;
        notify(req);

        // Clear pending write alert
        clearIndicator("fdd");
    }
    else if (tid == mc.ResolveTopic(NC_FDD_DISK_SAVE_RETARGETED))
    {
        HudToastRequest req;
        req.icon = "floppy";
        req.title = driveTitle;
        req.body = p->_reason.empty() ? fname : (p->_reason + "\n" + fname);
        req.priority = HudPriority::High;
        req.ttl = HudTiming::ToastDiskSaveRetargeted;
        notify(req);
    }
    else if (tid == mc.ResolveTopic(NC_FDD_DISK_PENDING_WRITE))
    {
        setIndicator("fdd", HudState::Alert, "", "", "floppy");
    }
}

void HudModel::onEmulatorState(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<EmulatorStateChangePayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    uint32_t state = p->_payloadNumber;
    if (state == StatePaused)
    {
        setExecState(ExecState::Paused);
    }
    else if (state == StateRun || state == StateResumed)
    {
        if (canTransitionTo(ExecState::Execute))
            setExecState(ExecState::Execute, HudTiming::IndicatorExecuteTimeout);
    }
    else if (state == StateStopped)
    {
        setExecState(ExecState::Idle);
        HudToastRequest req;
        req.title = "Emulator stopped";
        req.priority = HudPriority::Normal;
        req.ttl = HudTiming::ToastEmulatorStopped;
        notify(req);
    }
}

void HudModel::onBreakpoint(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<BreakpointTriggeredPayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    // Filter hidden / stepover breakpoints — do not display on HUD
    bool isHidden = p->hidden;
    if (!isHidden && _context && _context->pDebugManager)
    {
        auto bpMgr = _context->pDebugManager->GetBreakpointsManager();
        if (bpMgr)
        {
            auto* bp = bpMgr->GetBreakpointById(static_cast<uint16_t>(p->_payloadNumber));
            if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
            {
                isHidden = true;
            }
        }
    }

    if (isHidden)
        return;

    // Breakpoint hit: only single notification on screen, no stacking.
    // Clear any existing toasts so breakpoint notification is exclusively displayed.
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _toasts.clear();
    }

    char addrBuf[48];
    std::snprintf(addrBuf, sizeof(addrBuf), "PC: #%04X (ID: %u)", p->address, p->_payloadNumber);

    HudToastRequest req;
    req.icon = "breakpoint";
    req.title = "Breakpoint Hit";
    req.body = addrBuf;
    req.priority = HudPriority::High;
    req.ttl = HudTiming::ToastBreakpointHit;
    req.dedupKey = "breakpoint";
    req.coalesceCount = false;
    notify(req);

    setExecState(ExecState::Breakpoint);
}

void HudModel::onCpuStep(int, Message* message)
{
    bool isPaused = true;
    if (message && message->obj)
    {
        if (auto* statePayload = dynamic_cast<EmulatorStateChangePayload*>(message->obj))
        {
            if (!matchesInstance(statePayload->emulatorId))
                return;
            isPaused = (statePayload->_payloadNumber == StatePaused);
        }
    }
    else if (_context && _context->pEmulator)
    {
        isPaused = _context->pEmulator->IsPaused();
    }

    if (isPaused)
    {
        setExecState(ExecState::Paused);
    }
    else
    {
        if (canTransitionTo(ExecState::Execute))
            setExecState(ExecState::Execute, HudTiming::IndicatorExecuteTimeout);
    }
}

void HudModel::onSystemReset(int, Message*)
{
    setExecState(ExecState::Reset, HudTiming::ToastSystemReset);
}

void HudModel::onSpeedChanged(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<SpeedChangedPayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    if (p->turboMode)
    {
        setIndicator("speed", HudState::Active, "TURBO");
    }
    else if (p->multiplier > 1)
    {
        setIndicator("speed", HudState::Active, std::to_string(p->multiplier) + "x");
    }
    else
    {
        setIndicator("speed", HudState::Off);
    }
}

void HudModel::onRecording(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<RecordingStatePayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    if (p->recording)
    {
        setIndicator("rec", HudState::Active, "REC");
    }
    else
    {
        setIndicator("rec", HudState::Off);
        std::string fname = std::filesystem::path(p->path).filename().string();
        HudToastRequest req;
        req.icon = "rec";
        req.title = "Recording saved";
        req.body = fname.empty() ? p->path : fname;
        req.priority = HudPriority::Normal;
        req.ttl = HudTiming::ToastRecordingSaved;
        notify(req);
    }
}

void HudModel::onFileLoaded(int, Message* message)
{
    if (!message)
        return;
    auto* p = dynamic_cast<FileLoadedPayload*>(message->obj);
    if (!p || !matchesInstance(p->emulatorId))
        return;

    std::string fname = std::filesystem::path(p->path).filename().string();
    HudToastRequest req;

    if (p->kind == "tape")
    {
        req.icon = "tape";
        if (p->ok)
        {
            req.title = "Tape Loaded";
            req.body = fname.empty() ? p->path : fname;
            req.priority = HudPriority::Normal;
            req.ttl = HudTiming::ToastTapeLoaded;
        }
        else
        {
            req.title = "Tape Load Failed";
            req.body = fname.empty() ? p->path : fname;
            req.priority = HudPriority::High;
            req.ttl = HudTiming::ToastTapeLoadFailed;
        }
    }
    else if (p->kind == "disk")
    {
        if (p->ok)
        {
            // Disk insertion is already notified via NC_FDD_DISK_INSERTED with drive letter and filename.
            // Suppress redundant generic "Disk Loaded" toast.
            return;
        }
        else
        {
            req.icon = "floppy";
            req.title = "Disk Load Failed";
            req.body = fname.empty() ? p->path : fname;
            req.priority = HudPriority::High;
            req.ttl = HudTiming::ToastFileLoadFailed;
        }
    }
    else
    {
        req.icon = "file";
        req.dedupKey = "system-reset";
        req.coalesceCount = false;
        if (p->ok)
        {
            req.title = "Snapshot Loaded";
            req.body = fname.empty() ? p->path : fname;
            req.priority = HudPriority::Normal;
            req.ttl = HudTiming::ToastFileLoaded;
        }
        else
        {
            req.title = "Snapshot Load Failed";
            req.body = fname.empty() ? p->path : fname;
            req.priority = HudPriority::High;
            req.ttl = HudTiming::ToastFileLoadFailed;
        }

    }
    notify(req);
}

// --- Execution State Machine ---

void HudModel::setExecState(ExecState state, std::chrono::milliseconds ttl)
{
    _execState = state;
    if (ttl.count() > 0)
        _execStateExpiry = std::chrono::steady_clock::now() + ttl;
    else
        _execStateExpiry = std::chrono::steady_clock::time_point{};

    static const char* labels[] = {"", "PAUSE", "EXECUTE", "RESET", "BREAKPOINT"};
    const char* label = labels[static_cast<int>(state)];

    if (state == ExecState::Idle)
    {
        clearIndicator("pause");
    }
    else
    {
        setIndicator("pause", HudState::Active, label, "", "", ttl);
    }
}

bool HudModel::canTransitionTo(ExecState newState) const
{
    // Timed states (Reset, Execute) block lower-priority transitions until expired
    if (_execStateExpiry != std::chrono::steady_clock::time_point{})
    {
        if (std::chrono::steady_clock::now() < _execStateExpiry)
        {
            // Reset blocks Execute; Breakpoint always wins
            if (_execState == ExecState::Reset && newState == ExecState::Execute)
                return false;
        }
    }
    return true;
}
