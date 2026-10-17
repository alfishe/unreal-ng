#pragma once

/// @file scriptsession.h
/// @brief Automation sessions for the in-process script surfaces (Python, Lua): execution arbiter design §6a, X7.
///
/// A script takes control explicitly (take_control), which opens the emulator's run-control session: the GUI shows it
/// and locks its controls. When the user takes control back, every control call of the script (pause, resume, the
/// steps and runs) throws ControlRevoked: final - the script stops, it does not retry and does not take control again
/// by itself (a re-take by the same client is refused during its cooldown anyway).
///
/// The automation gate (automationgate.h): a script that drives the machine without having taken control gets a
/// session implicitly at its first control call (a pause opens none); each control call renews its 15 s idle lease.
/// The script is told once (Python: a UserWarning; otherwise the emulator's log) and advised to take control itself.

#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/uuid.h"
#include "emulator/emulator.h"
#include "common/logger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/executionarbiter.h"

namespace execution::script
{

/// Thrown by a script's control call after the user took control back (Python: ControlRevoked; Lua: an error)
class ControlRevoked : public std::runtime_error
{
public:
    ControlRevoked()
        : std::runtime_error(
              "revoked_by_user: the user took control back (at the GUI or from a companion); this script's "
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
/// Where a surface tells its script about an implicit session (the binding sets it: Python warns)
inline std::map<Origin, std::function<void(const std::string&)>>& AdviceSinks()
{
    static std::map<Origin, std::function<void(const std::string&)>> sinks;
    return sinks;
}
/// The gate's identity of a script surface: one implicit session per surface
inline std::string GateIdentity(Origin origin)
{
    return std::string(OriginName(origin)) + "|script";
}
}  // namespace detail

/// How the surface's binding tells its script that the gate opened a session (called with the advice text)
inline void SetAdviceSink(Origin origin, std::function<void(const std::string&)> sink)
{
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::AdviceSinks()[origin] = std::move(sink);
}

/// Open the session: true, or false with the reason (held by another surface; revoked_cooldown)
inline bool TakeControl(Emulator& emulator, Origin origin, const std::string& client, std::string& reason)
{
    EmulatorContext* context = emulator.GetContext();
    if (!context)
    {
        reason = "no emulator context";
        return false;
    }
    // The gate's implicit session for this surface gives way to the explicit one
    context->ReleaseImplicitRunControl(detail::GateIdentity(origin));
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
/// forgotten; the script must not retry). `drives`: the call changes the machine (a pause does not) - without a
/// session of its own, the script passes the automation gate: it gets one implicitly, or is refused
inline void Check(Emulator& emulator, Origin origin, bool drives = true)
{
    EmulatorContext* context = emulator.GetContext();
    const unreal::UUID owner = detail::Owner(emulator, origin);
    if (!owner.isNil())
    {
        if (context && context->IsRunControlRevoked(owner))
        {
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                detail::Sessions().erase({&emulator, origin});
            }
            throw ControlRevoked();
        }
        if (!context || context->HasRunControl(owner))
        {
            if (context)
                context->TouchRunControl(owner);
            return;
        }
        // Its implicit session's lease ran out (or it was released): forgotten, the gate decides anew
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::Sessions().erase({&emulator, origin});
    }
    if (!drives || !context)
        return;
    const std::string label = OriginName(origin);
    const EmulatorContext::ImplicitControl gate = context->EnterImplicitRunControl(
        label, detail::GateIdentity(origin), label + " script",
        std::chrono::duration_cast<std::chrono::milliseconds>(automation_gate::kImplicitLease));
    if (!gate.allowed)
    {
        if (gate.error == "revoked_by_user")
            throw ControlRevoked();
        throw std::runtime_error(gate.error + ": " + gate.message);
    }
    std::function<void(const std::string&)> sink;
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::Sessions()[{&emulator, origin}] = gate.owner;
        if (gate.opened)
        {
            auto it = detail::AdviceSinks().find(origin);
            if (it != detail::AdviceSinks().end())
                sink = it->second;
        }
    }
    if (!gate.opened)
        return;
    if (sink)
        sink(automation_gate::kAdvice);
    else
        LOGWARNING("%s script: %s", label.c_str(), automation_gate::kAdvice);
}

/// A script's pause, said: "paused", "queued: behind 'run_frames' (webapi) ..." (it applies when that ends) or
/// "refused: <why>"
inline std::string PauseStatus(const Result& result)
{
    switch (result.outcome)
    {
        case Outcome::Applied:
        case Outcome::Coalesced:
            return "paused";
        case Outcome::Pending:
        case Outcome::Busy:
            return "queued: " + result.detail;
        default:
            return "refused: " + result.detail;
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
