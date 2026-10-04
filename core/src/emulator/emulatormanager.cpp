#include "emulatormanager.h"

#include "emulator/machinevariants.h"
#include "emulator/zxpoly/zxpolygroup.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "stdafx.h"
#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/message-center/eventqueue.h"
#include "emulator/mainloop.h"
#include "emulator/notifications.h"
#include "emulator/video/screen.h"

#include <algorithm>
#include <iomanip>

// Implementation of EmulatorManager methods

namespace
{
// Store the failure reason for the caller (WebAPI 400 / CLI error message)
// when it asked for one. Kept trivial so failure paths stay allocation-light.
void SetCreateError(std::string* outError, const std::string& reason)
{
    if (outError != nullptr)
    {
        *outError = reason;
    }
}

// Decode the AvailRAMs bitmask (bits encode KB sizes directly: RAM_128 == 128)
// into a "128/512/1024" style list for error messages.
std::string SupportedRamList(unsigned availRams)
{
    static const unsigned knownRams[] = { 48, 128, 256, 512, 1024, 2048, 4096 };

    std::string result;
    for (unsigned ram : knownRams)
    {
        if ((availRams & ram) != 0)
        {
            if (!result.empty())
            {
                result += "/";
            }
            result += std::to_string(ram);
        }
    }
    return result;
}
}

void EmulatorManager::UpdateRealtimeScheduling()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    UpdateRealtimeSchedulingLocked();
}

void EmulatorManager::UpdateRealtimeSchedulingLocked()
{
    std::string selectedId;
    {
        std::lock_guard<std::mutex> selLock(_selectionMutex);
        selectedId = _selectedEmulatorId;
    }

    // Stateless fallback (the same rule CLI/WebAPI resolution uses to pick
    // "the" emulator): with no explicit selection the sole instance IS the
    // active one. This covers the GUI flow, which creates its single
    // instance through the manager but never calls SetSelectedEmulatorId.
    // With several instances and no selection none is active - no thread
    // holds real-time priority
    if (selectedId.empty() && _emulators.size() == 1)
        selectedId = _emulators.begin()->first;

    for (const auto& pair : _emulators)
    {
        MainLoop* mainLoop = pair.second ? pair.second->GetMainLoop() : nullptr;
        if (mainLoop)
            mainLoop->SetRealtimeRequested(pair.first == selectedId);
    }
}

