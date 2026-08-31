#include "emulatorcontext.h"

#include "common/modulelogger.h"
#include "stdafx.h"

/// region <Constructors / destructors>

/// Default constructor with LogTrace default logging level
EmulatorContext::EmulatorContext() : EmulatorContext(LoggerLevel::LogTrace) {}

/// Constructor allowing to specify default logging level
EmulatorContext::EmulatorContext(LoggerLevel level)
{
    // Ensure config and emulator state areas are clean
    this->config = CONFIG{};
    this->emulatorState = EmulatorState{};
    this->temporary = TEMP{};

    // Initialize all pointer members to nullptr for safety
    pModuleLogger = nullptr;
    pMainLoop = nullptr;
    pCore = nullptr;
    pKeyboard = nullptr;
    pMemory = nullptr;
    pPortDecoder = nullptr;
    pTape = nullptr;
    pTapeFastLoad = nullptr;
    pBetaDisk = nullptr;
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
}

/// Constructor registering reference to parent Emulator object
EmulatorContext::EmulatorContext(Emulator* emulator, LoggerLevel level) : EmulatorContext(level)
{
    pEmulator = emulator;
}

EmulatorContext::~EmulatorContext()
{
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
bool EmulatorContext::TakeRunControl(const UUID& owner, const std::string& surfaceLabel,
                                     std::string* errorReason)
{
    // A nil owner UUID is a programming error — surfaces must call UUID::Generate()
    // once at startup and reuse the value.
    if (owner.isNil())
    {
        if (errorReason)
        {
            *errorReason = "TakeRunControl: nil owner UUID is not allowed";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);

    const UUID& current = _runControlClaim.owner;
    if (current.isNil())
    {
        // Unclaimed — take it.
        _runControlClaim.owner = owner;
        _runControlClaim.surfaceLabel = surfaceLabel;
        return true;
    }

    if (current == owner)
    {
        // Idempotent re-claim by the same owner. Refresh label in case the surface
        // wants to re-tag itself, but do not fail.
        _runControlClaim.surfaceLabel = surfaceLabel;
        return true;
    }

    // Held by someone else.
    if (errorReason)
    {
        *errorReason = "Run-control claim held by another surface ('" +
                       _runControlClaim.surfaceLabel + "')";
    }
    return false;
}

void EmulatorContext::ReleaseRunControl(const UUID& owner)
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);

    // Defensive: only the current holder may release. Mismatched releases are
    // silently ignored — surfaces can release unconditionally at shutdown.
    if (_runControlClaim.owner == owner)
    {
        _runControlClaim.owner.clear();
        _runControlClaim.surfaceLabel.clear();
    }
}

bool EmulatorContext::HasRunControl(const UUID& owner) const
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    // Must be both currently claimed AND held by this specific owner.
    return !_runControlClaim.owner.isNil() && _runControlClaim.owner == owner;
}

bool EmulatorContext::IsRunControlClaimed() const
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);
    return !_runControlClaim.owner.isNil();
}

EmulatorContext::RunControlState EmulatorContext::GetRunControlState() const
{
    std::lock_guard<std::mutex> lock(_runControlClaim.mutex);

    RunControlState state;
    const bool claimed = !_runControlClaim.owner.isNil();
    state.claimed = claimed;
    if (claimed)
    {
        state.surfaceLabel = _runControlClaim.surfaceLabel;
        state.ownerUuid = _runControlClaim.owner.toString();
    }
    return state;
}

/// endregion </Run-control claim>
