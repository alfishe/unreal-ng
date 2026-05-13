#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>

#include "emulator.h"

/// @class EmulatorManager
/// @brief Manages multiple emulator instances
///
/// This singleton class is responsible for creating, tracking, and managing
/// multiple emulator instances. It ensures proper resource management and
/// provides a centralized interface for controlling emulators.
class EmulatorManager
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_GENERIC;

    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

private:
    std::map<std::string, std::shared_ptr<Emulator>> _emulators;
    std::mutex _emulatorsMutex;
    
    // Global selection state (shared across CLI, WebAPI, UI)
    std::string _selectedEmulatorId;
    std::mutex _selectionMutex;

    // Shutdown flag - blocks state changes during application exit
    std::atomic<bool> _isShuttingDown{false};

    // Private constructor for a singleton pattern
    EmulatorManager() = default;

    // Prevent copying
    EmulatorManager(const EmulatorManager&) = delete;
    EmulatorManager& operator=(const EmulatorManager&) = delete;

    // Prevent move operations (optional, but recommended for thread-safe classes)
    EmulatorManager(EmulatorManager&&) = delete;
    EmulatorManager& operator=(EmulatorManager&&) = delete;

public:
    /// @brief Get the singleton instance of EmulatorManager
    /// @return Pointer to the EmulatorManager instance
    static EmulatorManager* GetInstance()
    {
        static EmulatorManager instance;

        return &instance;
    }

