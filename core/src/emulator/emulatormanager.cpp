#include "emulatormanager.h"

#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "stdafx.h"

#include <algorithm>
#include <sstream>
#include <iomanip>

// Initialize static members
EmulatorManager* EmulatorManager::_instance = nullptr;
std::mutex EmulatorManager::_instanceMutex;

EmulatorManager* EmulatorManager::GetInstance()
{
    std::lock_guard<std::mutex> lock(_instanceMutex);
    if (_instance == nullptr)
    {
        _instance = new EmulatorManager();
    }
    return _instance;
}

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
        return emulator;
    }
    
    LOGERROR("EmulatorManager::CreateEmulatorWithId - Failed to initialize emulator with UUID: %s", emulatorId.c_str());
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
        return true;
    }

    LOGDEBUG("EmulatorManager::RemoveEmulator - No emulator found with ID '%s'", emulatorId.c_str());
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
    ShutdownAllEmulators();
}
