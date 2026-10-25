#include "joyconpairing.h"

#include <algorithm>

namespace controllers
{

HostDeviceId JoyConPairing::PairId(const HostDeviceId& left, const HostDeviceId& right)
{
    return left + "+" + right;
}

BatteryState JoyConPairing::Battery(const BatteryState& left, const BatteryState& right)
{
    if (!left.known)
        return right;
    if (!right.known)
        return left;
    BatteryState battery;
    battery.known = true;
    battery.level = left.level < 0 ? right.level : (right.level < 0 ? left.level : std::min(left.level, right.level));
    battery.coarse = left.coarse || right.coarse;
    battery.charging = left.charging && right.charging;
    battery.wired = left.wired && right.wired;
    return battery;
}

std::optional<JoyConPairing::Pair> JoyConPairing::PairUp()
{
    // The oldest alone of each side
    Entry* left = nullptr;
    Entry* right = nullptr;
    for (Entry& entry : _joyCons)
    {
        if (!entry.partner.empty())
            continue;
        if (entry.left && left == nullptr)
            left = &entry;
        else if (!entry.left && right == nullptr)
            right = &entry;
    }
    if (left == nullptr || right == nullptr)
        return std::nullopt;
    left->partner = right->id;
    right->partner = left->id;
    return Pair{PairId(left->id, right->id), left->id, right->id};
}

JoyConPairing::Change JoyConPairing::Add(const HostDeviceId& joyCon, bool left)
{
    Change change;
    if (joyCon.empty() || std::any_of(_joyCons.begin(), _joyCons.end(),
                                      [&joyCon](const Entry& entry) { return entry.id == joyCon; }))
        return change;
    _joyCons.push_back({joyCon, left, {}});
    change.formed = PairUp();
    return change;
}

JoyConPairing::Change JoyConPairing::Remove(const HostDeviceId& joyCon)
{
    Change change;
    const auto found = std::find_if(_joyCons.begin(), _joyCons.end(),
                                    [&joyCon](const Entry& entry) { return entry.id == joyCon; });
    if (found == _joyCons.end())
        return change;
    const Entry gone = *found;
    _joyCons.erase(found);
    if (gone.partner.empty())
        return change;
    for (Entry& entry : _joyCons)
        if (entry.id == gone.partner)
            entry.partner.clear();
    change.broken = gone.left ? Pair{PairId(gone.id, gone.partner), gone.id, gone.partner}
                              : Pair{PairId(gone.partner, gone.id), gone.partner, gone.id};
    change.formed = PairUp();  // the one left behind may join a Joy-Con that waits
    return change;
}

std::optional<JoyConPairing::Pair> JoyConPairing::PairOf(const HostDeviceId& joyCon) const
{
    for (const Entry& entry : _joyCons)
        if (entry.id == joyCon && !entry.partner.empty())
            return entry.left ? Pair{PairId(entry.id, entry.partner), entry.id, entry.partner}
                              : Pair{PairId(entry.partner, entry.id), entry.partner, entry.id};
    return std::nullopt;
}

std::optional<JoyConPairing::Pair> JoyConPairing::Find(const HostDeviceId& pair) const
{
    for (const Entry& entry : _joyCons)
        if (entry.left && !entry.partner.empty() && PairId(entry.id, entry.partner) == pair)
            return Pair{pair, entry.id, entry.partner};
    return std::nullopt;
}

size_t JoyConPairing::PairCount() const
{
    return static_cast<size_t>(std::count_if(_joyCons.begin(), _joyCons.end(),
                                             [](const Entry& entry) { return entry.left && !entry.partner.empty(); }));
}

}  // namespace controllers