/// @brief Create a new emulator instance with a unique ID
/// @param symbolicId Optional symbolic identifier for the emulator (if empty, a default will be used)
/// @param level Logger level for the emulator (note: automation-created instances automatically disable all modular logging)
/// @return Shared pointer to the created emulator, or nullptr if creation failed
    std::shared_ptr<Emulator> CreateEmulator(const std::string& symbolicId = "", LoggerLevel level = LoggerLevel::LogWarning);

    /// @brief Create a new emulator instance with a specific UUID and symbolic ID
    /// @param emulatorId The UUID to use for the emulator
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param level Logger level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @return Shared pointer to the created emulator, or nullptr if creation failed
    std::shared_ptr<Emulator> CreateEmulatorWithId(const std::string& emulatorId, const std::string& symbolicId = "", LoggerLevel level = LoggerLevel::LogWarning);

    /// @brief Create a new emulator instance with a specific model configuration
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param modelName Name of the model to create (e.g., "PENTAGON", "48K", "128K")
    /// @param level Logging level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @return Shared pointer to the created emulator, or nullptr on failure
    std::shared_ptr<Emulator> CreateEmulatorWithModel(const std::string& symbolicId, const std::string& modelName, LoggerLevel level = LoggerLevel::LogWarning);

    /// @brief Create a new emulator instance with a specific model and custom RAM size
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param modelName Name of the model to create
    /// @param ramSize RAM size in KB (must be supported by the model)
    /// @param level Logging level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @return Shared pointer to the created emulator, or nullptr on failure
    std::shared_ptr<Emulator> CreateEmulatorWithModelAndRAM(const std::string& symbolicId, const std::string& modelName, uint32_t ramSize, LoggerLevel level = LoggerLevel::LogWarning);

    /// @brief Get an existing emulator by ID
    /// @param emulatorId ID of the emulator to retrieve
    /// @return Shared pointer to the emulator, or nullptr if not found
    std::shared_ptr<Emulator> GetEmulator(const std::string& emulatorId);

    /// @brief Get emulator by index (0-based)
    /// @param index Zero-based index of the emulator
    /// @return Shared pointer to the emulator, or nullptr if index is invalid
    std::shared_ptr<Emulator> GetEmulatorByIndex(int index);

    /// @brief Get a list of all available emulator models
    /// @return Vector of model information structures
    std::vector<TMemModel> GetAvailableModels() const;

    /// @brief Get all emulator IDs
    /// @return Vector of emulator IDs
    std::vector<std::string> GetEmulatorIds();

    /// @brief Check if an emulator with the given ID exists
    /// @param emulatorId ID to check
    /// @return True if an emulator with the ID exists, false otherwise
    bool HasEmulator(const std::string& emulatorId);

    /// @brief Remove an emulator instance
    /// @param emulatorId ID of the emulator to remove
    /// @return True if the emulator was removed, false if not found
    bool RemoveEmulator(const std::string& emulatorId);
    
    /// @brief Get the globally selected emulator ID (shared across CLI, WebAPI, UI)
    /// @return ID of the currently selected emulator (empty string if none selected)
    std::string GetSelectedEmulatorId();
    
    ///  @brief Set the globally selected emulator ID (shared across CLI, WebAPI, UI)
    /// @param emulatorId ID of the emulator to select (empty string to clear selection)
    /// @return True if selection was updated, false if emulator doesn't exist (unless clearing)
    bool SetSelectedEmulatorId(const std::string& emulatorId);

    // Lifecycle control methods - UI should use these instead of direct Emulator calls
    /// @brief Start an emulator
    /// @param emulatorId ID of the emulator to start
    /// @return True if started successfully, false if emulator not found or already running
    bool StartEmulator(const std::string& emulatorId);

    /// @brief Start an emulator asynchronously
    /// @param emulatorId ID of the emulator to start
    /// @return True if started successfully, false if emulator not found or already running
    bool StartEmulatorAsync(const std::string& emulatorId);

    /// @brief Stop an emulator
    /// @param emulatorId ID of the emulator to stop
    /// @return True if stopped successfully, false if emulator not found
    bool StopEmulator(const std::string& emulatorId);

    /// @brief Pause an emulator
    /// @param emulatorId ID of the emulator to pause
    /// @return True if paused successfully, false if emulator not found or not running
    bool PauseEmulator(const std::string& emulatorId);

    /// @brief Resume an emulator
    /// @param emulatorId ID of the emulator to resume
    /// @return True if resumed successfully, false if emulator not found or not paused
    bool ResumeEmulator(const std::string& emulatorId);

    /// @brief Reset an emulator
    /// @param emulatorId ID of the emulator to reset
    /// @return True if reset successfully, false if emulator not found
    bool ResetEmulator(const std::string& emulatorId);

    /// @brief Get status of all emulators
    /// @return Map of emulator UUIDs to their states
    std::map<std::string, EmulatorStateEnum> GetAllEmulatorStatuses();

    /// @brief Get detailed information about all emulators
    /// @return Map of emulator UUIDs to their information strings
    std::map<std::string, std::string> GetAllEmulatorInfo();

    /// @brief Find all emulators with a specific symbolic ID
    /// @param symbolicId The symbolic ID to search for
    /// @return Vector of shared pointers to emulators matching the symbolic ID
    std::vector<std::shared_ptr<Emulator>> FindEmulatorsBySymbolicId(const std::string& symbolicId);

    /// @brief Get the oldest active emulator
    /// @return Shared pointer to the oldest emulator, or nullptr if no emulators exist
    std::shared_ptr<Emulator> GetOldestEmulator();

    /// @brief Get the most recently active emulator
    /// @return Shared pointer to the most recently active emulator, or nullptr if no emulators exist
    std::shared_ptr<Emulator> GetMostRecentEmulator();

    /// @brief Shutdown all emulators
    void ShutdownAllEmulators();

    /// @brief Prepare for application shutdown - blocks all state change operations
    /// @details Should be called as the FIRST step in application shutdown sequence,
    ///          before unbinding emulators or cleaning up resources.
    void PrepareForShutdown();

    /// @brief Check if manager is in shutdown mode
    /// @return True if PrepareForShutdown() has been called
    bool IsShuttingDown() const { return _isShuttingDown.load(); }

    /// @brief Destructor - cleans up all emulator instances
    ~EmulatorManager();
};

