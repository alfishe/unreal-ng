#pragma once

/// @file devicesfeed.h
/// @brief The value of the `controllers.devices` topic (surfaces.md §2.1): the host input owner's poll asks it on its
/// own thread whether the `devices` reply changed, so the automation planes read a published copy and never cross to
/// the owner's thread. It builds the reply again only when something may have changed: a device arrived, left or was
/// promoted (HostInputService::Revision), a host verb ran (HostInputControl::Changes: a role, a pin, a player, a
/// database entry), or kStatusEvery passed (the battery, the notice and the light the backends refresh on their own
/// threads every 10-30 s). Between those a poll is two integer compares.

#include <chrono>
#include <cstdint>
#include <string>

#include "profiles/json.h"

namespace controllers
{

class IHostInputOwner;

class DevicesFeed
{
public:
    using Clock = std::chrono::steady_clock;
    /// How often the battery, notice and light are looked at again without another reason
    static constexpr std::chrono::milliseconds kStatusEvery{2000};

    /// On the owner's thread. @p inputRevision: HostInputService::Revision (0 without a service). True when the
    /// value changed since the last true (the first poll is a change): Value() holds it
    bool Poll(IHostInputOwner& owner, uint64_t inputRevision, Clock::time_point now);

    /// The `devices` reply's body {devices: [...]} at the last change
    const JsonValue& Value() const
    {
        return _value;
    }
    /// The changes so far (the topic's `revision`)
    uint64_t Revision() const
    {
        return _revision;
    }
    /// The replies built (the overhead's measure: the tests count them)
    uint64_t Builds() const
    {
        return _builds;
    }

private:
    bool _have = false;
    uint64_t _inputRevision = 0;
    uint64_t _changes = 0;
    Clock::time_point _builtAt;
    std::string _text;  ///< _value's compact text: the change test
    JsonValue _value;
    uint64_t _revision = 0;
    uint64_t _builds = 0;
};

}  // namespace controllers
