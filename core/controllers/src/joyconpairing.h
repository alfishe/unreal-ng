#pragma once

/// @file joyconpairing.h
/// @brief A left and a right Joy-Con as one pad (SDL_hidapi_switch.c, the combined Joy-Cons SDL makes by default:
/// SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS). Pure, no OS calls: the backend tells it which Joy-Cons come and go, it
/// says which pairs form and break. The oldest Joy-Con without a partner pairs with the oldest one of the other side;
/// a Joy-Con whose partner goes is alone again and pairs with the next one of the other side that waits.
///
/// The pair's id is both ids with a '+' between, left first: stable while the same two Joy-Cons are joined, and a
/// profile naming one Joy-Con's id (a substring) applies to its pair as well.
///
/// Not thread-safe: the backend calls it under its own lock.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "hostbackend.h"
#include "hostdevice.h"

namespace controllers
{

class JoyConPairing
{
public:
    struct Pair
    {
        HostDeviceId id;  ///< left + "+" + right
        HostDeviceId left;
        HostDeviceId right;
    };
    /// What one Add / Remove changed
    struct Change
    {
        std::optional<Pair> broken;  ///< a pair gone with the Joy-Con that left (the other one is alone again)
        std::optional<Pair> formed;  ///< a pair just made
    };

    static HostDeviceId PairId(const HostDeviceId& left, const HostDeviceId& right);
    /// The pair's battery: the lower level, charging or on the cable only when both are (SDL: the lower power state
    /// and percentage of the two); one half's alone until the other reports
    static BatteryState Battery(const BatteryState& left, const BatteryState& right);

    Change Add(const HostDeviceId& joyCon, bool left);
    Change Remove(const HostDeviceId& joyCon);

    /// The pair a Joy-Con is in; nullopt while it is alone (or unknown)
    std::optional<Pair> PairOf(const HostDeviceId& joyCon) const;
    /// The pair of that id
    std::optional<Pair> Find(const HostDeviceId& pair) const;
    size_t PairCount() const;

private:
    struct Entry
    {
        HostDeviceId id;
        bool left = false;
        HostDeviceId partner;
    };
    std::optional<Pair> PairUp();

    std::vector<Entry> _joyCons;  ///< in arrival order
};

}  // namespace controllers