std::shared_ptr<Emulator> EmulatorManager::CreateEmulator(const std::string& symbolicId, LoggerLevel level,
                                                          std::function<void(CONFIG&)> configOverride)
{
    // Block new emulator creation during shutdown
    if (_isShuttingDown.load())
    {
        LOGWARNING("EmulatorManager::CreateEmulator - Blocked during shutdown");
        return nullptr;
    }

    // Create a new emulator with an auto-generated UUID
    auto emulator = std::make_shared<Emulator>(symbolicId, level);
    if (configOverride)
        emulator->SetConfigOverride(std::move(configOverride));

    // Initialize the emulator
    if (emulator->Init())
    {
        std::lock_guard<std::mutex> lock(_emulatorsMutex);
        std::string uuid = emulator->GetUUID();
        _emulators[uuid] = emulator;
        emulator->SetState(StateInitialized);

        /////////////////////////////////////////////////////////////////////////////////////////
        // DISABLE ALL MODULAR LOGGING FOR AUTOMATION-CREATED EMULATOR INSTANCES
        //
        // Automation interfaces (WebAPI, CLI) create emulator instances that should not
        // produce verbose internal logging output. This block explicitly disables all
        // modular logging sources from all modules to prevent noisy output like:
        // - [I/O_In] Info: [In] [PC:0296 ROM_3] Port: FE; Value: BF
        // - [I/O_Generic] Warning: [In] [PC:0296 ROM_3] Port: BFFE - no peripheral device to handle
        // - [Core_Generic] Info: tState counter after the frame: 71685
        //
        // This ensures clean automation output without internal emulator noise.
        /////////////////////////////////////////////////////////////////////////////////////////
        {
            EmulatorContext* context = emulator->GetContext();
            if (context && context->pModuleLogger)
            {
                context->pModuleLogger->TurnOffLoggingForAll();
                LOGINFO("EmulatorManager::CreateEmulator - Disabled all modular logging for automation instance");
            }
        }

        LOGINFO("EmulatorManager::CreateEmulator - Created emulator with UUID: %s, Symbolic ID: '%s'",
              uuid.c_str(),
              symbolicId.empty() ? "[none]" : symbolicId.c_str());

        // Emit notification that instance was created
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleTextPayload* payload = new SimpleTextPayload(uuid);
        messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

        // Sole-instance fallback may make this instance the active one
        UpdateRealtimeSchedulingLocked();

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulator - Failed to initialize emulator");
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::CreateEmulatorWithId(const std::string& emulatorId, const std::string& symbolicId, LoggerLevel level)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Check if an emulator with this UUID already exists
    if (_emulators.find(emulatorId) != _emulators.end())
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithId - Emulator with UUID '%s' already exists", emulatorId.c_str());
        return nullptr;
    }

    // Create a new emulator instance with the specified UUID
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Initialize the emulator
    if (emulator->Init())
    {
        _emulators[emulatorId] = emulator;
        emulator->SetState(StateInitialized);

        /////////////////////////////////////////////////////////////////////////////////////////
        // DISABLE ALL MODULAR LOGGING FOR AUTOMATION-CREATED EMULATOR INSTANCES
        //
        // Automation interfaces (WebAPI, CLI) create emulator instances that should not
        // produce verbose internal logging output. This block explicitly disables all
        // modular logging sources from all modules to prevent noisy output like:
        // - [I/O_In] Info: [In] [PC:0296 ROM_3] Port: FE; Value: BF
        // - [I/O_Generic] Warning: [In] [PC:0296 ROM_3] Port: BFFE - no peripheral device to handle
        // - [Core_Generic] Info: tState counter after the frame: 71685
        //
        // This ensures clean automation output without internal emulator noise.
        /////////////////////////////////////////////////////////////////////////////////////////
        {
            EmulatorContext* context = emulator->GetContext();
            if (context && context->pModuleLogger)
            {
                context->pModuleLogger->TurnOffLoggingForAll();
                LOGINFO("EmulatorManager::CreateEmulatorWithId - Disabled all modular logging for automation instance");
            }
        }

        LOGINFO("EmulatorManager::CreateEmulatorWithId - Created emulator with UUID: %s, Symbolic ID: '%s'",
              emulatorId.c_str(),
              symbolicId.empty() ? "[none]" : symbolicId.c_str());

        // Emit notification that instance was created
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleTextPayload* payload = new SimpleTextPayload(emulatorId);
        messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

        // Sole-instance fallback may make this instance the active one
        UpdateRealtimeSchedulingLocked();

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithId - Failed to initialize emulator with UUID: %s", emulatorId.c_str());
    return nullptr;
}

namespace
{

/// The config override of a machine variant: its board configuration, then the
/// caller's override
std::function<void(CONFIG&)> VariantOverride(const MachineVariant& variant, std::function<void(CONFIG&)> caller)
{
    return [apply = variant.apply, caller = std::move(caller)](CONFIG& config) {
        apply(config);
        if (caller)
            caller(config);
    };
}

} // namespace

std::shared_ptr<Emulator> EmulatorManager::CreateEmulatorWithModel(const std::string& symbolicId, const std::string& modelName, LoggerLevel level, std::string* outError, std::function<void(CONFIG&)> configOverride)
{
    // A ZX-Poly configuration name creates the whole group; checked before the
    // lock, since the group creates its members through this method
    if (ZXPolyGroup::FindConfiguration(modelName))
        return CreateZXPolyMachine(symbolicId, modelName, "", outError, std::move(configOverride));

    // A machine variant: its base model with the variant's board configuration
    // (the caller's override, if any, applies after it)
    if (const MachineVariant* variant = MachineVariants::Find(modelName))
    {
        std::string reason;
        if (!variant->supported(&reason))
        {
            SetCreateError(outError, "'" + std::string(variant->name) + "' is not available: " + reason);
            return nullptr;
        }
        return CreateEmulatorWithModelAndRAM(symbolicId, variant->baseModel, variant->ramKb, level, outError,
                                             VariantOverride(*variant, std::move(configOverride)));
    }

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create a new emulator instance
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Find the model configuration (static lookup, no context needed)
    const TMemModel* modelInfo = Config::FindModelByShortName(modelName);
    if (!modelInfo)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModel - Unknown model: '%s'", modelName.c_str());
        SetCreateError(outError, "unknown model '" + modelName + "' (see GET /api/v1/emulator/models)");
        return nullptr;
    }

    // Request this model for initialization: Emulator::Init applies it right
    // after config load, before any model-dependent subsystem initializes
    emulator->SetPreferredModel(modelInfo->Model, modelInfo->defaultRAM);
    if (configOverride)
        emulator->SetConfigOverride(std::move(configOverride));

    // Initialize the emulator. A model the build cannot construct (missing
    // port decoder, missing device support) throws std::logic_error out of
    // Emulator::Init - translate it into a reason instead of letting the
    // exception escape into HTTP handlers.
    bool initialized = false;
    try
    {
        initialized = emulator->Init();
    }
    catch (const std::exception& e)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModel - Model '%s' rejected by this build: %s",
                 modelName.c_str(), e.what());
        SetCreateError(outError, "model '" + modelName + "' is not supported by this build (" + e.what() + ")");
        return nullptr;
    }

    if (initialized)
    {
        std::string uuid = emulator->GetId();
        _emulators[uuid] = emulator;
        emulator->SetState(StateInitialized);

        /////////////////////////////////////////////////////////////////////////////////////////
        // DISABLE ALL MODULAR LOGGING FOR AUTOMATION-CREATED EMULATOR INSTANCES
        //
        // Automation interfaces (WebAPI, CLI) create emulator instances that should not
        // produce verbose internal logging output. This block explicitly disables all
        // modular logging sources from all modules to prevent noisy output like:
        // - [I/O_In] Info: [In] [PC:0296 ROM_3] Port: FE; Value: BF
        // - [I/O_Generic] Warning: [In] [PC:0296 ROM_3] Port: BFFE - no peripheral device to handle
        // - [Core_Generic] Info: tState counter after the frame: 71685
        //
        // This ensures clean automation output without internal emulator noise.
        /////////////////////////////////////////////////////////////////////////////////////////
        {
            EmulatorContext* context = emulator->GetContext();
            if (context && context->pModuleLogger)
            {
                context->pModuleLogger->TurnOffLoggingForAll();
                LOGINFO("EmulatorManager::CreateEmulatorWithModel - Disabled all modular logging for automation instance");
            }
        }

        LOGINFO("EmulatorManager::CreateEmulatorWithModel - Created emulator with UUID: %s, Symbolic ID: '%s', Model: '%s'",
                uuid.c_str(),
                symbolicId.empty() ? "[none]" : symbolicId.c_str(),
                modelInfo->FullName);

        // Emit notification that instance was created
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleTextPayload* payload = new SimpleTextPayload(uuid);
        messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

        // Sole-instance fallback may make this instance the active one
        UpdateRealtimeSchedulingLocked();

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithModel - Failed to initialize emulator with model: '%s'", modelName.c_str());
    SetCreateError(outError,
                   "initialization failed for model '" + modelName + "' (config folder '" +
                       Config::GetConfigFolderForModel(modelInfo->Model, modelInfo->defaultRAM) +
                       "' missing, or ROM files failed to load)");
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::CreateEmulatorWithModelAndRAM(const std::string& symbolicId, const std::string& modelName, uint32_t ramSize, LoggerLevel level, std::string* outError, std::function<void(CONFIG&)> configOverride)
{
    // A ZX-Poly configuration fixes its modules' memory (the base model's default)
    if (ZXPolyGroup::FindConfiguration(modelName))
    {
        SetCreateError(outError, "'" + modelName + "' is a ZX-Poly configuration: its RAM size is fixed, omit ram_size");
        return nullptr;
    }

    // A machine variant runs with its base model's fixed RAM size
    if (const MachineVariant* variant = MachineVariants::Find(modelName))
    {
        if (ramSize != variant->ramKb)
        {
            SetCreateError(outError, "'" + std::string(variant->name) + "' runs with " + std::to_string(variant->ramKb) +
                                         "KB RAM: omit ram_size or pass " + std::to_string(variant->ramKb));
            return nullptr;
        }
        std::string reason;
        if (!variant->supported(&reason))
        {
            SetCreateError(outError, "'" + std::string(variant->name) + "' is not available: " + reason);
            return nullptr;
        }
        return CreateEmulatorWithModelAndRAM(symbolicId, variant->baseModel, ramSize, level, outError,
                                             VariantOverride(*variant, std::move(configOverride)));
    }

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create a new emulator instance
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Find the model configuration (static lookup, no context needed)
    const TMemModel* modelInfo = Config::FindModelByShortName(modelName);
    if (!modelInfo)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - Unknown model: '%s'", modelName.c_str());
        SetCreateError(outError, "unknown model '" + modelName + "' (see GET /api/v1/emulator/models)");
        return nullptr;
    }

    // Validate RAM size for this model
    if ((ramSize & modelInfo->AvailRAMs) == 0)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - RAM size %dKB not supported by model '%s'",
                ramSize, modelName.c_str());
        SetCreateError(outError,
                       "RAM size " + std::to_string(ramSize) + "KB is not supported by model '" + modelName +
                           "' (supported: " + SupportedRamList(modelInfo->AvailRAMs) + "KB)");
        return nullptr;
    }

    // Request this model and RAM size for initialization. Emulator::Init
    // resolves the model config itself: configs/<model>/unreal.ini
    emulator->SetPreferredModel(modelInfo->Model, ramSize);
    if (configOverride)
        emulator->SetConfigOverride(std::move(configOverride));

    // Initialize the emulator. A model the build cannot construct (missing
    // port decoder, missing device support) throws std::logic_error out of
    // Emulator::Init - translate it into a reason instead of letting the
    // exception escape into HTTP handlers.
    bool initialized = false;
    try
    {
        initialized = emulator->Init();
    }
    catch (const std::exception& e)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - Model '%s' rejected by this build: %s",
                 modelName.c_str(), e.what());
        SetCreateError(outError, "model '" + modelName + "' is not supported by this build (" + e.what() + ")");
        return nullptr;
    }

    if (initialized)
    {
        std::string uuid = emulator->GetId();
        _emulators[uuid] = emulator;
        emulator->SetState(StateInitialized);

        /////////////////////////////////////////////////////////////////////////////////////////
        // DISABLE ALL MODULAR LOGGING FOR AUTOMATION-CREATED EMULATOR INSTANCES
        //
        // Automation interfaces (WebAPI, CLI) create emulator instances that should not
        // produce verbose internal logging output. This block explicitly disables all
        // modular logging sources from all modules to prevent noisy output like:
        // - [I/O_In] Info: [In] [PC:0296 ROM_3] Port: FE; Value: BF
        // - [I/O_Generic] Warning: [In] [PC:0296 ROM_3] Port: BFFE - no peripheral device to handle
        // - [Core_Generic] Info: tState counter after the frame: 71685
        //
        // This ensures clean automation output without internal emulator noise.
        /////////////////////////////////////////////////////////////////////////////////////////
        {
            EmulatorContext* context = emulator->GetContext();
            if (context && context->pModuleLogger)
            {
                context->pModuleLogger->TurnOffLoggingForAll();
                LOGINFO("EmulatorManager::CreateEmulatorWithModelAndRAM - Disabled all modular logging for automation instance");
            }
        }

        LOGINFO("EmulatorManager::CreateEmulatorWithModelAndRAM - Created emulator with UUID: %s, Symbolic ID: '%s', Model: '%s', RAM: %dKB",
                uuid.c_str(),
                symbolicId.empty() ? "[none]" : symbolicId.c_str(),
                modelInfo->FullName,
                ramSize);

        // Emit notification that instance was created
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleTextPayload* payload = new SimpleTextPayload(uuid);
        messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

        // Sole-instance fallback may make this instance the active one
        UpdateRealtimeSchedulingLocked();

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - Failed to initialize emulator with model: '%s', RAM: %dKB",
            modelName.c_str(), ramSize);
    SetCreateError(outError,
                   "initialization failed for model '" + modelName + "' with " + std::to_string(ramSize) +
                       "KB RAM (config folder '" +
                       Config::GetConfigFolderForModel(modelInfo->Model, ramSize) +
                       "' missing, or ROM files failed to load)");
    return nullptr;
}