/// @brief Usage examples for EmulatorManager
///
/// Example 1: Creating and managing multiple emulator instances with UUIDs
///
/// ```cpp
/// // Get the EmulatorManager instance
/// EmulatorManager* manager = EmulatorManager::GetInstance();
///
/// // Create a new emulator with a symbolic name
/// auto emulator1 = manager->CreateEmulator("Main Emulator");
/// if (emulator1) {
///     std::cout << "Created emulator with UUID: " << emulator1->GetUUID() << std::endl;
///     std::cout << "Symbolic ID: " << emulator1->GetSymbolicId() << std::endl;
///     std::cout << "Created at: "
///               << std::chrono::system_clock::to_time_t(emulator1->GetCreationTime())
///               << std::endl;
/// }
///
/// // Create another emulator with a specific UUID and symbolic ID
/// auto emulator2 = manager->CreateEmulatorWithId("550e8400-e29b-41d4-a716-446655440000", "Secondary Emulator");
/// if (emulator2) {
///     std::cout << "Created emulator with UUID: " << emulator2->GetUUID() << std::endl;
///     std::cout << "Symbolic ID: " << emulator2->GetSymbolicId() << std::endl;
/// }
///
/// // Get detailed information about all emulators
/// auto allInfo = manager->GetAllEmulatorInfo();
/// for (const auto& [uuid, info] : allInfo) {
///     std::cout << "\nEmulator " << uuid << ":\n" << info << std::endl;
/// }
///
/// // Find emulators by symbolic ID
/// auto mainEmulators = manager->FindEmulatorsBySymbolicId("Main Emulator");
/// for (const auto& uuid : mainEmulators) {
///     std::cout << "Found main emulator with UUID: " << uuid << std::endl;
/// }
///
/// // Get the most recently used emulator
/// auto recent = manager->GetMostRecentEmulator();
/// if (recent) {
///     std::cout << "Most recently used emulator: " << recent->GetUUID() << std::endl;
/// }
///
/// // Get the oldest emulator
/// auto oldest = manager->GetOldestEmulator();
/// if (oldest) {
///     std::cout << "Oldest emulator: " << oldest->GetUUID() << std::endl;
/// }
///

/// Example 2: Basic usage with multiple emulator instances
///
/// ```cpp
/// // Get the EmulatorManager instance
/// EmulatorManager* manager = EmulatorManager::GetInstance();
///
/// // Create two emulator instances with different IDs
/// auto emu1 = manager->CreateEmulator("ZX48K_1", LoggerLevel::LogInfo);
/// auto emu2 = manager->CreateEmulator("ZX128K_1", LoggerLevel::LogInfo);
///
/// // Start the first emulator
/// emu1->Start();
///
/// // Get all emulator IDs
/// auto ids = manager->GetEmulatorIds();
/// for (const auto& id : ids) {
///     std::cout << "Emulator ID: " << id << std::endl;
/// }
///
/// // Get status of all emulators
/// auto statuses = manager->GetAllEmulatorStatuses();
/// for (const auto& pair : statuses) {
///     std::cout << "Emulator " << pair.first << " status: "
///               << getEmulatorStateName(pair.second) << std::endl;
/// }
///
/// // Pause the first emulator
/// emu1->Pause();
///
/// // Start the second emulator
/// emu2->Start();
///
/// // Stop all emulators when done
/// manager->ShutdownAllEmulators();
/// ```
///
/// Example 2: Removing a specific emulator
///
/// ```cpp
/// // Get the EmulatorManager instance
/// EmulatorManager* manager = EmulatorManager::GetInstance();
///
/// // Create an emulator
/// auto emu = manager->CreateEmulator("TestEmulator");
///
/// // Use the emulator
/// emu->Start();
///
/// // When done, remove just this emulator
/// manager->RemoveEmulator("TestEmulator");
///

