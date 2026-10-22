#include "emulator/actions/targetstate.h"

#include <algorithm>
#include <utility>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace actions
{

/// region <Pins>

const char* Pin::ModeName() const
{
    switch (kind)
    {
        case Kind::Follow: return "follow";
        case Kind::View: return "view";
        case Kind::Instance: return "instance";
    }
    return "follow";
}

std::string Pin::ToText() const
{
    return kind == Kind::Follow ? std::string("follow") : std::string(ModeName()) + ":" + id;
}

bool Pin::FromText(std::string_view text, Pin& pin, std::string& error)
{
    if (text.empty() || text == "follow")
    {
        pin = Pin::Follow();
        return true;
    }
    const size_t colon = text.find(':');
    const std::string_view kind = text.substr(0, colon);
    const std::string id = colon == std::string_view::npos ? std::string() : std::string(text.substr(colon + 1));
    if ((kind == "view" || kind == "instance") && !id.empty())
    {
        pin = kind == "view" ? Pin::OnView(id) : Pin::OnInstance(id);
        return true;
    }
    error = "target '" + std::string(text) + "': follow, view:<id> or instance:<id>";
    return false;
}

const char* FollowSourceName(FollowSource source)
{
    switch (source)
    {
        case FollowSource::View: return "view";
        case FollowSource::Selected: return "selected";
        case FollowSource::Sole: return "sole";
        case FollowSource::None: return "none";
    }
    return "none";
}

/// endregion </Pins>

/// region <Handle>

TargetHandle::TargetHandle(TargetHandle&& other) noexcept
    : _state(other._state), _kind(other._kind), _id(std::move(other._id))
{
    other._state = nullptr;
}

TargetHandle& TargetHandle::operator=(TargetHandle&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        _state = other._state;
        _kind = other._kind;
        _id = std::move(other._id);
        other._state = nullptr;
    }
    return *this;
}

TargetHandle::~TargetHandle()
{
    Reset();
}

void TargetHandle::Reset()
{
    if (_state != nullptr)
        _state->Unregister(_kind, _id);
    _state = nullptr;
}

/// endregion </Handle>

/// region <State>

namespace
{
std::pair<std::string, FollowSource> ManagerFallback()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string selected = manager->GetSelectedEmulatorId();
    if (!selected.empty() && manager->GetEmulator(selected))
        return {selected, FollowSource::Selected};
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    if (ids.size() == 1)
        return {ids.front(), FollowSource::Sole};
    return {std::string(), FollowSource::None};
}
}  // namespace

TargetState& TargetState::Instance()
{
    // Never destroyed: the static EmulatorManager reports instances gone in its own destructor at exit
    static TargetState* state = [] {
        auto* created = new TargetState({}, true, {});
        // Rule 4 reads the selection and the instance list: follow them (the observers stay for the process)
        MessageCenter& center = MessageCenter::DefaultMessageCenter();
        for (const char* topic : {NC_EMULATOR_SELECTION_CHANGED, NC_EMULATOR_INSTANCE_CREATED})
            center.AddObserver(topic, ObserverCallbackFunc([created](int, Message*) { created->Refresh(); }));
        return created;
    }();
    return *state;
}

TargetState::TargetState(Fallback fallback, bool post, Exists exists)
    : _fallback(std::move(fallback)), _exists(std::move(exists)), _post(post)
{
    if (!_fallback)
        _fallback = ManagerFallback;
    if (!_exists)
        _exists = [](const std::string& id) { return EmulatorManager::GetInstance()->GetEmulator(id) != nullptr; };
}

TargetState::~TargetState() = default;

std::pair<std::string, FollowSource> TargetState::FallbackNow() const
{
    return _fallback ? _fallback() : std::pair<std::string, FollowSource>{std::string(), FollowSource::None};
}

TargetInfo TargetState::FollowedLocked(const std::pair<std::string, FollowSource>& fallback) const
{
    TargetInfo target;
    for (const ViewEntry& view : _views)
    {
        if (view.info.activeInstance.empty())
            continue;  // rule 3: a view without an active instance is skipped
        target.instance = view.info.activeInstance;
        target.view = view.info.id;
        target.viewName = view.info.name;
        target.source = FollowSource::View;
        return target;
    }
    target.instance = fallback.first;
    target.source = fallback.first.empty() ? FollowSource::None : fallback.second;
    return target;
}

TargetInfo TargetState::ResolveLocked(const Pin& pin, const std::pair<std::string, FollowSource>& fallback) const
{
    if (pin.kind == Pin::Kind::Follow)
        return FollowedLocked(fallback);
    TargetInfo target;
    target.pin = pin;
    if (pin.kind == Pin::Kind::View)
    {
        for (const ViewEntry& view : _views)
            if (view.info.id == pin.id)
            {
                target.instance = view.info.activeInstance;
                target.view = view.info.id;
                target.viewName = view.info.name;
                target.source = FollowSource::View;
            }
        return target;
    }
    target.instance = pin.id;
    target.source = FollowSource::None;
    // The view that shows it, front first
    for (const ViewEntry& view : _views)
        if (view.info.activeInstance == pin.id)
        {
            target.view = view.info.id;
            target.viewName = view.info.name;
            target.source = FollowSource::View;
            break;
        }
    return target;
}

