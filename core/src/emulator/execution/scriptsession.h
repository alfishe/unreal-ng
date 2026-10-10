#pragma once

/// @file scriptsession.h
/// @brief Automation sessions for the in-process script surfaces (Python, Lua): execution arbiter design §6a, X7.
///
/// A script takes control explicitly (take_control), which opens the emulator's run-control session: the GUI shows it
/// and locks its controls. When the user takes control back, every control call of the script (pause, resume, the
/// steps and runs) throws ControlRevoked: final - the script stops, it does not retry and does not take control again
/// by itself (a re-take by the same client is refused during its cooldown anyway).

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/uuid.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/executionarbiter.h"

namespace execution::script
{

/// Thrown by a script's control call after the user took control back (Python: ControlRevoked; Lua: an error)
class ControlRevoked : public std::runtime_error
{
public:
    ControlRevoked()
        : std::runtime_error("revoked_by_user: the user took control back at the emulator's GUI; this script's "
                             "automation session has ended. Stop - do not retry, and do not take control again "
                             "without asking the user.")
    {
    }
};

namespace detail
{
inline std::mutex& Mutex()
{
    static std::mutex m;
    return m;
}
/// (emulator, surface) -> the session's owner
inline std::map<std::pair<const Emulator*, Origin>, unreal::UUID>& Sessions()
{
    static std::map<std::pair<const Emulator*, Origin>, unreal::UUID> sessions;
    return sessions;
}
inline unreal::UUID Owner(const Emulator& emulator, Origin origin)
{
    std::lock_guard<std::mutex> lock(Mutex());
    auto it = Sessions().find({&emulator, origin});
    return it == Sessions().end() ? unreal::UUID() : it->second;
}
}  // namespace detail

/// Open the session: true, or false with the reason (held by another surface; revoked_cooldown)
inline bool TakeControl(Emulator& emulator, Origin origin, const std::string& client, std::string& reason)
{
    EmulatorContext* context = emulator.GetContext();
    if (!context)
    {
        reason = "no emulator context";
        return false;
    }
    const unreal::UUID owner = unreal::UUID::Generate();
    if (!context->TakeRunControl(owner, OriginName(origin), &reason, client, std::chrono::milliseconds(0)))
        return false;
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::Sessions()[{&emulator, origin}] = owner;
    return true;
}

/// End the session (a no-op without one)
inline void ReleaseControl(Emulator& emulator, Origin origin)
{
    unreal::UUID owner;
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        auto it = detail::Sessions().find({&emulator, origin});
        if (it == detail::Sessions().end())
            return;
        owner = it->second;
        detail::Sessions().erase(it);
    }
    if (EmulatorContext* context = emulator.GetContext())
        context->ReleaseRunControl(owner);
}

/// Before a control call: throws ControlRevoked when the user took this script's session back (the session is then
/// forgotten; the script must not retry)
inline void Check(Emulator& emulator, Origin origin)
{
    const unreal::UUID owner = detail::Owner(emulator, origin);
    if (owner.isNil())
        return;
    EmulatorContext* context = emulator.GetContext();
    if (context && context->IsRunControlRevoked(owner))
    {
        {
            std::lock_guard<std::mutex> lock(detail::Mutex());
            detail::Sessions().erase({&emulator, origin});
        }
        throw ControlRevoked();
    }
}

/// Who asks, for the execution arbiter: the script's session when it holds control, else the surface alone
inline Requester RequesterOf(Emulator& emulator, Origin origin)
{
    const unreal::UUID owner = detail::Owner(emulator, origin);
    EmulatorContext* context = emulator.GetContext();
    if (!owner.isNil() && context && context->HasRunControl(owner))
        return Requester{origin, owner.toString()};
    return Requester{origin, {}};
}

}  // namespace execution::script
