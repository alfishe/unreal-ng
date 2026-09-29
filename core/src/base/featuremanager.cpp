#include "featuremanager.h"

#include "emulator/io/mouse/mouse.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

#include "3rdparty/message-center/messagecenter.h"
#include "common/inifile.h"
#include "common/modulelogger.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#endif
#include "emulator/video/screen.h"

/// region <Logging>
const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
constexpr uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_FEATURES;
/// endregion </Logging>

FeatureManager::FeatureManager(EmulatorContext* context) : _context(context)
{
    setDefaults();
}

/// @brief Register a new feature with metadata and default values.
/// @param info Feature information structure containing all metadata
void FeatureManager::registerFeature(const FeatureInfo& info)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _features[info.id] = info;
    if (!info.alias.empty())
    {
        _aliases[info.alias] = info.id;
    }
}

/// @brief Remove a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature to remove
void FeatureManager::removeFeature(const std::string& idOrAlias)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto* feature = findFeature(idOrAlias);
    if (feature)
    {
        _aliases.erase(feature->alias);
        _features.erase(feature->id);
        _dirty = true;
    }
}

/// @brief Remove all features (reset to empty).
void FeatureManager::clear()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _features.clear();
    _aliases.clear();
    _dirty = true;
}

/// @brief True while the TTD recording lock is held (TimeTravelManager engages it
/// on entering Recording and holds it until the session returns to Idle)
bool FeatureManager::isTtdRecordingActive() const
{
    if (_ttdShortcutOverrideActive)
    {
        return true;
    }

    if (_context && _context->pTimeTravelManager)
    {
        if (_context->pTimeTravelManager->IsRecording())
            return true;
    }

    return false;
}

bool FeatureManager::isTtdTimelineBound() const
{
    if (isTtdRecordingActive())
        return true;
    if (!_context)
        return false;
    if (_context->ttdReplayActive)
        return true;
    return _context->pTimeTravelManager &&
           _context->pTimeTravelManager->GetState() == ttd::TTDSessionState::Detached;
}

void FeatureManager::onTtdRecordingStarted()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _ttdShortcutOverrideActive = true;
    onFeatureChanged(Features::kTimeTravel);
}

void FeatureManager::onTtdRecordingStopped()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _ttdShortcutOverrideActive = false;
    onFeatureChanged(Features::kTimeTravel);
}

/// @brief Set feature enabled/disabled by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @param enabled Whether to enable or disable the feature
/// @return true if the feature was found and updated, false if feature not found
bool FeatureManager::setFeature(const std::string& idOrAlias, bool enabled)
{
    std::string changedId;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto* feature = findFeature(idOrAlias);
        if (feature)
        {
            const std::string& id = feature->id;

            // Block enabling the fast-disk / fast-tape / turbo-tape shortcuts while the
            // machine is bound to a TTD timeline (they change what the guest code does,
            // so a replay would diverge), and turbo mode while recording (a recorded run
            // must reflect real timing)
            // ...and switching the capture flags off (timetravel, debugmode) while
            // recording: capture would stop mid-session and corrupt the history
            const std::string refusal = refusalReason(id, enabled);
            if (!refusal.empty())
            {
                if (_context && _context->pModuleLogger)
                {
                    _context->pModuleLogger->Warning(_MODULE, _SUBMODULE, "setFeature('%s', %s) refused: %s",
                                                     id.c_str(), enabled ? "on" : "off", refusal.c_str());
                }
                return false;
            }

            // Contention changes the machine's timing in both directions: a timeline recorded with one
            // setting replays only with the same one
            if (id == Features::kContention && enabled != feature->enabled && isTtdTimelineBound())
            {
                if (_context && _context->pModuleLogger)
                {
                    _context->pModuleLogger->Warning(_MODULE, _SUBMODULE,
                        "Cannot change '%s' while the machine is bound to a TTD timeline", id.c_str());
                }
                return false;
            }

            bool wasEnabled = feature->enabled;
            bool valueChanged = (wasEnabled != enabled);
            feature->enabled = enabled;

            // Log the feature state change
            if (_context && _context->pModuleLogger)
            {
                if (valueChanged)
                {
                    _context->pModuleLogger->Info(_MODULE, _SUBMODULE,
                        "Feature '%s' changed: %s -> %s",
                        feature->id.c_str(),
                        wasEnabled ? "ON" : "OFF",
                        enabled ? "ON" : "OFF");
                }
            }

            // Auto-enable master debugmode when enabling any debug subfeature
            // This ensures breakpoints/calltrace/memorytracking work as expected
            if (enabled)
            {
                if (id == Features::kBreakpoints || id == Features::kCallTrace || id == Features::kMemoryTracking ||
                    id == Features::kTimeTravel)
                {
                    auto* master = findFeature(Features::kDebugMode);
                    if (master && !master->enabled)
                    {
                        master->enabled = true;
                        valueChanged = true;

                        // Log the auto-enabled master feature
                        if (_context && _context->pModuleLogger)
                        {
                            _context->pModuleLogger->Info(_MODULE, _SUBMODULE,
                                "Feature '%s' auto-enabled (required by '%s')",
                                Features::kDebugMode,
                                feature->id.c_str());
                        }
                    }
                }
            }

            // Always call onFeatureChanged() to ensure caches are synchronized,
            // even if the value didn't change. This handles the edge case where
            // UpdateFeatureCache() was never called during initialization.
            // Only mark dirty if value actually changed (for persistence purposes).
            if (valueChanged)
            {
                _dirty = true;
            }

            changedId = feature->id;
        }
        else
        {
            // Feature not found - log warning
            if (_context && _context->pModuleLogger)
            {
                _context->pModuleLogger->Warning(_MODULE, _SUBMODULE,
                    "Feature '%s' not found", idOrAlias.c_str());
            }
            return false;
        }
    }

    // Notify outside the lock: observers may query FeatureManager back and the
    // cascade refreshes subsystem caches
    onFeatureChanged(changedId);

    return true;
}

