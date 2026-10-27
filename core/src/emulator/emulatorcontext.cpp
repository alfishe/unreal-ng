#include "emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/notifications.h"
#include "3rdparty/message-center/messagecenter.h"
#include <algorithm>
#include "emulator/emulator.h"

#include "common/modulelogger.h"
#include "emulator/memory/devicememory.h"
#include "stdafx.h"

/// region <Constructors / destructors>

namespace
{
std::atomic<size_t> g_liveContexts{0};
}

size_t EmulatorContext::LiveCount()
{
    return g_liveContexts.load();
}

/// Default constructor with LogTrace default logging level
EmulatorContext::EmulatorContext() : EmulatorContext(LoggerLevel::LogTrace) {}

/// Constructor allowing to specify default logging level
EmulatorContext::EmulatorContext(LoggerLevel level)
{
    // Ensure config and emulator state areas are clean
    this->config = CONFIG{};
    this->emulatorState = EmulatorState{};
    this->temporary = TEMP{};

    // The ATM palette must carry the standard ZX colors, not the value-init
    // black - the extended video modes render through it before any #FF write
    this->emulatorState.atm.InitPalette();
    this->emulatorState.atm.InitFont();

    // Initialize all pointer members to nullptr for safety
    pModuleLogger = nullptr;
    pMainLoop = nullptr;
    pCore = nullptr;
    pKeyboard = nullptr;
    pMouse = nullptr;
    pMouseManager = nullptr;
    pJoystick = nullptr;
    pMemory = nullptr;
    pPortDecoder = nullptr;
    pTape = nullptr;
    pTapeFastLoad = nullptr;
    pTapeTurboController = nullptr;
    pBetaDisk = nullptr;
    pDiskFastLoad = nullptr;
    pDiskAutostart = nullptr;
    pScreen = nullptr;
    pAudioManagerObj = nullptr;
    pAudioCallback = nullptr;
    pSoundManager = nullptr;
#ifdef ENABLE_RECORDING
    pRecordingManager = nullptr;
#endif
    pDebugManager = nullptr;
    pEmulator = nullptr;

    // Create advanced logging
    ModuleLogger* moduleLogger = new ModuleLogger(this);
    if (moduleLogger)
    {
        moduleLogger->SetLoggingLevel(level);

        moduleLogger->LogMessage(LoggerLevel::LogDebug, PlatformModulesEnum::MODULE_CORE,
                                 PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG,
                                 "Emulator - ModuleLogger initialized");

        pModuleLogger = moduleLogger;
    }
    else
    {
        throw std::runtime_error("EmulatorContext::EmulatorContext() - Unable to initialize ModuleLogger");
    }

    g_liveContexts.fetch_add(1);
}

/// Constructor registering reference to parent Emulator object
EmulatorContext::EmulatorContext(Emulator* emulator, LoggerLevel level) : EmulatorContext(level)
{
    pEmulator = emulator;
}

EmulatorContext::~EmulatorContext()
{
    g_liveContexts.fetch_sub(1);
    DeviceMemory::Forget(this);   // the registry's views of this machine's memories
    if (pModuleLogger != nullptr)
    {
        delete pModuleLogger;
    }
}
/// endregion </Constructors / destructors>

/// region <Run-control claim (GDB TDD §3.3 / parent TDD §7.2)>
//
// Advisory owner token. Pause and read-only queries are always allowed; while a
// surface holds the claim with the target paused, other surfaces' run-affecting
// operations (Resume/Step/Seek/state-writes) are refused. Sprint 0 ships the
// mechanism only — enforcement at the call sites lands in Phase 2 / G1.
//
// Note on UUID semantics: a default-constructed UUID is all-zero (the "nil"
// UUID) and is used here as the sentinel meaning "unclaimed". UUID::isNil()
// reports exactly that (an earlier revision returned the inverted result; the
// workarounds were migrated back to isNil() when it was fixed).
//
namespace
{
int64_t NowEpochMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string ClientKey(const std::string& label, const std::string& client)
{
    return label + "/" + client;
}
}  // namespace

