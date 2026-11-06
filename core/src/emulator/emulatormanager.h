#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <thread>

#include "emulator.h"

/// @struct MachineIdentity
/// @brief Machine identity snapshot shared by every automation surface
///
/// Parity rule: WebAPI, MCP and CLI are equally important consumers and must
/// report identical machine information. This struct is computed in ONE place
/// (EmulatorManager::GetMachineIdentity) so the surfaces can never drift;
/// each module only decides how to serialize it (JSON keys / text lines).
struct MachineIdentity
{
    std::string Model;           ///< Short name from the model table ("48K")
    std::string ModelFullName;   ///< Human-readable name ("ZX-Spectrum 48k")
    uint32_t RamKb = 0;          ///< Resolved RAM size in KB
    std::string VideoMode;       ///< Live video mode name (empty until the screen subsystem exists)
    bool HasVideoMode = false;   ///< Whether VideoMode carries a value
    double SpeedMultiplier = 1.0; ///< Live z80 frequency multiplier (turbo)
    std::string ConfigFolder;    ///< configs/<folder> backing this machine
    std::string RamPowerOn;      ///< "random" / "zero": RAM contents at creation (Config::RamPowerOnName)
    std::string Variant;         ///< machine variant name ("TSL-VDAC2", MachineVariants), empty for a plain model
    std::string VariantTitle;    ///< its title ("TS-Conf + VDAC2 (FT812)")
    bool Valid = false;          ///< False while the emulator context does not exist yet

    /// ZX-Poly: this instance belongs to a four-module group
    /// (docs/inprogress/2026-09-27-zxpoly). Module 0 is the machine every
    /// surface shows; 1-3 are hidden members
    bool ZXPoly = false;
    int ZXPolyModule = -1;       ///< 0 = master, 1..3 = slave CPU module
    std::string ZXPolyMasterId;  ///< the group's visible machine
    bool ZXPolyLocked = false;   ///< #3D00 locked (a ZX-Poly edition runs)
    int ZXPolyVideoMode = 0;     ///< #3D00 D2-D4
};

class ZXPolyGroup;

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

    // ZX-Poly groups by master ID (the groups own their four instances)
    std::map<std::string, std::shared_ptr<ZXPolyGroup>> _zxpolyGroups;  // shared_ptr: complete type not needed here
    std::recursive_mutex _zxpolyMutex;
    bool RemoveEmulatorInstance(const std::string& emulatorId);
    
    // Global selection state (shared across CLI, WebAPI, UI)
    std::string _selectedEmulatorId;
    std::mutex _selectionMutex;

    // Shutdown flag - blocks state changes during application exit
    std::atomic<bool> _isShuttingDown{false};

    /// region <Retired instances (BUGS.md #11, docs/inprogress/2026-10-10-gui-instance-lifetime)>
    /// A removal retires the instance; its last holder frees it. Tracked here so one that is never freed is seen
    struct RetiredInstance
    {
        std::weak_ptr<Emulator> emulator;
        std::string id;
        std::chrono::steady_clock::time_point retiredAt;
        std::chrono::steady_clock::time_point lastWarning;
        bool warned = false;
    };
    std::vector<RetiredInstance> _retired;
    /// Instances whose emulation thread did not stop (StopFor timed out): never freed, the thread may still use them
    std::vector<std::shared_ptr<Emulator>> _stuck;
    std::mutex _retiredMutex;
    std::condition_variable _retiredCv;
    std::thread _retiredWatchdog;
    bool _retiredWatchdogStop = false;
    bool _retiredWatchdogClosed = false;   // the destructor ran: no restart
    std::chrono::milliseconds _retiredWarnAfter = kRetiredAliveWarnAfter;
    std::chrono::milliseconds _retiredWarnEvery = kRetiredAliveWarnEvery;
    std::atomic<uint64_t> _retiredWarnings{0};
    std::atomic<int64_t> _leaseWaitTimeoutMs{kLeaseWaitTimeout.count()};
    std::atomic<int64_t> _stopJoinTimeoutMs{kStopJoinTimeout.count()};
    std::atomic<int64_t> _shutdownFreeTimeoutMs{kShutdownFreeTimeout.count()};

    void TrackRetired(const std::shared_ptr<Emulator>& emulator);
    void WatchRetired();
    void StopRetiredWatchdog(bool final = false);
    /// Stop (bounded), retire, track: the end of every removal
    void RetireRemoved(const std::shared_ptr<Emulator>& emulator, const char* caller);
    /// endregion </Retired instances>

    /// Recompute per-instance real-time scheduling requests from the current
    /// selection: exactly one instance - the selected one, or the sole
    /// instance when nothing is selected - keeps its emulation thread
    /// real-time; every other thread drops back to normal scheduling
    /// (user-facing rule: only the ACTIVE emulator may hold real-time
    /// priority). Called after every selection / instance-map mutation
    void UpdateRealtimeScheduling();

    /// Same, for callers that already hold _emulatorsMutex
    void UpdateRealtimeSchedulingLocked();

    // Private constructor for a singleton pattern
    EmulatorManager() = default;

    // Prevent copying
    EmulatorManager(const EmulatorManager&) = delete;
    EmulatorManager& operator=(const EmulatorManager&) = delete;

    // Prevent move operations (optional, but recommended for thread-safe classes)
    EmulatorManager(EmulatorManager&&) = delete;
    EmulatorManager& operator=(EmulatorManager&&) = delete;