/// @brief Set mode for a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @param mode New mode to set for the feature
/// @return true if the feature was found and updated, false if feature not found
bool FeatureManager::setMode(const std::string& idOrAlias, const std::string& mode)
{
    std::unique_lock<std::recursive_mutex> lock(_mutex);
    auto* feature = findFeature(idOrAlias);
    if (feature && feature->mode != mode)
    {
        feature->mode = mode;
        _dirty = true;
        std::string changedId = feature->id;
        lock.unlock();

        // Notify outside the lock (see setFeature)
        onFeatureChanged(changedId);
        return true;
    }
    else if (feature)
    {
        // Feature found but no change needed
        return true;
    }
    else
    {
        // Feature not found
        return false;
    }
}

/// @brief Get mode for a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Current mode of the feature, or empty string if not found
std::string FeatureManager::getMode(const std::string& idOrAlias) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const auto* feature = findFeature(idOrAlias);
    return feature ? feature->mode : "";
}

/// @brief Query if a feature is enabled by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @return true if the feature is enabled, false otherwise or if not found
bool FeatureManager::isEnabled(const std::string& idOrAlias) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const auto* feature = findFeature(idOrAlias);
    if (!feature)
        return false;

    return feature->enabled && !isMaskedByTtd(feature->id);
}

std::string FeatureManager::refusalReason(const std::string& idOrAlias, bool enabled) const
{
    std::string id;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        const auto* feature = findFeature(idOrAlias);
        if (!feature)
            return {};
        id = feature->id;
    }

    // Either direction swaps the fitted General Sound card (FR-4)
    if (id == Features::kGSLightweight)
    {
        ttd::TimeTravelManager* ttd = _context ? _context->pTimeTravelManager : nullptr;
        const bool gsFitted = _context && _context->pSoundManager && _context->pSoundManager->getGeneralSound();
        return (ttd && gsFitted) ? ttd->RecordingGuard(ttd::TTDGuardedAction::SwitchGsCard) : std::string();
    }

    if (!enabled)
    {
        ttd::TimeTravelManager* ttd = _context ? _context->pTimeTravelManager : nullptr;
        if (ttd && id == Features::kTimeTravel)
            return ttd->RecordingGuard(ttd::TTDGuardedAction::DisableTimeTravel);
        if (ttd && id == Features::kDebugMode)
            return ttd->RecordingGuard(ttd::TTDGuardedAction::DisableDebugMode);
        return {};
    }

    if ((id == Features::kFastDisk || id == Features::kFastTape || id == Features::kTurboTape) && isTtdTimelineBound())
    {
        return "Cannot enable " + id + " while TTD is recording or replaying history: it changes what the guest "
               "code does, so the replay would no longer match the recording. Stop the recording, or leave the "
               "history, first.";
    }
    if (id == Features::kTurboMode && isTtdRecordingActive())
    {
        return "Cannot enable turbo mode while TTD is recording: a recording must show the code running at real "
               "speed. Stop the recording first.";
    }
    return {};
}