std::vector<TMemModel> EmulatorManager::GetAvailableModels() const
{
    // Model list is static data, access directly
    return Config::GetAvailableModels();
}

MachineIdentity EmulatorManager::GetMachineIdentity(Emulator& emulator)
{
    MachineIdentity identity;

    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr)
    {
        return identity;  // Valid stays false; callers serialize null/unknown fields
    }

    identity.Valid = true;

    const CONFIG& config = context->config;
    if (const MachineVariant* variant = MachineVariants::Of(config))
    {
        identity.Variant = variant->name;
        identity.VariantTitle = variant->title;
    }

    // Use direct model lookup instead of iterating a temporary vector
    // (the vector copies struct with pointers, which should be safe, but
    // this avoids potential issues with constexpr string literals on iOS)
    const TMemModel* modelInfo = Config::FindModelByEnum(config.mem_model);

    if (modelInfo != nullptr && modelInfo->ShortName != nullptr)
    {
        try { identity.Model = std::string(modelInfo->ShortName); }
        catch (...) { identity.Model = "unknown"; }
    }
    else
    {
        identity.Model = "unknown";
    }

    if (modelInfo != nullptr && modelInfo->FullName != nullptr)
    {
        try { identity.ModelFullName = std::string(modelInfo->FullName); }
        catch (...) { identity.ModelFullName = "unknown"; }
    }
    else
    {
        identity.ModelFullName = "unknown";
    }
    identity.RamKb = config.ramsize;
    if (context->pScreen != nullptr)
    {
        identity.VideoMode = Screen::GetVideoModeName(context->pScreen->GetVideoMode());
        identity.HasVideoMode = true;
    }
    // CPU T per base T: the multiplier over the clock denominator (the Profi in hi-res runs 10/7 of 3.5 MHz)
    identity.SpeedMultiplier = static_cast<double>(context->emulatorState.current_z80_frequency_multiplier) /
                               static_cast<double>(context->emulatorState.ClockDen());
    identity.ConfigFolder = Config::GetConfigFolderForModel(config.mem_model, config.ramsize);
    identity.RamPowerOn = Config::RamPowerOnName(config.ramPowerOn);

    if (ZXPolyGroup* group = GetInstance()->GetZXPolyGroup(emulator.GetId()))
    {
        const ZXPolyGroup::Status status = group->GetStatus();
        identity.ZXPoly = true;
        identity.ZXPolyMasterId = status.memberIds[0];
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        {
            if (status.memberIds[m] == emulator.GetId())
                identity.ZXPolyModule = static_cast<int>(m);
        }
        identity.ZXPolyLocked = status.locked;
        identity.ZXPolyVideoMode = status.videoMode;
    }

    return identity;
}