bool EmulatorContext::TakeRunControl(const unreal::UUID& owner, const std::string& surfaceLabel,
                                     std::string* errorReason)
{
    return TakeRunControl(owner, surfaceLabel, errorReason, {}, std::chrono::milliseconds(0));
}

bool EmulatorContext::TakeRunControl(const unreal::UUID& owner, const std::string& surfaceLabel, std::string* errorReason,
                                     const std::string& client, std::chrono::milliseconds lease,
                                     const std::string& peer)
{
    return TakeRunControlAs(owner, surfaceLabel, errorReason, client, lease, peer, {});
}

bool EmulatorContext::TakeRunControlAs(const unreal::UUID& owner, const std::string& surfaceLabel, std::string* errorReason,
                                       const std::string& client, std::chrono::milliseconds lease,
                                       const std::string& peer, const std::string& implicitIdentity)
{
    // A nil owner UUID is a programming error — surfaces must call UUID::Generate()
    // once at startup and reuse the value.
    if (owner.isNil())
    {
        if (errorReason)
            *errorReason = "TakeRunControl: nil owner UUID is not allowed";
        return false;
    }
    ExpireIdleRunControl();

    bool taken = false;
    bool refusedCooldown = false;
    std::string supersededLabel, supersededClient, supersededOwner;
    {
        std::lock_guard<std::mutex> lock(_runControlClaim.mutex);

        // The user took control back from this client a short while ago: it does not take it again by itself
        // (owner decision 2026-10-09: clients do not rush back; the user allows it at the GUI or the cooldown ends)
        const std::string key = ClientKey(surfaceLabel, client);
        const auto now = RunControlNow();
        auto& cooldowns = _runControlClaim.cooldowns;
        cooldowns.erase(std::remove_if(cooldowns.begin(), cooldowns.end(),
                                       [&](const auto& c) { return c.second <= now; }),
                        cooldowns.end());
        for (const auto& c : cooldowns)
            if (c.first == key)
            {
                if (errorReason)
                    *errorReason = "revoked_cooldown: the user took control back from '" + key +
                                   "' at the GUI; do not retry - ask the user to allow it again";
                refusedCooldown = true;
            }

        if (!refusedCooldown)
        {
            RunControlClaim& c = _runControlClaim;
            // An explicit take takes over another client's implicit session (owner 2026-10-24: an implicit session
            // makes an un-sessioned client visible and revocable, it reserves nothing; the gate's advice is "take
            // control explicitly"). A command of it under way finishes; its next change meets the explicit holder
            if (!c.owner.isNil() && c.owner != owner && !c.implicitIdentity.empty() && implicitIdentity.empty())
            {
                supersededLabel = c.surfaceLabel;
                supersededClient = c.client;
                supersededOwner = c.owner.toString();
                if (pEmulator)
                    pEmulator->GetArbiter().ReleaseClaim(
                        execution::Requester{execution::OriginFromName(supersededLabel), supersededOwner});
                c.owner.clear();
                c.surfaceLabel.clear();
                c.client.clear();
                c.peer.clear();
                c.implicitIdentity.clear();
            }
            const unreal::UUID& current = _runControlClaim.owner;
            if (current.isNil())
            {
                // Unclaimed — take it. The execution arbiter enforces it: a run (an intent or a direct run) from any
                // other surface is refused while it is held; a pause never is
                _runControlClaim.owner = owner;
                _runControlClaim.surfaceLabel = surfaceLabel;
                _runControlClaim.client = client;
                _runControlClaim.peer = peer;
                _runControlClaim.sinceMs = NowEpochMs();
                _runControlClaim.lease = lease;
                _runControlClaim.implicitIdentity = implicitIdentity;
                _runControlClaim.lastActivity = now;
                auto& revoked = _runControlClaim.revokedOwners;
                revoked.erase(std::remove(revoked.begin(), revoked.end(), owner.toString()), revoked.end());
                if (pEmulator)
                    pEmulator->GetArbiter().TakeClaim(
                        execution::Requester{execution::OriginFromName(surfaceLabel), owner.toString()});
                taken = true;
            }
            else if (current == owner)
            {
                // Idempotent re-claim by the same owner. Refresh label in case the surface
                // wants to re-tag itself, but do not fail.
                _runControlClaim.surfaceLabel = surfaceLabel;
                _runControlClaim.lastActivity = now;
                return true;
            }
            else if (errorReason)
            {
                *errorReason = DescribeHolder(RunControlStateLocked());
            }
        }
    }
    if (!supersededOwner.empty())
    {
        LOGINFO("Run control: %s %s took over the implicit session of %s %s", surfaceLabel.c_str(), client.c_str(),
                supersededLabel.c_str(), supersededClient.c_str());
        PostRunControlChanged("superseded", supersededLabel, supersededClient, supersededOwner,
                              surfaceLabel + (client.empty() ? std::string() : " " + client));
    }
    if (taken)
        PostRunControlChanged("taken", surfaceLabel, client, owner.toString());
    else if (refusedCooldown)
        PostRunControlChanged("retake_refused", surfaceLabel, client, owner.toString());
    return taken;
}

