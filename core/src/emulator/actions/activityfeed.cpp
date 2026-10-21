#include "emulator/actions/activityfeed.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include "common/logger.h"

namespace actions
{

const char* ActivityOriginName(execution::Origin origin)
{
    using execution::Origin;
    switch (origin)
    {
        case Origin::Gui: return "gui";
        case Origin::HostDevice: return "host_device";
        case Origin::Companion: return "companion";
        case Origin::WebApi: return "webapi";
        case Origin::Mcp: return "mcp";
        case Origin::Cli: return "cli";
        case Origin::Lua: return "lua";
        case Origin::Python: return "python";
        default: return execution::OriginName(origin);
    }
}

StateNode ActivityValueNode(const ActionValue& value)
{
    StateNode node = StateNode::Object();
    switch (value.kind)
    {
        case ValueKind::Press: node["p"] = value.pressed; break;
        case ValueKind::Delta:
            node["d"] = value.delta;
            node["rate"] = value.rate;
            break;
        case ValueKind::Level: node["level"] = value.level; break;
        case ValueKind::Text: node["text"] = value.text; break;
    }
    if (value.params.kind == StateNode::Kind::Object && !value.params.members.empty())
        node["params"] = value.params;
    return node;
}

StateNode ActivityEntryNode(const ActivityEntry& entry)
{
    StateNode node = StateNode::Object();
    node["seq"] = static_cast<int64_t>(entry.seq);
    node["instance"] = entry.instance;
    node["id"] = entry.id;
    node["value"] = ActivityValueNode(entry.value);
    node["origin"] = ActivityOriginName(entry.origin);
    node["surface"] = entry.surface;
    node["ok"] = entry.ok;
    if (!entry.ok)
        node["error"] = entry.error;
    if (!entry.message.empty())
        node["message"] = entry.message;
    node["t_ms"] = entry.tMs;
    node["merged"] = static_cast<int64_t>(entry.merged);
    return node;
}

namespace
{
std::string SurfaceName(const ActionOrigin& origin)
{
    if (!origin.client.empty())
        return origin.client;
    switch (origin.origin)
    {
        case execution::Origin::Gui: return "GUI";
        case execution::Origin::Lua: return "Lua";
        case execution::Origin::Python: return "Python";
        default: break;
    }
    return origin.identity.empty() ? std::string(ActivityOriginName(origin.origin)) : origin.identity;
}
}  // namespace

ActivityFeed& ActivityFeed::Instance()
{
    static ActivityFeed* feed = new ActivityFeed();  // never destroyed: workers may record at exit
    return *feed;
}

ActivityFeed::ActivityFeed(Clock clock) : _clock(std::move(clock))
{
    if (!_clock)
        _clock = [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        };
}

void ActivityFeed::Record(const std::string& instance, const ActionInfo& info, const ActionValue& value,
                          const ActionOrigin& origin, const ActionResult& result)
{
    const int64_t now = _clock();
    ActivityEntry recorded;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        bool merged = false;
        if (value.kind == ValueKind::Delta && !_entries.empty())
        {
            ActivityEntry& last = _entries.back();
            if (last.kind == ValueKind::Delta && last.id == info.id && last.instance == instance &&
                last.origin == origin.origin && last.identity == origin.identity && last.session == origin.session &&
                now - last.firstMs <= kMergeMs)
            {
                last.value.delta += value.delta;
                last.value.rate = std::max(last.value.rate, value.rate);
                last.seq = _nextSeq++;
                last.ok = result.ok;
                last.error = result.error;
                last.message = result.message;
                last.tMs = now;
                last.merged++;
                recorded = last;
                merged = true;
            }
        }
        if (!merged)
        {
            ActivityEntry entry;
            entry.seq = _nextSeq++;
            entry.instance = instance;
            entry.id = info.id;
            entry.kind = value.kind;
            entry.value = value;
            entry.origin = origin.origin;
            entry.identity = origin.identity;
            entry.session = origin.session;
            entry.surface = SurfaceName(origin);
            entry.ok = result.ok;
            entry.error = result.error;
            entry.message = result.message;
            entry.tMs = now;
            entry.firstMs = now;
            _entries.push_back(entry);
            recorded = std::move(entry);
            while (_entries.size() > kCapacity)
            {
                _evicted.push_back(_entries.front().seq);
                _entries.pop_front();
                if (_evicted.size() > kEvictedKept)
                    _evicted.pop_front();
            }
        }
    }
    std::vector<std::shared_ptr<Listener>> listeners;
    {
        std::lock_guard<std::mutex> lock(_listenersMutex);
        for (const auto& [id, listener] : _listeners)
            listeners.push_back(listener);
    }
    for (const std::shared_ptr<Listener>& listener : listeners)
    {
        try
        {
            (*listener)(recorded);
        }
        catch (const std::exception& e)
        {
            LOGWARNING("ActivityFeed: a listener threw: %s", e.what());
        }
    }
}

ActivityFeed::Page ActivityFeed::Since(uint64_t since, const std::string& instance, size_t limit) const
{
    Page page;
    std::lock_guard<std::mutex> lock(_mutex);
    page.last = _nextSeq - 1;
    for (const ActivityEntry& entry : _entries)
        if (entry.seq > since && (instance.empty() || entry.instance == instance))
            page.entries.push_back(entry);
    // A merge moves an entry to the end with a new seq: the order of the deque is the order of the seqs
    if (limit > 0 && page.entries.size() > limit)
        page.entries.erase(page.entries.begin(), page.entries.end() - static_cast<std::ptrdiff_t>(limit));
    page.dropped = static_cast<uint64_t>(_evicted.end() - std::upper_bound(_evicted.begin(), _evicted.end(), since));
    return page;
}

uint64_t ActivityFeed::LastSeq() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _nextSeq - 1;
}

uint64_t ActivityFeed::AddListener(Listener listener)
{
    std::lock_guard<std::mutex> lock(_listenersMutex);
    const uint64_t id = _nextListener++;
    _listeners[id] = std::make_shared<Listener>(std::move(listener));
    return id;
}

void ActivityFeed::RemoveListener(uint64_t id)
{
    std::lock_guard<std::mutex> lock(_listenersMutex);
    _listeners.erase(id);
}

void ActivityFeed::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.clear();
    _evicted.clear();
}

}  // namespace actions