std::shared_ptr<Emulator> EmulatorManager::CreateZXPolyMachine(const std::string& symbolicId,
                                                               const std::string& modelName,
                                                               const std::string& mediaPath, std::string* outError,
                                                               std::function<void(CONFIG&)> configOverride)
{
    auto group = std::make_shared<ZXPolyGroup>(symbolicId.empty() ? std::string("zxpoly") : symbolicId);
    std::string error;
    if (!group->Create(modelName, &error, configOverride) || !group->LoadMedia(mediaPath, &error))
    {
        if (outError)
            *outError = error;
        return nullptr;
    }

    group->AttachToMaster();
    std::shared_ptr<Emulator> master = group->GetMaster();
    {
        std::lock_guard<std::recursive_mutex> lock(_zxpolyMutex);
        _zxpolyGroups[master->GetId()] = group;
    }
    return master;
}

ZXPolyGroup* EmulatorManager::GetZXPolyGroup(const std::string& emulatorId)
{
    std::lock_guard<std::recursive_mutex> lock(_zxpolyMutex);
    for (auto& [masterId, group] : _zxpolyGroups)
    {
        const ZXPolyGroup::Status status = group->GetStatus();
        for (const std::string& id : status.memberIds)
        {
            if (id == emulatorId)
                return group.get();
        }
    }
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::GetEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        return it->second;
    }

    LOGDEBUG("EmulatorManager::GetEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::GetEmulatorByIndex(int index)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create sorted list by creation time (hidden group members have no index)
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> emulatorTimestamps;
    for (const auto& pair : _emulators)
    {
        if (!pair.second->IsHiddenGroupMember())
            emulatorTimestamps.push_back({pair.first, pair.second->GetCreationTime()});
    }

    if (index < 0 || index >= static_cast<int>(emulatorTimestamps.size()))
    {
        LOGDEBUG("EmulatorManager::GetEmulatorByIndex - Invalid index %d (valid range: 0-%d)",
                index, static_cast<int>(emulatorTimestamps.size()) - 1);
        return nullptr;
    }

    // Sort by creation time (earlier = lower index)
    std::sort(emulatorTimestamps.begin(), emulatorTimestamps.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Get the ID at the specified index
    std::string emulatorId = emulatorTimestamps[index].first;
    return _emulators[emulatorId];
}

std::vector<std::string> EmulatorManager::GetEmulatorIds()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create vector of (id, creation_time) pairs
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> emulatorTimestamps;
    for (const auto& pair : _emulators)
    {
        // Hidden group members (ZX-Poly slaves) are reached through their
        // visible master, not listed as machines of their own
        if (!pair.second->IsHiddenGroupMember())
            emulatorTimestamps.push_back({pair.first, pair.second->GetCreationTime()});
    }

    // Sort by creation time (earlier = lower index)
    std::sort(emulatorTimestamps.begin(), emulatorTimestamps.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Extract just the IDs in sorted order
    std::vector<std::string> ids;
    for (const auto& pair : emulatorTimestamps)
    {
        ids.push_back(pair.first);
    }

    return ids;
}

bool EmulatorManager::HasEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    return _emulators.find(emulatorId) != _emulators.end();
}