bool FeatureManager::hasFeature(const std::string& idOrAlias) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return findFeature(idOrAlias) != nullptr;
}

bool FeatureManager::isMaskedByTtd(const std::string& id) const
{
    // Shortcuts read as OFF for the whole timeline binding (recording, replay, Detached);
    // turbo mode only while recording
    if (id == Features::kFastDisk || id == Features::kFastTape || id == Features::kTurboTape)
        return isTtdTimelineBound();
    if (id == Features::kTurboMode)
        return isTtdRecordingActive();
    return false;
}

/// @brief Engage/disengage Core turbo mode to match the 'turbomode' feature and the
/// current TTD-recording gate (see header doc). Idempotent.
void FeatureManager::syncTurboModeWithTtdState()
{
    if (!_context || !_context->pCore)
        return;

    bool wantsTurbo = false;
    bool withAudio = _context->config.turbo_mode_audio;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        const auto* tm = findFeature(Features::kTurboMode);
        wantsTurbo = tm && tm->enabled;
    }

    // Recording forces turbo off regardless of the stored desired state; the
    // desired state itself is left untouched so it can be restored verbatim
    // once recording stops (same masking approach as isEnabled() above).
    if (isTtdRecordingActive())
        wantsTurbo = false;

    if (_context->pCore->IsTurboMode() != wantsTurbo)
    {
        if (wantsTurbo)
            _context->pCore->EnableTurboMode(withAudio);
        else
            _context->pCore->DisableTurboMode();
    }
}

/// @brief List all features and their metadata.
/// @return Vector containing FeatureInfo for all registered features
std::vector<FeatureManager::FeatureInfo> FeatureManager::listFeatures() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<FeatureInfo> out;
    for (const auto& kv : _features)
    {
        // The state in effect, as isEnabled() reports it (TTD may force a feature off)
        out.push_back(kv.second);
        if (isMaskedByTtd(kv.second.id))
            out.back().enabled = false;
    }

    return out;
}

