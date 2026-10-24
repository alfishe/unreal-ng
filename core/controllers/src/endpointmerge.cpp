#include "endpointmerge.h"

#include <algorithm>
#include <cctype>

namespace controllers
{

namespace
{
std::string Folded(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    std::string out;
    out.reserve(end - begin);
    for (size_t i = begin; i < end; ++i)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
    return out;
}

uint64_t Distance(uint64_t a, uint64_t b)
{
    return a > b ? a - b : b - a;
}

template <typename T>
void KeepLast(std::vector<T>& items, size_t count)
{
    if (items.size() > count)
        items.erase(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(items.size() - count));
}
}  // namespace

bool EndpointMerge::NamesMatch(const std::string& vendorName, const std::string& productName)
{
    const std::string a = Folded(vendorName);
    const std::string b = Folded(productName);
    if (a.empty() || b.empty())
        return false;
    return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

/// region <Raw HID endpoints>

void EndpointMerge::Order(Device& device)
{
    // The most controls first; stable, so the earlier endpoint keeps the input on a tie
    std::stable_sort(device.endpoints.begin(), device.endpoints.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
}

EndpointMerge::HidAdded EndpointMerge::AddHid(const HidEndpointInfo& endpoint)
{
    if (!endpoint.physical.empty())
        for (auto& [id, device] : _devices)
            if (device.physical == endpoint.physical)
            {
                device.endpoints.emplace_back(endpoint.key, endpoint.controls);
                Order(device);
                _endpoints[endpoint.key] = id;
                return {id, false};
            }
    Device device;
    device.physical = endpoint.physical;
    device.name = endpoint.name;
    device.arrivalMs = endpoint.arrivalMs;
    device.endpoints.emplace_back(endpoint.key, endpoint.controls);
    _devices[endpoint.id] = std::move(device);
    _endpoints[endpoint.key] = endpoint.id;
    // A standalone companion of its name: held back from now on, its presses counted afresh for the match (the ones
    // of its standalone time had no raw device to coincide with)
    for (auto& [key, companion] : _companions)
    {
        if (!companion.standalone || !companion.device.empty() || !NamesMatch(companion.vendorName, endpoint.name))
            continue;
        if (companion.shadowedBy.empty())
        {
            companion.edges.clear();
            companion.presses = 0;
            companion.hits.clear();
            companion.shadowPresses = 0;
        }
        companion.shadowedBy.push_back(endpoint.id);
    }
    return {endpoint.id, true};
}

EndpointMerge::HidRemoved EndpointMerge::RemoveHid(const std::string& key, uint64_t nowMs)
{
    const auto found = _endpoints.find(key);
    if (found == _endpoints.end())
        return {};
    const HostDeviceId id = found->second;
    _endpoints.erase(found);
    Device& device = _devices[id];
    device.endpoints.erase(std::remove_if(device.endpoints.begin(), device.endpoints.end(),
                                          [&key](const auto& entry) { return entry.first == key; }),
                           device.endpoints.end());
    if (!device.endpoints.empty())
        return {id, false};
    // The companion stays: it may match the device when it comes back
    if (!device.companion.empty())
        if (const auto companion = _companions.find(device.companion); companion != _companions.end())
        {
            companion->second.device.clear();
            if (nowMs != 0)
                companion->second.arrivalMs = nowMs;
        }
    for (auto& [companionKey, companion] : _companions)
    {
        companion.shadowedBy.erase(std::remove(companion.shadowedBy.begin(), companion.shadowedBy.end(), id),
                                   companion.shadowedBy.end());
        companion.hits.erase(id);
        for (CompanionEdgeRecord& record : companion.edges)
            record.hit.erase(std::remove(record.hit.begin(), record.hit.end(), id), record.hit.end());
    }
    _devices.erase(id);
    return {id, true};
}

bool EndpointMerge::IsInput(const std::string& key) const
{
    const auto found = _endpoints.find(key);
    if (found == _endpoints.end())
        return false;
    const auto device = _devices.find(found->second);
    return device != _devices.end() && !device->second.endpoints.empty() &&
           device->second.endpoints.front().first == key;
}

std::vector<std::string> EndpointMerge::EndpointsOf(const HostDeviceId& device) const
{
    std::vector<std::string> keys;
    if (const auto found = _devices.find(device); found != _devices.end())
        for (const auto& [key, controls] : found->second.endpoints)
            keys.push_back(key);
    return keys;
}

/// endregion </Raw HID endpoints>

/// region <Companion endpoints>

void EndpointMerge::AddCompanion(const CompanionInfo& companion)
{
    Companion entry;
    entry.vendorName = companion.vendorName;
    entry.arrivalMs = companion.arrivalMs;
    _companions[companion.key] = std::move(entry);
}

void EndpointMerge::RemoveCompanion(const std::string& key)
{
    const auto found = _companions.find(key);
    if (found == _companions.end())
        return;
    if (const auto device = _devices.find(found->second.device); device != _devices.end())
        device->second.companion.clear();
    _companions.erase(found);
}

std::vector<HostDeviceId> EndpointMerge::CandidatesLocked(const Companion& companion) const
{
    // The unclaimed devices of its name; any unclaimed device when none has it (a name the OS spells differently):
    // the presses decide then
    std::vector<HostDeviceId> named;
    std::vector<HostDeviceId> unclaimed;
    for (const auto& [id, device] : _devices)
    {
        if (!device.companion.empty())
            continue;
        unclaimed.push_back(id);
        if (NamesMatch(companion.vendorName, device.name))
            named.push_back(id);
    }
    return named.empty() ? unclaimed : named;
}

std::optional<EndpointMerge::Confirmed> EndpointMerge::ConfirmLocked(const std::string& key, Companion& companion)
{
    if (!companion.device.empty())
        return std::nullopt;
    HostDeviceId best;
    int bestHits = 0;
    int secondHits = 0;
    for (const HostDeviceId& candidate : CandidatesLocked(companion))
    {
        const auto hits = companion.hits.find(candidate);
        const int count = hits == companion.hits.end() ? 0 : hits->second;
        if (count > bestHits)
        {
            secondHits = bestHits;
            bestHits = count;
            best = candidate;
        }
        else if (count > secondHits)
            secondHits = count;
    }
    // Enough coincidences, more than any other candidate, and with most of the companion's presses
    if (best.empty() || bestHits < kConfirmEdges || bestHits <= secondHits || bestHits * 2 <= companion.presses)
        return std::nullopt;
    const bool promoted = companion.standalone;
    companion.standalone = false;
    companion.shadowedBy.clear();
    companion.device = best;
    companion.edges.clear();
    companion.presses = 0;
    companion.hits.clear();
    _devices[best].companion = key;
    _devices[best].edges.clear();
    return Confirmed{key, best, promoted};
}

std::optional<EndpointMerge::Confirmed> EndpointMerge::HidEdge(const HostDeviceId& device, uint64_t timeMs)
{
    const auto found = _devices.find(device);
    if (found == _devices.end() || !found->second.companion.empty())
        return std::nullopt;
    bool waiting = false;
    for (const auto& [key, companion] : _companions)
        waiting = waiting || companion.device.empty();
    if (!waiting)
        return std::nullopt;  // nothing to correlate: no work per press once every companion is matched
    found->second.edges.push_back(timeMs);
    KeepLast(found->second.edges, kKeptEdges);
    // A companion press that came first (its daemon was quicker than our report) counts now
    for (auto& [key, companion] : _companions)
    {
        if (!companion.device.empty())
            continue;
        const std::vector<HostDeviceId> candidates = CandidatesLocked(companion);
        if (std::find(candidates.begin(), candidates.end(), device) == candidates.end())
            continue;
        for (CompanionEdgeRecord& record : companion.edges)
            if (Distance(record.timeMs, timeMs) <= kEdgeWindowMs &&
                std::find(record.hit.begin(), record.hit.end(), device) == record.hit.end())
            {
                record.hit.push_back(device);
                ++companion.hits[device];
            }
        if (auto confirmed = ConfirmLocked(key, companion))
            return confirmed;
    }
    return std::nullopt;
}

std::optional<EndpointMerge::Confirmed> EndpointMerge::CompanionEdge(const std::string& key, uint64_t timeMs)
{
    const auto found = _companions.find(key);
    if (found == _companions.end() || !found->second.device.empty())
        return std::nullopt;
    Companion& companion = found->second;
    CompanionEdgeRecord record;
    record.timeMs = timeMs;
    for (const HostDeviceId& candidate : CandidatesLocked(companion))
    {
        const std::vector<uint64_t>& edges = _devices[candidate].edges;
        if (std::any_of(edges.begin(), edges.end(),
                        [timeMs](uint64_t edge) { return Distance(edge, timeMs) <= kEdgeWindowMs; }))
        {
            record.hit.push_back(candidate);
            ++companion.hits[candidate];
        }
    }
    companion.edges.push_back(std::move(record));
    ++companion.presses;
    // The hits stay counted when an old record goes; only the late presses look back this far
    KeepLast(companion.edges, kKeptEdges);
    std::optional<Confirmed> confirmed = ConfirmLocked(key, companion);
    if (!confirmed && !companion.shadowedBy.empty() && ++companion.shadowPresses >= kShadowPresses &&
        std::none_of(companion.shadowedBy.begin(), companion.shadowedBy.end(),
                     [&companion](const HostDeviceId& device) { return companion.hits.count(device) != 0; }))
        companion.shadowedBy.clear();  // another pad of its name: the companion gives its input again
    return confirmed;
}

std::vector<std::string> EndpointMerge::StandaloneDue(uint64_t nowMs)
{
    std::vector<std::string> due;
    for (auto& [key, companion] : _companions)
    {
        if (companion.standalone || !companion.device.empty() || nowMs < companion.arrivalMs + kArrivalWindowMs)
            continue;
        // A raw device of its name, or a nameless one that arrived with it. A device of another name does not hold
        // it back (at the start everything arrives together); if it is the pad after all, the presses promote
        const bool candidate = std::any_of(_devices.begin(), _devices.end(), [&companion](const auto& entry) {
            const Device& device = entry.second;
            return device.companion.empty() &&
                   (NamesMatch(companion.vendorName, device.name) ||
                    (device.name.empty() && Distance(companion.arrivalMs, device.arrivalMs) <= kArrivalWindowMs));
        });
        if (candidate)
            continue;
        companion.standalone = true;
        due.push_back(key);
    }
    return due;
}

bool EndpointMerge::IsStandalone(const std::string& companion) const
{
    const auto found = _companions.find(companion);
    return found != _companions.end() && found->second.standalone;
}

bool EndpointMerge::IsShadowed(const std::string& companion) const
{
    const auto found = _companions.find(companion);
    return found != _companions.end() && found->second.standalone && !found->second.shadowedBy.empty();
}

EndpointMerge::MatchState EndpointMerge::StateOf(const HostDeviceId& device) const
{
    const auto found = _devices.find(device);
    if (found == _devices.end())
        return MatchState::None;
    if (!found->second.companion.empty())
        return MatchState::Confirmed;
    for (const auto& [key, companion] : _companions)
    {
        if (!companion.device.empty() || Distance(companion.arrivalMs, found->second.arrivalMs) > kArrivalWindowMs)
            continue;
        const std::vector<HostDeviceId> candidates = CandidatesLocked(companion);
        if (std::find(candidates.begin(), candidates.end(), device) != candidates.end())
            return MatchState::Tentative;
    }
    return MatchState::None;
}

std::string EndpointMerge::CompanionOf(const HostDeviceId& device) const
{
    const auto found = _devices.find(device);
    return found == _devices.end() ? std::string() : found->second.companion;
}

HostDeviceId EndpointMerge::DeviceOf(const std::string& companion) const
{
    const auto found = _companions.find(companion);
    return found == _companions.end() ? HostDeviceId() : found->second.device;
}

/// endregion </Companion endpoints>

}  // namespace controllers