bool EmulatorManager::RemoveEmulator(const std::string& emulatorId)
{
    // A ZX-Poly master takes its group along: unhook the group from the
    // master's loop, remove the master, then the group removes its slaves
    std::shared_ptr<ZXPolyGroup> group;
    {
        std::lock_guard<std::recursive_mutex> lock(_zxpolyMutex);
        auto it = _zxpolyGroups.find(emulatorId);
        if (it != _zxpolyGroups.end())
        {
            group = std::move(it->second);
            _zxpolyGroups.erase(it);
        }
    }
    if (group)
        group->DetachFromMaster();

    const bool removed = RemoveEmulatorInstance(emulatorId);
    group.reset();    // ~ZXPolyGroup removes the slaves (the master is already gone)
    return removed;
}

bool EmulatorManager::RemoveEmulatorInstance(const std::string& emulatorId)
{
    // Notify observers BEFORE the instance is stopped and freed. All message
    // handlers run on the single MessageCenter worker thread, so this destroy
    // notification is dispatched before any message posted later - observers
    // holding raw context pointers (e.g. HUD models) can detach while the
    // context is still alive. Posting alone is not enough: notifications
    // queued AHEAD of the destroy notice must also finish dispatching while
    // the context is alive, so wait for the queue to drain before freeing
    // anything. The wait happens before _emulatorsMutex is taken - a handler
    // that re-enters EmulatorManager would otherwise deadlock on our lock.
    //
    // Context leases (Emulator::LeaseContext): refuse new ones before anyone
    // hears about the removal, then - after the drain, with no lock held -
    // wait for the live ones. A UI thread reading the context (adopting the
    // instance, opening a menu) finishes before the context is freed.
    std::shared_ptr<Emulator> retiring = GetEmulator(emulatorId);
    if (retiring)
        retiring->BeginRetirement();
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_EMULATOR_INSTANCE_DESTROYED, new SimpleTextPayload(emulatorId), true);
        if (!messageCenter.WaitForOutstandingMessages())
        {
            LOGWARNING("EmulatorManager::RemoveEmulator - Message queue drain timed out for '%s'; observers may race the instance free", emulatorId.c_str());
        }
    }
    if (retiring)
        retiring->WaitForContextLeases();

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        // Always go through Stop(), even when IsRunning() already reads false:
        // Stop() is a blocking barrier (it returns only once the emulation
        // thread has actually joined), while IsRunning() alone can go false
        // before that join completes if another caller is concurrently
        // stopping the same instance. Gating this on IsRunning() let Release()
        // free Core's Screen / SoundManager / TimeTravelManager while that
        // thread was still mid-frame (2026-10-02 crash).
        LOGINFO("EmulatorManager::RemoveEmulator - Stopping emulator with ID '%s'", emulatorId.c_str());
        it->second->Stop();

        // Release resources
        it->second->Release();

        // Remove from map
        _emulators.erase(it);
        LOGINFO("EmulatorManager::RemoveEmulator - Removed emulator with ID '%s'", emulatorId.c_str());

        // Clear selection if this was the selected emulator
        {
            std::lock_guard<std::mutex> selLock(_selectionMutex);
            if (_selectedEmulatorId == emulatorId)
            {
                std::string previousId = _selectedEmulatorId;
                _selectedEmulatorId = "";

                // Send notification about selection being cleared
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, "");
                messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

                LOGINFO("EmulatorManager::RemoveEmulator - Cleared selection (was pointing to removed emulator)");
            }
        }

        // The surviving sole instance (if any) may become the active one
        UpdateRealtimeSchedulingLocked();

        return true;
    }

    LOGDEBUG("EmulatorManager::RemoveEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

// Lifecycle control methods
bool EmulatorManager::StartEmulator(const std::string& emulatorId)
{
    // Block state changes during shutdown
    if (_isShuttingDown.load())
    {
        LOGWARNING("EmulatorManager::StartEmulator - Blocked during shutdown");
        return false;
    }

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (!it->second->IsRunning())
        {
            it->second->Start();
            LOGINFO("EmulatorManager::StartEmulator - Started emulator with ID '%s'", emulatorId.c_str());
            return true;
        }
        else
        {
            LOGDEBUG("EmulatorManager::StartEmulator - Emulator with ID '%s' already running", emulatorId.c_str());
            return false;
        }
    }

    LOGDEBUG("EmulatorManager::StartEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

bool EmulatorManager::StartEmulatorAsync(const std::string& emulatorId)
{
    // Block state changes during shutdown
    if (_isShuttingDown.load())
    {
        LOGWARNING("EmulatorManager::StartEmulatorAsync - Blocked during shutdown");
        return false;
    }

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (!it->second->IsRunning())
        {
            it->second->StartAsync();
            LOGINFO("EmulatorManager::StartEmulatorAsync - Started emulator async with ID '%s'", emulatorId.c_str());
            
            // Re-evaluate selection after start
            // Only change selection if current selection is invalid (stopped or non-existent)
            bool shouldAutoSelect = false;
            std::string currentSelection;
            {
                std::lock_guard<std::mutex> selLock(_selectionMutex);
                currentSelection = _selectedEmulatorId;
                
                // Check if current selection is valid (exists and not stopped)
                bool selectionIsValid = false;
                if (!currentSelection.empty())
                {
                    auto selIt = _emulators.find(currentSelection);
                    if (selIt != _emulators.end())
                    {
                        // Valid if running or paused (not stopped)
                        selectionIsValid = selIt->second->IsRunning() || selIt->second->IsPaused();
                    }
                }
                
                // Auto-select if no valid selection
                shouldAutoSelect = !selectionIsValid;
            }
            
            if (shouldAutoSelect)
            {
                std::lock_guard<std::mutex> selLock(_selectionMutex);
                std::string previousId = _selectedEmulatorId;
                _selectedEmulatorId = emulatorId;
                
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, emulatorId);
                messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);
                LOGINFO("EmulatorManager::StartEmulatorAsync - Auto-selected emulator '%s' (previous selection was invalid)", emulatorId.c_str());
            }

            // Propagate the (possibly new) selection to the thread priorities
            UpdateRealtimeSchedulingLocked();

            return true;
        }
        else
        {
            LOGDEBUG("EmulatorManager::StartEmulatorAsync - Emulator with ID '%s' already running", emulatorId.c_str());
            return false;
        }
    }

    LOGDEBUG("EmulatorManager::StartEmulatorAsync - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

bool EmulatorManager::StopEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (it->second->IsRunning())
        {
            it->second->Stop();
            LOGINFO("EmulatorManager::StopEmulator - Stopped emulator with ID '%s'", emulatorId.c_str());
            
            // Re-evaluate selection after stop
            // If we stopped the selected emulator, try to select another running/paused one
            bool needsReselection = false;
            {
                std::lock_guard<std::mutex> selLock(_selectionMutex);
                needsReselection = (_selectedEmulatorId == emulatorId);
            }
            
            if (needsReselection)
            {
                // Find another running or paused emulator to select
                std::string newSelection;
                for (const auto& pair : _emulators)
                {
                    if (pair.first != emulatorId && 
                        (pair.second->IsRunning() || pair.second->IsPaused()))
                    {
                        newSelection = pair.first;
                        break;
                    }
                }
                
                // Update selection (may be empty if no other valid emulators)
                {
                    std::lock_guard<std::mutex> selLock(_selectionMutex);
                    std::string previousId = _selectedEmulatorId;
                    _selectedEmulatorId = newSelection;
                    
                    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                    EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, newSelection);
                    messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);
                    
                    if (newSelection.empty())
                    {
                        LOGINFO("EmulatorManager::StopEmulator - Cleared selection (no other running/paused emulators)");
                    }
                    else
                    {
                        LOGINFO("EmulatorManager::StopEmulator - Auto-selected alternative emulator: '%s'", newSelection.c_str());
                    }
                }
            }

            // Propagate the (possibly re-selected) selection to the thread priorities
            UpdateRealtimeSchedulingLocked();

            return true;
        }
        else
        {
            LOGDEBUG("EmulatorManager::StopEmulator - Emulator with ID '%s' not running", emulatorId.c_str());
            return false;
        }
    }

    LOGDEBUG("EmulatorManager::StopEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

bool EmulatorManager::PauseEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (it->second->IsRunning() && !it->second->IsPaused())
        {
            it->second->Pause();
            LOGINFO("EmulatorManager::PauseEmulator - Paused emulator with ID '%s'", emulatorId.c_str());
            return true;
        }
        else
        {
            LOGDEBUG("EmulatorManager::PauseEmulator - Emulator with ID '%s' not running or already paused", emulatorId.c_str());
            return false;
        }
    }

    LOGDEBUG("EmulatorManager::PauseEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

bool EmulatorManager::ResumeEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (it->second->IsPaused())
        {
            it->second->Resume();
            LOGINFO("EmulatorManager::ResumeEmulator - Resumed emulator with ID '%s'", emulatorId.c_str());
            return true;
        }
        else
        {
            LOGDEBUG("EmulatorManager::ResumeEmulator - Emulator with ID '%s' not paused", emulatorId.c_str());
            return false;
        }
    }

    LOGDEBUG("EmulatorManager::ResumeEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

bool EmulatorManager::ResetEmulator(const std::string& emulatorId)
{
    // Block state changes during shutdown
    if (_isShuttingDown.load())
    {
        LOGWARNING("EmulatorManager::ResetEmulator - Blocked during shutdown");
        return false;
    }

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        it->second->Reset();
        LOGINFO("EmulatorManager::ResetEmulator - Reset emulator with ID '%s'", emulatorId.c_str());
        return true;
    }

    LOGDEBUG("EmulatorManager::ResetEmulator - No emulator found with ID '%s'", emulatorId.c_str());
    return false;
}