/// @brief Set all features to their default values (for startup/reset).
/// Registers default features with their initial states.
void FeatureManager::setDefaults()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // Example: register default features here. Extend as needed.
    clear();

    registerFeature({Features::kDebugMode,
                     Features::kDebugModeAlias,
                     Features::kDebugModeDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kMemoryTracking,
                     Features::kMemoryTrackingAlias,
                     Features::kMemoryTrackingDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryAnalysis});
    registerFeature({Features::kBreakpoints,
                     Features::kBreakpointsAlias,
                     Features::kBreakpointsDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kCallTrace,
                     Features::kCallTraceAlias,
                     Features::kCallTraceDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryAnalysis});
    registerFeature({Features::kSoundGeneration,
                     Features::kSoundGenerationAlias,
                     Features::kSoundGenerationDesc,
                     true,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kSoundHQ,
                     Features::kSoundHQAlias,
                     Features::kSoundHQDesc,
                     true,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kScreenHQ,
                     Features::kScreenHQAlias,
                     Features::kScreenHQDesc,
                     true,  // ON by default - demo compatibility
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kRecording,
                     Features::kRecordingAlias,
                     Features::kRecordingDesc,
                     false,  // OFF by default - heavy functionality
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kSharedMemory,
                     Features::kSharedMemoryAlias,
                     Features::kSharedMemoryDesc,
                     false,  // OFF by default - opt-in for external tool access
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kOpcodeProfiler,
                     Features::kOpcodeProfilerAlias,
                     Features::kOpcodeProfilerDesc,
                     false,  // OFF by default - analysis feature
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryAnalysis});
    registerFeature({Features::kTimeTravel,
                     Features::kTimeTravelAlias,
                     Features::kTimeTravelDesc,
                     false,  // OFF by default - heavy feature, opt-in
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kOverscan,
                     Features::kOverscanAlias,
                     Features::kOverscanDesc,
                     false,  // OFF by default - Pentagon only, demo development
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kPortTrace,
                     Features::kPortTraceAlias,
                     Features::kPortTraceDesc,
                     false,  // OFF by default - diagnostic feature, opt-in
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kFastTape,
                     Features::kFastTapeAlias,
                     Features::kFastTapeDesc,
                     true,  // ON by default - instant loads are the expected behavior
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kTurboTape,
                     Features::kTurboTapeAlias,
                     Features::kTurboTapeDesc,
                     true,  // ON by default - the trap stays instant for vanilla blocks; warp picks up
                            // every signal-path block and ends itself (read-gap watchdog / end-of-tape)
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kFastDisk,
                     Features::kFastDiskAlias,
                     Features::kFastDiskDesc,
                     true,  // ON by default - compressed FDC timing and ROM loop traps
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kTurboMode,
                     Features::kTurboModeAlias,
                     Features::kTurboModeDesc,
                     false,  // OFF by default - opt-in max-speed run
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kHud,
                     Features::kHudAlias,
                     Features::kHudDesc,
                     false,  // OFF by default - opt-in on UI and automation
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kKempstonMouse,
                     Features::kKempstonMouseAlias,
                     Features::kKempstonMouseDesc,
                     true,  // ON by default - whether a mouse is fitted is decided by the machine config
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kContention,
                     Features::kContentionAlias,
                     Features::kContentionDesc,
                     true,  // ON by default - the machine's hardware timing
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    registerFeature({Features::kGSLightweight,
                     Features::kGSLightweightAlias,
                     Features::kGSLightweightDesc,
                     false,  // OFF by default - [SOUND] GSType decides the fitted personality
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    _dirty = false;
}

/// @brief Load feature states from features.ini (UTF-8). If missing, uses defaults.
/// @param path Path to the features.ini file
void FeatureManager::loadFromFile(const std::string& path)
{
    if (!std::filesystem::exists(FileHelper::ToFsPath(path)))
    {
        return;
    }

    IniFile ini;
    if (!ini.LoadFile(path))
    {
        std::cerr << "Failed to load " << path << std::endl;
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        // Traverse all sections (feature ids) in the file
        const std::vector<std::string> sections = ini.GetAllSections();
        for (const std::string& section : sections)
        {
            auto it = _features.find(section);
            if (it == _features.end())
                continue;  // Only override registered features
            FeatureInfo& f = it->second;

            const char* state = ini.GetValue(section.c_str(), "state", nullptr);
            if (state)
            {
                std::string s = state;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                f.enabled = (s == Features::kStateOn || s == "true" || s == "1");
            }

            const char* mode = ini.GetValue(section.c_str(), "mode", nullptr);
            if (mode)
            {
                f.mode = mode;
            }
        }

        // Features state fully match the settings file
        _dirty = false;
    }

    // Recalculate all cached flags (empty featureId = bulk reload from file)
    onFeatureChanged();
}

/// @brief Save current feature states to features.ini (UTF-8).
/// @param path Path where to save the features.ini file
void FeatureManager::saveToFile(const std::string& path) const
{
    IniFile ini;

    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& [id, f] : _features)
        {
            ini.SetValue(id.c_str(), "state", f.enabled ? Features::kStateOn : Features::kStateOff);
            ini.SetValue(id.c_str(), "mode", f.mode.c_str());
        }

        _dirty = false;
    }

    if (!ini.SaveFile(path))
    {
        std::cerr << "Failed to save " << path << std::endl;
    }
}

/// @brief Call when a feature state or mode changes. Triggers save if needed.
/// Posts NC_FEATURE_CHANGED as the last step so consumers see consistent cached state.
/// @param changedFeatureId The canonical ID of the feature that changed, or empty for bulk reload.
void FeatureManager::onFeatureChanged(const std::string& changedFeatureId)
{
    // Update the feature cache in Memory class if it exists
    if (_context && _context->pCore && _context->pCore->GetMemory())
    {
        _context->pCore->GetMemory()->UpdateFeatureCache();

        // Synchronize master switch with feature changes
        bool debugEnabled;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto debugIt = _features.find(Features::kDebugMode);
            debugEnabled = debugIt != _features.end() && debugIt->second.enabled;
        }
        _context->pCore->GetZ80()->isDebugMode = debugEnabled;

        // Switch memory interface based on debug mode and the machine's contention (with its switch)
        _context->pCore->SetContentionSwitch(isEnabled(Features::kContention));  // re-selects the interface

        // Update Z80 feature cache (opcode profiler etc.)
        _context->pCore->GetZ80()->UpdateFeatureCache();
    }

    // Update feature cache in SoundManager if it exists
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->UpdateFeatureCache();
    }