public:
    /// Every wait in a removal is bounded (owner decision 2026-10-10). A timeout is safe: the removal retires,
    /// it frees nothing; the warning it logs is a bug report
    static constexpr std::chrono::milliseconds kLeaseWaitTimeout{2000};       ///< live context leases
    static constexpr std::chrono::milliseconds kStopJoinTimeout{5000};        ///< the emulation thread's exit
    static constexpr std::chrono::milliseconds kRetiredAliveWarnAfter{10000}; ///< a retired instance still held
    static constexpr std::chrono::milliseconds kRetiredAliveWarnEvery{60000}; ///< and again while it stays
    static constexpr std::chrono::milliseconds kShutdownFreeTimeout{3000};    ///< exit waits for the last holders

    /// Retired instances some holder still keeps alive (stuck ones included)
    size_t RetiredAliveCount();
    /// Warnings the retired-instance watchdog has logged (a holder that does not let go)
    uint64_t RetiredWarningCount() const { return _retiredWarnings.load(); }
    /// Tests: shorten the retired-instance warnings
    void SetRetiredWarningTimes(std::chrono::milliseconds after, std::chrono::milliseconds every);
    /// Tests: shorten the removal's bounded waits (the k* constants are the defaults)
    void SetRemovalTimeouts(std::chrono::milliseconds leaseWait, std::chrono::milliseconds stopJoin,
                            std::chrono::milliseconds shutdownFree);

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
    /// @param configOverride Optional; edits the loaded config before any subsystem initializes
    ///        (e.g. Config::RamPowerOnOverride) - the same parameter on every create path
    std::shared_ptr<Emulator> CreateEmulator(const std::string& symbolicId = "", LoggerLevel level = LoggerLevel::LogWarning,
                                             std::function<void(CONFIG&)> configOverride = {});

    /// @brief Create a new emulator instance with a specific UUID and symbolic ID
    /// @param emulatorId The UUID to use for the emulator
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param level Logger level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @return Shared pointer to the created emulator, or nullptr if creation failed
    std::shared_ptr<Emulator> CreateEmulatorWithId(const std::string& emulatorId, const std::string& symbolicId = "", LoggerLevel level = LoggerLevel::LogWarning);

    /// @brief Create a new emulator instance with a specific model configuration
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param modelName Name of the model to create (e.g., "PENTAGON", "48K", "128K"), or a ZX-Poly
    ///        configuration (ZXPolyGroup::Configurations: "ZXPOLY-128K"...) - that creates the whole
    ///        group and returns its master (CreateZXPolyMachine without media)
    /// @param level Logging level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @param outError Optional; when non-null it receives a human-readable failure reason
    ///        (unknown model, model not supported by this build, init failure) on nullptr return
    /// @return Shared pointer to the created emulator, or nullptr on failure
    /// @param configOverride Optional; edits the loaded config before any subsystem initializes.
    ///        A ZX-Poly configuration applies it to all four modules
    std::shared_ptr<Emulator> CreateEmulatorWithModel(const std::string& symbolicId, const std::string& modelName, LoggerLevel level = LoggerLevel::LogWarning, std::string* outError = nullptr, std::function<void(CONFIG&)> configOverride = {});

    /// @brief Create a new emulator instance with a specific model and custom RAM size
    /// @param symbolicId Optional symbolic identifier for the emulator
    /// @param modelName Name of the model to create
    /// @param ramSize RAM size in KB (must be supported by the model)
    /// @param level Logging level for the emulator (note: automation-created instances automatically disable all modular logging)
    /// @param outError Optional; when non-null it receives a human-readable failure reason
    ///        (unknown model, RAM size not supported, model not supported by this build, init failure) on nullptr return
    /// @return Shared pointer to the created emulator, or nullptr on failure
    std::shared_ptr<Emulator> CreateEmulatorWithModelAndRAM(const std::string& symbolicId, const std::string& modelName, uint32_t ramSize, LoggerLevel level = LoggerLevel::LogWarning, std::string* outError = nullptr, std::function<void(CONFIG&)> configOverride = {});

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

    /// @brief Snapshot the machine identity of an emulator instance
    /// @details Single source for the identity information reported by WebAPI,
    ///          MCP and CLI (parity rule). Works on partially initialized
    ///          instances: Valid=false when the context does not exist yet.
    /// @param emulator Emulator instance to describe
    /// @return Filled MachineIdentity struct
    static MachineIdentity GetMachineIdentity(Emulator& emulator);

    /// Create a ZX-Poly machine: four instances of one model, synchronized
    /// (ZXPolyGroup). modelName: a configuration name (ZXPOLY-48K,
    /// ZXPOLY-128K, ZXPOLY-PENTAGON) or any creatable model. mediaPath: a .zxp snapshot, a .prom ROM image or a
    /// multiloader disk (empty: the bare machine). Returns the master - the
    /// instance every surface addresses; the slaves are hidden members.
    /// The machine is created, not started. Removing the master removes the
    /// whole group
    /// configOverride applies to all four modules (see CreateEmulatorWithModel)
    std::shared_ptr<Emulator> CreateZXPolyMachine(const std::string& symbolicId, const std::string& modelName,
                                                  const std::string& mediaPath, std::string* outError = nullptr,
                                                  std::function<void(CONFIG&)> configOverride = {});

    /// The ZX-Poly group whose member (master or slave) has this ID; nullptr if none
    ZXPolyGroup* GetZXPolyGroup(const std::string& emulatorId);

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
    bool PauseEmulator(const std::string& emulatorId, execution::Origin origin = execution::Origin::Internal);
    bool PauseEmulator(const std::string& emulatorId, const execution::Requester& requester);
    /// The pause with its result (Applied; Pending: queued behind a command, named in detail; Busy: `wait` ran out;
    /// Refused): the surfaces answer with it. `wait` > 0: wait behind a command too, that long (Emulator::PauseAndWait)
    execution::Result PauseEmulatorWithResult(const std::string& emulatorId, const execution::Requester& requester,
                                              std::chrono::milliseconds wait = std::chrono::milliseconds(0));

    /// @brief Resume an emulator
    /// @param emulatorId ID of the emulator to resume
    /// @return True if resumed successfully, false if emulator not found or not paused
    bool ResumeEmulator(const std::string& emulatorId, execution::Origin origin = execution::Origin::Internal);
    /// A run asked by `requester` (an automation session: its surface and id): refused while another surface holds
    /// the run-control claim
    bool ResumeEmulator(const std::string& emulatorId, const execution::Requester& requester);

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