std::map<std::string, EmulatorStateEnum> EmulatorManager::GetAllEmulatorStatuses()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    std::map<std::string, EmulatorStateEnum> statuses;
    
    for (const auto& pair : _emulators)
    {
        statuses[pair.first] = pair.second->GetState();
    }
    
    return statuses;
}

std::map<std::string, std::string> EmulatorManager::GetAllEmulatorInfo()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    std::map<std::string, std::string> infoMap;
    
    for (const auto& [uuid, emulator] : _emulators)
    {
        infoMap[uuid] = emulator->GetInstanceInfo();
    }
    
    return infoMap;
}

std::vector<std::shared_ptr<Emulator>> EmulatorManager::FindEmulatorsBySymbolicId(const std::string& symbolicId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    std::vector<std::shared_ptr<Emulator>> matchingEmulators;
    
    for (const auto& [uuid, emulator] : _emulators)
    {
        if (emulator->GetSymbolicId() == symbolicId)
        {
            matchingEmulators.push_back(emulator);
        }
    }
    
    return matchingEmulators;
}

std::shared_ptr<Emulator> EmulatorManager::GetOldestEmulator()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    
    if (_emulators.empty())
    {
        return nullptr;
    }
    
    auto oldest = std::min_element(_emulators.begin(), _emulators.end(),
        [](const auto& a, const auto& b) {
            return a.second->GetCreationTime() < b.second->GetCreationTime();
        });
    
    return oldest->second;
}