#ifdef ENABLE_RECORDING
    // Update feature cache in RecordingManager if it exists
    if (_context && _context->pRecordingManager)
    {
        _context->pRecordingManager->UpdateFeatureCache();
    }
#endif

    // Kempston Mouse fitting follows the kempstonmouse feature
    if (_context && _context->pMouse)
    {
        _context->pMouse->ApplyConfiguration();
    }

    // Update feature cache in Screen (for ScreenHQ toggle) if it exists
    if (_context && _context->pScreen)
    {
        _context->pScreen->UpdateFeatureCache();
    }

    // Notify TTD manager of feature changes (for memory deallocation on disable)
    if (_context && _context->pTimeTravelManager)
    {
        _context->pTimeTravelManager->UpdateFeatureCache();
    }

    // Update port trace recorder cache in PortDecoder (instantiates/releases the
    // recorder when the porttrace feature is toggled)
    if (_context && _context->pPortDecoder)
    {
        _context->pPortDecoder->UpdateFeatureCache();
    }

    // General turbo (max speed) mode: push the desired state into Core when the
    // 'turbomode' feature itself changes, or when TTD recording starts/stops
    // (which changes 'timetravel'). Deliberately NOT run on every feature change:
    // other code (TapeTurboController) engages/disengages Core turbo directly for
    // its own reasons, and an unconditional sync here would stomp on that turbo
    // state every time an unrelated feature is toggled.
    if (changedFeatureId == Features::kTurboMode || changedFeatureId == Features::kTimeTravel)
    {
        syncTurboModeWithTtdState();
    }

    if (_dirty)
    {
        saveToFile(kFeaturesIni);
    }

    // Post NC_FEATURE_CHANGED — AFTER all caches are updated and state is persisted.
    // Consumers see consistent cached state when their observer fires.
    if (_context)
    {
        bool featureEnabled = false;
        if (!changedFeatureId.empty())
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            const auto* f = findFeature(changedFeatureId);
            featureEnabled = f ? f->enabled : false;
        }

        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        // Observers only read the payload inside their handler (verified:
        // HudModel and the notification tests copy the values out), so
        // ownership can be transferred to the dispatched Message
        messageCenter.Post(NC_FEATURE_CHANGED,
            new FeatureChangedPayload(_context->emulatorId, changedFeatureId, featureEnabled), true);
    }
}

/// @brief Find a feature by id or alias (mutable).
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Pointer to the FeatureInfo if found, nullptr otherwise
FeatureManager::FeatureInfo* FeatureManager::findFeature(const std::string& idOrAlias)
{
    // Try to find the feature by its canonical id
    auto it = _features.find(idOrAlias);
    if (it != _features.end())
        return &it->second;

    // If not found, try to resolve as an alias
    auto ait = _aliases.find(idOrAlias);
    if (ait != _aliases.end())
    {
        // Look up the canonical id from the alias and return the feature if it exists
        auto fit = _features.find(ait->second);
        if (fit != _features.end())
            return &fit->second;
    }

    // Feature not found
    return nullptr;
}

/// @brief Find a feature by id or alias (const).
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Pointer to the const FeatureInfo if found, nullptr otherwise
const FeatureManager::FeatureInfo* FeatureManager::findFeature(const std::string& idOrAlias) const
{
    // Try to find the feature by its canonical id
    auto it = _features.find(idOrAlias);
    if (it != _features.end())
        return &it->second;

    // If not found, try to resolve as an alias
    auto ait = _aliases.find(idOrAlias);
    if (ait != _aliases.end())
    {
        // Look up the canonical id from the alias and return the feature if it exists
        auto fit = _features.find(ait->second);
        if (fit != _features.end())
            return &fit->second;
    }

    // Feature not found
    return nullptr;
}