void EmulatorContext::ReleaseRunControl(const unreal::UUID& owner)
{
    std::string label, client;
    {
        std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
        // Defensive: only the current holder may release. Mismatched releases are
        // silently ignored — surfaces can release unconditionally at shutdown.
        if (_runControlClaim.owner != owner || owner.isNil())
            return;
        if (pEmulator)
            pEmulator->GetArbiter().ReleaseClaim(
                execution::Requester{execution::OriginFromName(_runControlClaim.surfaceLabel), owner.toString()});
        label = _runControlClaim.surfaceLabel;
        client = _runControlClaim.client;
        _runControlClaim.owner.clear();
        _runControlClaim.surfaceLabel.clear();
        _runControlClaim.client.clear();
        _runControlClaim.peer.clear();
        _runControlClaim.implicitIdentity.clear();
    }
    PostRunControlChanged("released", label, client, owner.toString());
}

bool EmulatorContext::RevokeRunControl(const std::string& by)
{
    std::string label, client, ownerText;
    {
        std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
        if (_runControlClaim.owner.isNil())
            return false;
        ownerText = _runControlClaim.owner.toString();
        label = _runControlClaim.surfaceLabel;
        client = _runControlClaim.client;
        if (pEmulator)
            pEmulator->GetArbiter().ReleaseClaim(
                execution::Requester{execution::OriginFromName(label), ownerText});
        _runControlClaim.revokedOwners.push_back(ownerText);
        _runControlClaim.cooldowns.emplace_back(
            ClientKey(label, client),
            RunControlNow() + std::chrono::seconds(_runControlClaim.retakeCooldownSeconds));
        _runControlClaim.owner.clear();
        _runControlClaim.surfaceLabel.clear();
        _runControlClaim.client.clear();
        _runControlClaim.peer.clear();
        _runControlClaim.implicitIdentity.clear();
    }
    // Its direct run (if one is under way) ends at the next instruction; the call answers revoked_by_user
    if (pEmulator)
        pEmulator->GetArbiter().AbortDriving();
    LOGINFO("Run control taken back from %s %s by %s", label.c_str(), client.c_str(), by.c_str());
    PostRunControlChanged("revoked", label, client, ownerText, by);
    return true;
}

bool EmulatorContext::IsRunControlRevoked(const unreal::UUID& owner) const
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    const auto& revoked = _runControlClaim.revokedOwners;
    return std::find(revoked.begin(), revoked.end(), owner.toString()) != revoked.end();
}

void EmulatorContext::TouchRunControl(const unreal::UUID& owner)
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    if (_runControlClaim.owner == owner)
        _runControlClaim.lastActivity = RunControlNow();
}