std::shared_ptr<Emulator> EmulatorManager::GetMostRecentEmulator()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    
    if (_emulators.empty())
    {
        return nullptr;
    }
    
    std::shared_ptr<Emulator> recent;
    for (const auto& pair : _emulators)
    {
        if (pair.second->IsHiddenGroupMember())
            continue;
        if (!recent || recent->GetLastActivityTime() < pair.second->GetLastActivityTime())
            recent = pair.second;
    }

    return recent;
}

std::string EmulatorManager::GetSelectedEmulatorId()
{
    std::lock_guard<std::mutex> lock(_selectionMutex);
    return _selectedEmulatorId;
}

bool EmulatorManager::SetSelectedEmulatorId(const std::string& emulatorId)
{
    // Allow clearing selection (empty string)
    if (emulatorId.empty())
    {
        {
            std::lock_guard<std::mutex> lock(_selectionMutex);
            std::string previousId = _selectedEmulatorId;
            _selectedEmulatorId = "";

            // Send notification about selection change
            if (!previousId.empty())
            {
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, "");
                messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);
            }
        }

        // Dropping the selection must also drop any realtime request it granted
        UpdateRealtimeScheduling();

        return true;
    }
    
    // Verify emulator exists (allow selecting stopped emulators for explicit selection)
    {
        std::lock_guard<std::mutex> lock(_emulatorsMutex);
        if (_emulators.find(emulatorId) == _emulators.end())
        {
            LOGDEBUG("EmulatorManager::SetSelectedEmulatorId - Emulator '%s' does not exist", emulatorId.c_str());
            return false;
        }
    }
    
    // Update selection
    {
        std::lock_guard<std::mutex> lock(_selectionMutex);
        std::string previousId = _selectedEmulatorId;
        _selectedEmulatorId = emulatorId;

        // Send notification about selection change
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, emulatorId);
        messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

        LOGINFO("EmulatorManager::SetSelectedEmulatorId - Selected emulator: '%s'", emulatorId.c_str());
    }

    // Move the realtime request to the newly selected instance
    UpdateRealtimeScheduling();

    return true;
}

