#include "emulatormanager.h"

#include "common/modulelogger.h"
#include "stdafx.h"
#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/message-center/eventqueue.h"

#include <algorithm>
#include <iomanip>

// Implementation of EmulatorManager methods

std::shared_ptr<Emulator> EmulatorManager::CreateEmulator(const std::string& symbolicId, LoggerLevel level)
{
    // Create a new emulator with an auto-generated UUID
    auto emulator = std::make_shared<Emulator>(symbolicId, level);

    // Initialize the emulator
    if (emulator->Init())
    {
        std::lock_guard<std::mutex> lock(_emulatorsMutex);
        std::string uuid = emulator->GetUUID();
        _emulators[uuid] = emulator;
        emulator->SetState(StateInitialized);

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

    // Iterate through the map to find the emulator at the specified index
    auto it = _emulators.begin();
    std::advance(it, index);
    return it->second;
}

std::vector<std::string> EmulatorManager::GetEmulatorIds()
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    std::vector<std::string> ids;
    for (const auto& pair : _emulators)
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
    std::lock_guard<std::mutex> lock(_emulatorsMutex);

    auto it = _emulators.find(emulatorId);
    if (it != _emulators.end())
    {
        if (!it->second->IsRunning())
        {
            it->second->StartAsync();
            LOGINFO("EmulatorManager::StartEmulatorAsync - Started emulator async with ID '%s'", emulatorId.c_str());
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

EmulatorManager::~EmulatorManager()
{
    // Ensure all emulators are properly shut down before destruction
    ShutdownAllEmulators();
    
    // Clear the emulators map
    std::lock_guard<std::mutex> lock(_emulatorsMutex);
    _emulators.clear();
}
