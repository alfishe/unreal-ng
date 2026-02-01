#include "emulatormanager.h"

#include "common/modulelogger.h"
#include "stdafx.h"
#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/message-center/eventqueue.h"
#include "emulator/notifications.h"

#include <algorithm>
#include <iomanip>

// Implementation of EmulatorManager methods

std::shared_ptr<Emulator> EmulatorManager::CreateEmulator(const std::string& symbolicId, LoggerLevel level)
{
    // Block new emulator creation during shutdown
    if (_isShuttingDown.load())
    {
        LOGWARNING("EmulatorManager::CreateEmulator - Blocked during shutdown");
        return nullptr;
    }

    // Create a new emulator with an auto-generated UUID
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

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

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithId - Failed to initialize emulator with UUID: %s", emulatorId.c_str());
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::CreateEmulatorWithModel(const std::string& symbolicId, const std::string& modelName, LoggerLevel level)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create a new emulator instance
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Find the model configuration
    Config tempConfig(emulator->GetContext());
    const TMemModel* modelInfo = tempConfig.FindModelByShortName(modelName);
    if (!modelInfo)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModel - Unknown model: '%s'", modelName.c_str());
        return nullptr;
    }

    // Configure the emulator for this model before initialization
    CONFIG& config = emulator->GetContext()->config;
    config.mem_model = modelInfo->Model;
    config.ramsize = modelInfo->defaultRAM;

    // Initialize the emulator
    if (emulator->Init())
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

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithModel - Failed to initialize emulator with model: '%s'", modelName.c_str());
    return nullptr;
}

std::shared_ptr<Emulator> EmulatorManager::CreateEmulatorWithModelAndRAM(const std::string& symbolicId, const std::string& modelName, uint32_t ramSize, LoggerLevel level)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    // Create a new emulator instance
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Find the model configuration
    Config tempConfig(emulator->GetContext());
    const TMemModel* modelInfo = tempConfig.FindModelByShortName(modelName);
    if (!modelInfo)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - Unknown model: '%s'", modelName.c_str());
        return nullptr;
    }

    // Validate RAM size for this model
    if ((ramSize & modelInfo->AvailRAMs) == 0)
    {
        LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - RAM size %dKB not supported by model '%s'",
                ramSize, modelName.c_str());
        return nullptr;
    }

    // Configure the emulator for this model and RAM size before initialization
    CONFIG& config = emulator->GetContext()->config;
    config.mem_model = modelInfo->Model;
    config.ramsize = ramSize;

    // Initialize the emulator
    if (emulator->Init())
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

        return emulator;
    }

    LOGERROR("EmulatorManager::CreateEmulatorWithModelAndRAM - Failed to initialize emulator with model: '%s', RAM: %dKB",
            modelName.c_str(), ramSize);
    return nullptr;
}

std::vector<TMemModel> EmulatorManager::GetAvailableModels() const
{
    // Create a temporary config to access the model list
    // This is a bit of a hack, but since the models are static data,
    // we can access them through any config instance
    EmulatorContext tempContext;
    Config tempConfig(&tempContext);
    return tempConfig.GetAvailableModels();
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

    if (index < 0 || index >= static_cast<int>(_emulators.size()))
    {
        LOGDEBUG("EmulatorManager::GetEmulatorByIndex - Invalid index %d (valid range: 0-%d)",
                index, static_cast<int>(_emulators.size()) - 1);
        return nullptr;
    }

    // Create sorted list by creation time
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> emulatorTimestamps;
    for (const auto& pair : _emulators)
    {
        emulatorTimestamps.push_back({pair.first, pair.second->GetCreationTime()});
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
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        // Stop the emulator if it's running
        if (it->second->IsRunning())
        {
            LOGINFO("EmulatorManager::RemoveEmulator - Stopping running emulator with ID '%s'", emulatorId.c_str());
            it->second->Stop();
        }

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
        
        // Emit notification that instance was destroyed
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleTextPayload* payload = new SimpleTextPayload(emulatorId);
        messageCenter.Post(NC_EMULATOR_INSTANCE_DESTROYED, payload);

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
    
    auto recent = std::max_element(_emulators.begin(), _emulators.end(),
        [](const auto& a, const auto& b) {
            return a.second->GetLastActivityTime() < b.second->GetLastActivityTime();
        });
    
    return recent->second;
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
    
    return true;
}

void EmulatorManager::ShutdownAllEmulators()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    
    for (auto& [uuid, emulator] : _emulators)
    {
        // Stop the emulator if it's running
        if (emulator->IsRunning())
        {
            LOGINFO("EmulatorManager::ShutdownAllEmulators - Stopping emulator with UUID: %s", uuid.c_str());
            emulator->Stop();
        }
        
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