void EmulatorContext::AllowRunControlRetake(const std::string& clientKey)
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    auto& cooldowns = _runControlClaim.cooldowns;
    if (clientKey.empty())
        cooldowns.clear();
    else
        cooldowns.erase(std::remove_if(cooldowns.begin(), cooldowns.end(),
                                       [&](const auto& c) { return c.first == clientKey; }),
                        cooldowns.end());
}

void EmulatorContext::SetRunControlRetakeCooldown(uint32_t seconds)
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    _runControlClaim.retakeCooldownSeconds = seconds;
}

EmulatorContext::ImplicitControl EmulatorContext::EnterImplicitRunControl(const std::string& surfaceLabel,
                                                                         const std::string& identity,
                                                                         const std::string& client,
                                                                         std::chrono::milliseconds lease,
                                                                         const std::string& peer)
{
    ExpireIdleRunControl();
    ImplicitControl result;
    {
        std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
        RunControlClaim& c = _runControlClaim;
        const auto now = RunControlNow();
        if (!c.owner.isNil())
        {
            // Its own implicit session goes on; anybody else's session refuses it
            if (!c.implicitIdentity.empty() && c.implicitIdentity == identity)
            {
                c.lastActivity = now;
                result.allowed = true;
                result.owner = c.owner;
                return result;
            }
            result.error = "held_by_other";
            result.holder = RunControlStateLocked();
            result.message = DescribeHolder(result.holder);
            return result;
        }
        // The user took control back from this client a short while ago: it stops, it does not drive on implicitly
        const std::string key = ClientKey(surfaceLabel, client);
        for (const auto& cooldown : c.cooldowns)
            if (cooldown.first == key && cooldown.second > now)
            {
                result.error = "revoked_by_user";
                result.message = "The user took control back from '" + client +
                                 "' at the GUI; stop, do not retry - ask the user before driving the emulator again";
                return result;
            }
    }
    const unreal::UUID owner = unreal::UUID::Generate();
    std::string reason;
    if (!TakeRunControlAs(owner, surfaceLabel, &reason, client, lease, peer, identity))
    {
        result.error = reason.rfind("revoked_cooldown", 0) == 0 ? "revoked_by_user" : "held_by_other";
        result.message = reason;
        if (result.error == "held_by_other")
        {
            std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
            result.holder = RunControlStateLocked();
            result.message = DescribeHolder(result.holder);
        }
        return result;
    }
    LOGINFO("Run control taken implicitly by %s %s (it drove the machine without taking control)", surfaceLabel.c_str(),
            client.c_str());
    result.allowed = true;
    result.opened = true;
    result.owner = owner;
    return result;
}

bool EmulatorContext::ReleaseImplicitRunControl(const std::string& identity)
{
    const unreal::UUID owner = ImplicitRunControlOwner(identity);
    if (owner.isNil())
        return false;
    ReleaseRunControl(owner);
    return true;
}

unreal::UUID EmulatorContext::ImplicitRunControlOwner(const std::string& identity) const
{
    ExpireIdleRunControl();
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    if (_runControlClaim.owner.isNil() || _runControlClaim.implicitIdentity.empty() ||
        _runControlClaim.implicitIdentity != identity)
        return unreal::UUID();
    return _runControlClaim.owner;
}

void EmulatorContext::SetRunControlClock(std::function<std::chrono::steady_clock::time_point()> clock)
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    _runControlClaim.clock = std::move(clock);
}

std::chrono::steady_clock::time_point EmulatorContext::RunControlNow() const
{
    return _runControlClaim.clock ? _runControlClaim.clock() : std::chrono::steady_clock::now();
}