TargetState::Snapshot TargetState::SnapshotLocked(const std::pair<std::string, FollowSource>& fallback) const
{
    Snapshot snapshot;
    snapshot.followed = FollowedLocked(fallback).instance;
    snapshot.views.reserve(_views.size());
    for (const ViewEntry& view : _views)
        snapshot.views.push_back(view.info);
    for (const SessionEntry& session : _sessions)
        snapshot.targets[session.id] = ResolveLocked(session.pin, fallback);
    return snapshot;
}

void TargetState::Publish(const char* reason)
{
    std::lock_guard<std::mutex> publishing(_publishMutex);
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::string followed;
    std::string previous;
    bool viewsChanged = false;
    std::vector<std::string> moved;
    uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Snapshot now = SnapshotLocked(fallback);
        auto sameView = [](const ViewInfo& a, const ViewInfo& b) {
            return a.id == b.id && a.name == b.name && a.activeInstance == b.activeInstance;
        };
        viewsChanged = now.views.size() != _last.views.size() ||
                       !std::equal(now.views.begin(), now.views.end(), _last.views.begin(), sameView);
        for (const auto& [id, target] : now.targets)
        {
            auto it = _last.targets.find(id);
            if (it == _last.targets.end() || it->second != target)
                moved.push_back(id);
        }
        const bool closed = std::any_of(_last.targets.begin(), _last.targets.end(),
                                        [&now](const auto& entry) { return now.targets.count(entry.first) == 0; });
        if (now.followed == _last.followed && !viewsChanged && moved.empty() && !closed)
            return;
        followed = now.followed;
        previous = _last.followed;
        _last = std::move(now);
        revision = ++_revision;
    }
    if (_post)
        MessageCenter::DefaultMessageCenter().Post(
            NC_SURFACE_TARGET,
            new SurfaceTargetPayload(followed, previous, viewsChanged, std::move(moved), reason, revision));
}

ViewHandle TargetState::RegisterView(std::string name, std::string owner)
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        id = "v" + std::to_string(_nextView++);
        ViewEntry entry;
        entry.info.id = id;
        entry.info.name = name.empty() ? "Window " + id.substr(1) : std::move(name);
        entry.info.owner = std::move(owner);
        _views.push_back(std::move(entry));
    }
    Publish("view");
    return ViewHandle(this, TargetHandle::Kind::View, id);
}

void TargetState::SetActive(const ViewHandle& view, std::string instanceId)
{
    if (view._state == this)
        SetActiveById(view.Id(), std::move(instanceId));
}

void TargetState::Activated(const ViewHandle& view)
{
    if (view._state == this)
        ActivatedById(view.Id());
}

bool TargetState::SetActiveById(const std::string& viewId, std::string instanceId)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find_if(_views.begin(), _views.end(), [&](const ViewEntry& v) { return v.info.id == viewId; });
        if (it == _views.end())
            return false;
        it->info.activeInstance = std::move(instanceId);
    }
    Publish("active");
    return true;
}

bool TargetState::ActivatedById(const std::string& viewId)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find_if(_views.begin(), _views.end(), [&](const ViewEntry& v) { return v.info.id == viewId; });
        if (it == _views.end())
            return false;
        std::rotate(_views.begin(), it, it + 1);  // to the front, the others keep their order
    }
    Publish("focus");
    return true;
}

std::vector<ViewInfo> TargetState::Views() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<ViewInfo> out;
    out.reserve(_views.size());
    for (const ViewEntry& view : _views)
        out.push_back(view.info);
    return out;
}

std::optional<ViewInfo> TargetState::View(const std::string& viewId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const ViewEntry& view : _views)
        if (view.info.id == viewId)
            return view.info;
    return std::nullopt;
}

bool TargetState::HasViews() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_views.empty();
}

std::string TargetState::Followed() const
{
    return FollowedTarget().instance;
}

TargetInfo TargetState::FollowedTarget() const
{
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::lock_guard<std::mutex> lock(_mutex);
    return FollowedLocked(fallback);
}

TargetInfo TargetState::Resolve(const Pin& pin) const
{
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::lock_guard<std::mutex> lock(_mutex);
    return ResolveLocked(pin, fallback);
}