void EmulatorManager::ShutdownAllEmulators()
{
    std::vector<std::string> ids;
    std::vector<std::shared_ptr<Emulator>> retiring;
    {
        std::lock_guard<std::mutex> lock(_emulatorsMutex);
        ids.reserve(_emulators.size());
        retiring.reserve(_emulators.size());
        for (const auto& [uuid, emulator] : _emulators)
        {
            ids.push_back(uuid);
            retiring.push_back(emulator);
        }
    }

    // Context leases: refuse new ones, wait for the live ones after the drain
    // (see RemoveEmulatorInstance())
    for (const auto& emulator : retiring)
        emulator->BeginRetirement();

    if (ids.empty())
    {
        LOGINFO("EmulatorManager::ShutdownAllEmulators - No emulators to shut down");
        return;
    }

    // Detach observers first - the same contract RemoveEmulator() upholds:
    // destroy notices (and the messages queued ahead of them) must be
    // dispatched while the contexts are still alive. The worker keeps running
    // until process exit, so without this step the exit path frees contexts
    // under observers that still hold raw pointers to them. Waiting happens
    // with no EmulatorManager lock held.
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        for (const auto& id : ids)
        {
            messageCenter.Post(NC_EMULATOR_INSTANCE_DESTROYED, new SimpleTextPayload(id), true);
        }
        if (!messageCenter.WaitForOutstandingMessages())
        {
            LOGWARNING("EmulatorManager::ShutdownAllEmulators - Message queue drain timed out; observers may race the instances free");
        }
    }
    for (const auto& emulator : retiring)
        emulator->WaitForContextLeases();
    retiring.clear();

    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    for (auto& [uuid, emulator] : _emulators)
    {
        // Always go through Stop() - see RemoveEmulatorInstance() for why
        // gating this on IsRunning() is unsafe
        LOGINFO("EmulatorManager::ShutdownAllEmulators - Stopping emulator with UUID: %s", uuid.c_str());
        emulator->Stop();

        // Release resources
        emulator->Release();
    }

    _emulators.clear();
    LOGINFO("EmulatorManager::ShutdownAllEmulators - All emulators have been shut down");
}

void EmulatorManager::PrepareForShutdown()
{
    LOGINFO("EmulatorManager::PrepareForShutdown - Setting shutdown flag to block automation requests");
    _isShuttingDown.store(true);
}

EmulatorManager::~EmulatorManager()
{
    // Ensure all emulators are properly shut down before destruction
    ShutdownAllEmulators();
    
    // Clear the emulators map
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    _emulators.clear();
}