std::string EmulatorContext::DescribeHolder(const RunControlState& holder)
{
    if (!holder.claimed)
        return "Run control is free";
    std::string text = "Run control is held by " + holder.surfaceLabel +
                       (holder.client.empty() ? std::string() : " (" + holder.client + ")") +
                       (holder.peer.empty() ? std::string() : " from " + holder.peer);
    const std::string leaseLeft =
        holder.leaseMs ? std::to_string((holder.leaseLeftMs + 999) / 1000) + " s" : std::string();
    if (holder.implicit)
    {
        text += " in an implicit session (opened because it changed the machine without taking control";
        text += leaseLeft.empty() ? std::string(")") : "; it ends after " + leaseLeft + " more without a call of it)";
        text += ". Take control explicitly - POST /control/session, MCP control_execution take_control, CLI session "
                "take, take_control() - which takes over an implicit session; then send the session id from every "
                "client that drives this emulator (header X-Unreal-Session). Or wait until it ends";
    }
    else
    {
        text += " in an explicit session";
        if (!leaseLeft.empty())
            text += " (it ends after " + leaseLeft + " more without a call of it)";
        text += ". Wait until it releases control, use its session id if it is yours, or ask the user (Take back "
                "control at the GUI)";
    }
    return text;
}

void EmulatorContext::ExpireIdleRunControl() const
{
    std::string label, client, ownerText;
    {
        std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
        const RunControlClaim& c = _runControlClaim;
        if (c.owner.isNil() || c.lease.count() == 0 || RunControlNow() - c.lastActivity < c.lease)
            return;
        ownerText = c.owner.toString();
        label = c.surfaceLabel;
        client = c.client;
        if (pEmulator)
            pEmulator->GetArbiter().ReleaseClaim(execution::Requester{execution::OriginFromName(label), ownerText});
        auto& mutableClaim = const_cast<RunControlClaim&>(c);
        mutableClaim.owner.clear();
        mutableClaim.surfaceLabel.clear();
        mutableClaim.client.clear();
        mutableClaim.peer.clear();
        mutableClaim.implicitIdentity.clear();
    }
    LOGINFO("Run control session of %s %s expired (idle lease)", label.c_str(), client.c_str());
    PostRunControlChanged("expired", label, client, ownerText);
}

void EmulatorContext::PostRunControlChanged(const std::string& event, const std::string& label,
                                            const std::string& client, const std::string& owner,
                                            const std::string& by) const
{
    MessageCenter::DefaultMessageCenter().Post(
        NC_RUN_CONTROL_CHANGED, new RunControlChangedPayload(emulatorId, event, label, client, owner, by), true);
}

bool EmulatorContext::HasRunControl(const unreal::UUID& owner) const
{
    ExpireIdleRunControl();
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    // Must be both currently claimed AND held by this specific owner.
    return !_runControlClaim.owner.isNil() && _runControlClaim.owner == owner;
}

bool EmulatorContext::IsRunControlClaimed() const
{
    ExpireIdleRunControl();
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    return !_runControlClaim.owner.isNil();
}

EmulatorContext::RunControlState EmulatorContext::GetRunControlState() const
{
    ExpireIdleRunControl();
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    return RunControlStateLocked();
}

EmulatorContext::RunControlState EmulatorContext::RunControlStateLocked() const
{
    RunControlState state;
    const bool claimed = !_runControlClaim.owner.isNil();
    state.claimed = claimed;
    if (claimed)
    {
        state.surfaceLabel = _runControlClaim.surfaceLabel;
        state.ownerUuid = _runControlClaim.owner.toString();
        state.client = _runControlClaim.client;
        state.peer = _runControlClaim.peer;
        state.sinceMs = _runControlClaim.sinceMs;
        state.leaseMs = static_cast<uint32_t>(_runControlClaim.lease.count());
        state.implicit = !_runControlClaim.implicitIdentity.empty();
        if (_runControlClaim.lease.count() > 0)
        {
            const auto left = _runControlClaim.lease - std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           RunControlNow() - _runControlClaim.lastActivity);
            state.leaseLeftMs = left.count() > 0 ? static_cast<uint32_t>(left.count()) : 0u;
        }
    }
    const auto now = RunControlNow();
    for (const auto& c : _runControlClaim.cooldowns)
        if (c.second > now)
            state.cooldowns.emplace_back(c.first,
                                         std::chrono::duration_cast<std::chrono::seconds>(c.second - now).count());
    return state;
}

/// endregion </Run-control claim>