SessionHandle TargetState::OpenSession(SurfaceSessionSpec spec, Pin pin)
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        id = "s" + std::to_string(_nextSession++);
        SessionEntry entry;
        entry.id = id;
        entry.spec = std::move(spec);
        const bool knownView =
            pin.kind != Pin::Kind::View ||
            std::any_of(_views.begin(), _views.end(), [&](const ViewEntry& v) { return v.info.id == pin.id; });
        entry.pin = knownView ? std::move(pin) : Pin::Follow();
        if (!knownView)
            entry.notice = "no view '" + pin.id + "': following the focus";
        _sessions.push_back(std::move(entry));
    }
    Publish("session");
    return SessionHandle(this, TargetHandle::Kind::Session, id);
}

bool TargetState::SetPin(const std::string& sessionId, const Pin& pin, std::string& error, std::string& message)
{
    // The manager has its own lock: asked before ours. A removal racing with the pin falls back like any removal
    if (pin.kind == Pin::Kind::Instance && !(_exists && _exists(pin.id)))
    {
        error = "Not Found";
        message = "no instance '" + pin.id + "'";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto session = std::find_if(_sessions.begin(), _sessions.end(),
                                    [&](const SessionEntry& s) { return s.id == sessionId; });
        if (session == _sessions.end())
        {
            error = "Not Found";
            message = "no surface session '" + sessionId + "'";
            return false;
        }
        if (pin.kind == Pin::Kind::View)
        {
            if (_views.empty())
            {
                error = "not_now";
                message = "the host has no views (headless): pin an instance, or follow";
                return false;
            }
            if (std::none_of(_views.begin(), _views.end(), [&](const ViewEntry& v) { return v.info.id == pin.id; }))
            {
                error = "Not Found";
                message = "no view '" + pin.id + "'";
                return false;
            }
        }
        session->pin = pin;
        session->notice.clear();
    }
    Publish("pin");
    return true;
}

std::optional<SurfaceSessionInfo> TargetState::Session(const std::string& sessionId) const
{
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::lock_guard<std::mutex> lock(_mutex);
    for (const SessionEntry& session : _sessions)
        if (session.id == sessionId)
            return SurfaceSessionInfo{session.id, session.spec, session.pin, ResolveLocked(session.pin, fallback),
                                      session.notice};
    return std::nullopt;
}

std::vector<SurfaceSessionInfo> TargetState::Sessions() const
{
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<SurfaceSessionInfo> out;
    out.reserve(_sessions.size());
    for (const SessionEntry& session : _sessions)
        out.push_back({session.id, session.spec, session.pin, ResolveLocked(session.pin, fallback), session.notice});
    return out;
}

std::optional<TargetInfo> TargetState::TargetFor(const std::string& sessionId) const
{
    const std::pair<std::string, FollowSource> fallback = FallbackNow();
    std::lock_guard<std::mutex> lock(_mutex);
    for (const SessionEntry& session : _sessions)
        if (session.id == sessionId)
            return ResolveLocked(session.pin, fallback);
    return std::nullopt;
}

void TargetState::Unregister(TargetHandle::Kind kind, const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (kind == TargetHandle::Kind::Session)
        {
            _sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(),
                                           [&](const SessionEntry& s) { return s.id == id; }),
                            _sessions.end());
        }
        else
        {
            auto it = std::find_if(_views.begin(), _views.end(), [&](const ViewEntry& v) { return v.info.id == id; });
            if (it == _views.end())
                return;
            const std::string name = it->info.name;
            _views.erase(it);
            // `view:` pins on it fall back to following, with a notice (targeting.md §5)
            for (SessionEntry& session : _sessions)
                if (session.pin.kind == Pin::Kind::View && session.pin.id == id)
                {
                    session.pin = Pin::Follow();
                    session.notice = "the view '" + name + "' closed: following the focus";
                }
        }
    }
    Publish(kind == TargetHandle::Kind::Session ? "session" : "view");
}

void TargetState::InstanceGone(const std::string& instanceId)
{
    if (instanceId.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (ViewEntry& view : _views)
            if (view.info.activeInstance == instanceId)
                view.info.activeInstance.clear();
        for (SessionEntry& session : _sessions)
            if (session.pin.kind == Pin::Kind::Instance && session.pin.id == instanceId)
            {
                session.pin = Pin::Follow();
                session.notice = "the instance is gone: following the focus";
            }
    }
    Publish("instance");
}

void TargetState::InstanceReplaced(const std::string& oldId, const std::string& newId)
{
    if (oldId.empty() || newId.empty() || oldId == newId)
        return;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (ViewEntry& view : _views)
            if (view.info.activeInstance == oldId)
                view.info.activeInstance = newId;
        for (SessionEntry& session : _sessions)
            if (session.pin.kind == Pin::Kind::Instance && session.pin.id == oldId)
                session.pin.id = newId;
    }
    Publish("instance");
}

void TargetState::Refresh()
{
    Publish("selection");
}

uint64_t TargetState::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

/// endregion </State>

}  // namespace actions
