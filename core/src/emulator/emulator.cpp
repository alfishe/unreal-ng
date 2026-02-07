#include "emulator.h"

#include <loaders/disk/loader_scl.h>
#include <loaders/disk/loader_trd.h>
#include <loaders/snapshot/loader_z80.h>

#include <chrono>
#include <functional>
#include <iomanip>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "common/systemhelper.h"
#include "common/threadhelper.h"
#include "common/timehelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/io/fdc/wd1793.h"
#include "loaders/snapshot/loader_sna.h"

/// region <Constructors / Destructors>

Emulator::Emulator(LoggerLevel level) : Emulator("", level) {}

Emulator::Emulator(const std::string& symbolicId, LoggerLevel level)
{
    _uuid = UUID::Generate(); // Generate new unique UUID
    _emulatorId = _uuid.toString();
    _symbolicId = symbolicId;
    _createdAt = std::chrono::system_clock::now();
    _lastActivity = _createdAt;
    _loggerLevel = level;
    _state = StateInitialized;

    // Create and initialize emulator context. ModuleLogger will be initialized as well.
    _context = new EmulatorContext(_loggerLevel);
    if (_context != nullptr)
    {
        _logger = _context->pModuleLogger;
        _context->pEmulator = this;

        // Create FeatureManager and assign to context
        _featureManager = new FeatureManager(_context);
        _context->pFeatureManager = _featureManager;

        MLOGDEBUG("Emulator::Emulator(symbolicId='%s', level=%d) - Instance created with UUID: %s", symbolicId.c_str(),
                  level, _emulatorId.c_str());
        MLOGDEBUG("Emulator::Init - context created");
    }
    else
    {
        LOGERROR("Emulator::Emulator(id=%s) - context creation failed", symbolicId.c_str());
        throw std::runtime_error("Emulator::Emulator() - context creation failed");
    }
}

Emulator::~Emulator()
{
    MLOGDEBUG("Emulator::~Emulator()");

    // Ensure resources are released if Release() wasn't called explicitly
    if (_initialized.load(std::memory_order_acquire))
    {
        Release();
    }

    if (_featureManager)
    {
        delete _featureManager;
        _featureManager = nullptr;
    }
}

/// endregion </Constructors / Destructors>

/// region <Initialization>

bool Emulator::Init()
{
    // Early exit if already initialized
    if (_initialized.load(std::memory_order_acquire))
    {
        LOGERROR("Emulator::Init() - already initialized");
        throw std::logic_error("Emulator::Init() - already initialized");
    }

    bool result = false;

    // Lock mutex until exiting current scope
    std::lock_guard<std::mutex> lock(_mutexInitialization);

    // Double-check after acquiring the lock
    if (_initialized.load(std::memory_order_relaxed))
    {
        LOGERROR("Emulator::Init() - already initialized (race condition detected)");
        throw std::logic_error("Emulator::Init() - already initialized (race condition detected)");
    }

    // Ensure that MessageCenter instance is up and running
    [[maybe_unused]] MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter(true);

    // Get host system info
    GetSystemInfo();

    // Load configuration
    _config = new Config(_context);
    if (_config != nullptr)
    {
        result = _config->LoadConfig();

        if (result)
        {
            MLOGDEBUG("Emulator::Init - Config file successfully loaded");
        }
        else
        {
            MLOGERROR("Emulator::Init - Config load failed");
        }
    }
    else
    {
        MLOGERROR("Emulator::Init - config manager creation failed");
        result = false;
    }

    // Create and initialize CPU system instance (including most peripheral devices)
    if (result)
    {
        result = false;

        _core = new Core(_context);
        if (_core && _core->Init())
        {
            MLOGDEBUG("Emulator::Init - CPU system core created");

            _context->pCore = _core;

            _z80 = _core->GetZ80();
            _memory = _core->GetMemory();

            result = true;
        }
        else
        {
            MLOGERROR("Emulator::Init - CPU system core (or main peripheral devices) creation failed");
        }
    }

    // Load ROMs
    if (result)
    {
        ROM& rom = *_core->GetROM();

        // std::string rompath = rom.GetROMFilename();
        result = rom.LoadROM();

        if (result)
        {
            // Calculate ROM segment signatures
            rom.CalculateSignatures();

            MLOGDEBUG("Emulator::Init - ROM data successfully loaded");
            result = true;
        }
        else
        {
            MLOGERROR("Emulator::Init - ROM load failed");
            result = false;
        }
    }

    // Create and initialize additional peripheral devices
    ;  // Tape
    ;  // HDD/CD
    ;  // ZiFi
    ;  // GS / NGS

    // Create and initialize Debugger and related components
    ;  // Debugger

    // Create and initialize Scripting support
    ;  // Scripting host (Python or Lua?)

    // Create and initialize main emulator loop
    if (result)
    {
        result = false;

        _mainloop = new MainLoop(_context);
        if (_mainloop != nullptr)
        {
            MLOGDEBUG("Emulator::Init - mainloop created");

            result = true;
        }
        else
        {
            MLOGERROR("Emulator::Init - mainloop creation failed");
        }
    }

    // Create and initialized debug manager (including breakpoint, label managers and disassembler)
    if (result)
    {
        result = false;

        DebugManager* manager = new DebugManager(_context);
        if (manager != nullptr)
        {
            MLOGDEBUG("Emulator::Init - debug manager created");

            _debugManager = manager;
            _breakpointManager = manager->GetBreakpointsManager();

            _context->pDebugManager = manager;

            result = true;
        }
    }

    /// region <Sanity checks>

    if (!_context)
    {
        std::string error = "Context was not created";
        throw std::logic_error(error);
    }

    if (!_config)
    {
        std::string error = "Config was not created";
        throw std::logic_error(error);
    }

    if (!_core)
    {
        std::string error = "CPU was not created";
        throw std::logic_error(error);
    }

    if (!_context->pCore)
    {
        std::string error = "_context->pCore not available";
        throw std::logic_error(error);
    }

    if (!_context->pMemory)
    {
        std::string error = "_context->pMemory not available";
        throw std::logic_error(error);
    }

    if (!_context->pScreen)
    {
        std::string error = "_context->pScreen not available";
        throw std::logic_error(error);
    }

    if (!_context->pKeyboard)
    {
        std::string error = "_context->pKeyboard not available";
        throw std::logic_error(error);
    }

    if (!_context->pTape)
    {
        std::string error = "_context->pTape not available";
        throw std::logic_error(error);
    }

    if (!_context->pBetaDisk)
    {
        std::string error = "_context->pBetaDisk not available";
        throw std::logic_error(error);
    }

    if (!_context->pPortDecoder)
    {
        std::string error = "_context->pPortDecoder not available";
        throw std::logic_error(error);
    }

    if (!_context->pSoundManager)
    {
        std::string error = "_context->pSoundManager not available";
        throw std::logic_error(error);
    }

    if (_isDebug && !_context->pDebugManager)
    {
        std::string error = "_context->pDebugManager not available";
        throw std::logic_error(error);
    }

    /// endregion </Sanity checks>

    // Reset CPU and set-up all ports / ROM and RAM pages
    if (result)
    {
        _core->Reset();

        // Init default video render
        _context->pScreen->InitFrame();

        // Propagate initial feature values to all subsystems (SoundManager, Memory, etc.)
        // This ensures cached feature flags match FeatureManager state after initialization
        // If not done - there will be no sound
        if (_featureManager)
        {
            _featureManager->onFeatureChanged();
        }
        
        // Ensure SoundManager feature cache is definitely synced (belt-and-suspenders)
        // This guards against race conditions during async start
        if (_context->pSoundManager)
        {
            _context->pSoundManager->UpdateFeatureCache();
        }

        // Ensure all logger messages displayed
        _context->pModuleLogger->Flush();

        // Mark as initialized at the very last moment
        _initialized = true;
    }

    // Release all created resources if any of initialization steps failed
    if (!result)
    {
        // Important!: use ReleaseNoGuard() only since we're already locked mutex
        ReleaseNoGuard();
    }

    return result;
}

void Emulator::Release()
{
    // Lock mutex until exiting current scope
    std::lock_guard<std::mutex> lock(_mutexInitialization);

    // Guard against double-release (thread safety)
    if (_isReleased)
    {
        MLOGDEBUG("Emulator::Release - Already released, ignoring");
        return;
    }

    _isReleased = true;

    // Mark as destroying to prevent new operations from other threads
    SetState(StateDestroying);

    ReleaseNoGuard();
}

void Emulator::ReleaseNoGuard()
{
    // Guard against null context (shouldn't happen, but be safe)
    if (!_context)
        return;

    // Cleanup any pending step-over operation (orphan cleanup)
    if (_pendingStepOverBpId != 0 && _breakpointManager)
    {
        MLOGDEBUG("Emulator::ReleaseNoGuard - Cleaning up orphaned step-over breakpoint ID %d", _pendingStepOverBpId);
        _breakpointManager->RemoveBreakpointByID(_pendingStepOverBpId);
        
        // Reactivate any deactivated breakpoints
        for (uint16_t bpId : _stepOverDeactivatedBps)
        {
            _breakpointManager->ActivateBreakpoint(bpId);
        }
        
        _pendingStepOverBpId = 0;
        _stepOverDeactivatedBps.clear();
    }

    // Release debug manager (and related components)
    if (_context->pDebugManager)
    {
        delete _context->pDebugManager;
        _context->pDebugManager = nullptr;
    }

    // Stop and release main loop
    if (_mainloop != nullptr)
    {
        _mainloop->Stop();

        delete _mainloop;
        _mainloop = nullptr;
    }

    /// region <Release additional peripheral devices>
    // GS / NGS
    // ZiFi
    // HDD/CD
    // Tape
    // Floppy
    for (size_t i = 0; i < 4; i++)
    {
        FDD* diskDrive = _context->coreState.diskDrives[i];
        DiskImage* diskImage = _context->coreState.diskImages[i];

        if (diskDrive != nullptr)
        {
            diskDrive->ejectDisk();
            delete diskDrive;

            _context->coreState.diskDrives[i] = nullptr;
        }

        if (diskImage)
        {
            delete diskImage;

            _context->coreState.diskImages[i] = nullptr;
        }
    }

    /// endregion </Release additional peripheral devices>

    // Release CPU subsystem core (it will release all main peripherals)
    _context->pCore = nullptr;
    if (_core != nullptr)
    {
        delete _core;
        _core = nullptr;
    }

    // Release Config
    if (_config != nullptr)
    {
        delete _config;
        _config = nullptr;
    }

    // Release EmulatorContext as last step
    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Initialization>

//
// Read CPU ID string and analyze MMX/SSE/SSE2 feature flags
// See: https://en.wikipedia.org/wiki/CPUID
//
void Emulator::GetSystemInfo()
{
    HOST& host = _context->host;

    // Initialize host structure members
    memset(host.cpu_model, 0, sizeof(host.cpu_model));
    host.mmx = 0;
    host.sse = 0;
    host.sse2 = 0;
    host.cpufq = 0;
    host.ticks_frame = 0;

#if defined(__x86__) || defined(__x86_64__)
    char cpuString[49];
    cpuString[0] = '\0';

    SystemHelper::GetCPUString(cpuString);
    LOGINFO("CPU ID: %s", cpuString);

    [[maybe_unused]] unsigned cpuver =
        SystemHelper::GetCPUID(1, 0);                  // Read Highest Function Parameter and ManufacturerID
    unsigned features = SystemHelper::GetCPUID(1, 1);  // Read Processor Info and Feature Bits
    host.mmx = (features >> 23) & 1;
    host.sse = (features >> 25) & 1;
    host.sse2 = (features >> 26) & 1;
    MLOGINFO("MMX:%s, SSE:%s, SSE2:%s", host.mmx ? "YES" : "NO", host.sse ? "YES" : "NO", host.sse2 ? "YES" : "NO");

    host.cpufq = SystemHelper::GetCPUFrequency();
#elif defined(__arm__) || defined(__aarch64__)
#ifdef __APPLE__

    size_t size = sizeof(host.cpu_model);
    sysctlbyname("machdep.cpu.brand_string", &host.cpu_model, &size, NULL, 0);
#endif
#endif

    MLOGINFO("CPU model: %s", host.cpu_model);
    MLOGINFO("CPU Frequency: %dMHz", (unsigned)(host.cpufq / 1000000));
}

// Performance management
BaseFrequency_t Emulator::GetSpeed()
{
    return (BaseFrequency_t)_context->coreState.baseFreqMultiplier;
}

void Emulator::SetSpeed(BaseFrequency_t speed)
{
    _core->SetCPUClockSpeed(speed);
}

void Emulator::SetSpeedMultiplier(uint8_t multiplier)
{
    _core->SetSpeedMultiplier(multiplier);
}

void Emulator::EnableTurboMode(bool withAudio)
{
    _core->EnableTurboMode(withAudio);
}

void Emulator::DisableTurboMode()
{
    _core->DisableTurboMode();
}

bool Emulator::IsTurboMode() const
{
    return _core->IsTurboMode();
}

/// region <Integration interfaces>

EmulatorContext* Emulator::GetContext()
{
    return _context;
}

ModuleLogger* Emulator::GetLogger()
{
    return _context->pModuleLogger;
}

MainLoop* Emulator::GetMainLoop()
{
    return _mainloop;
}

Memory* Emulator::GetMemory()
{
    return _context->pMemory;
}

DebugManager* Emulator::GetDebugManager()
{
    return _debugManager;
}

BreakpointManager* Emulator::GetBreakpointManager()
{
    return _breakpointManager;
}

FramebufferDescriptor Emulator::GetFramebuffer()
{
    return _context->pScreen->GetFramebufferDescriptor();
}

void Emulator::SetAudioCallback(void* obj, AudioCallback callback)
{
    // Use memory_order_release to ensure all previous writes are visible to the emulator thread
    _context->pAudioManagerObj.store(obj, std::memory_order_release);
    _context->pAudioCallback.store(callback, std::memory_order_release);

    MLOGINFO("Emulator::SetAudioCallback() - Audio callback set: obj=%p, callback=%p", obj, (void*)callback);
}

void Emulator::ClearAudioCallback()
{
    // Use memory_order_release to ensure the nullptr writes are visible to the emulator thread
    _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    _context->pAudioCallback.store(nullptr, std::memory_order_release);

    MLOGINFO("Emulator::ClearAudioCallback() - Audio callback cleared for emulator %s", _emulatorId.c_str());
}

/// endregion </Integration interfaces>

// region Regular workflow

void Emulator::Reset()
{
    // To avoid race conditions, we must pause the emulator during reset
    // (Z80 thread executing ROM code during reset can cause inconsistent state)
    bool wasRunning = _isRunning && !_isPaused;

    if (wasRunning)
    {
        // Pause the emulator
        Pause();

        // Give the emulator thread time to fully pause
        // (it needs to finish the current frame and enter pause loop)
        sleep_ms(20);
    }

    // Now perform reset while paused (safe, no race condition)
    _core->Reset();

    // Resume if it was running before
    if (wasRunning)
    {
        Resume();
    }
}

void Emulator::Start()
{
    // Skip if not initialized
    if (!_initialized)
    {
        MLOGERROR("Emulator::Start() - not initialized");
        return;
    }

    // Set running state (may already be set by StartAsync() - that's OK)
    _isPaused = false;
    _isRunning = true;
    _stopRequested = false;

    // Broadcast notification - Emulator started
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StateRun);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);

    // Update state
    SetState(StateRun);

    // Pass execution to the main loop
    // It will return only after stop request
    _mainloop->Run(_stopRequested);
}

void Emulator::StartAsync()
{
    // Stop the existing thread
    if (_asyncThread)
        Stop();

    // Set running state immediately to prevent race conditions with UI state updates
    // This ensures that IsRunning() returns true immediately after StartAsync() returns
    _isPaused = false;
    _isRunning = true;
    _stopRequested = false;

    // Start new thread with name 'emulator-xxxxxxxxxxxx' (last 12 chars of UUID) and execute Start() method from it
    // The short ID matches the shared memory naming convention for consistency
    std::string shortId = _emulatorId.length() > 12 ? _emulatorId.substr(_emulatorId.length() - 12) : _emulatorId;
    std::string threadName = "emulator-" + shortId;

    _asyncThread = new std::thread([this, threadName]() {
        ThreadHelper::setThreadName(threadName.c_str());

        this->Start();
    });
}

/// @brief Pauses emulator execution
/// 
/// Pauses the Z80 emulation thread. When paused, the emulator stops executing
/// instructions but remains in memory and can be resumed.
///
/// @param broadcast If true (default), broadcasts StatePaused to UI and listeners.
///                  If false, performs a "silent" pause without triggering UI updates.
///
/// @note Use broadcast=false for internal operations where:
///       - Memory is being reallocated (shared memory migration)
///       - State is temporarily inconsistent and UI refresh would crash
///       - You want an atomic pause/operation/resume without visible state flicker
///
/// @warning Silent pause (broadcast=false) should always be paired with silent resume.
///          The UI will not know the emulator was paused, so don't leave it paused.
///
/// @example
///   // User-initiated pause (shows in debugger):
///   emulator->Pause();  // or Pause(true)
///   
///   // Internal pause for memory migration (invisible to UI):
///   emulator->Pause(false);
///   // ... perform migration ...
///   emulator->Resume(false);
void Emulator::Pause(bool broadcast)
{
    if (_isPaused)
        return;

    if (!_isRunning || !_mainloop)
    {
        // Cannot pause if not running or mainloop not initialized
        return;
    }

    _isPaused = true;
    // NOTE: Do NOT set _isRunning = false here!
    // The emulator thread is still active, just paused.
    // Setting _isRunning = false would cause Stop() to skip _asyncThread->join(),
    // leading to a crash when RemoveEmulator() destroys memory while thread is still running.
    // MainLoop::Run() will detect this via Emulator::IsPaused() check.

    // Update state and broadcast only if requested
    // broadcast=false is used for internal operations like shared memory migration
    // where we don't want to trigger UI updates during the brief pause
    if (broadcast)
    {
        SetState(StatePaused);

        // Broadcast notification - Emulator execution paused
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleNumberPayload* payload = new SimpleNumberPayload(StatePaused);
        messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
    }
}

/// @brief Resumes emulator execution after pause
/// 
/// Resumes the Z80 emulation thread from a paused state. The emulator continues
/// executing from where it was paused.
///
/// @param broadcast If true (default), broadcasts StateResumed to UI and listeners.
///                  If false, performs a "silent" resume without triggering UI updates.
///
/// @note Use broadcast=false for internal operations where:
///       - Memory was just reallocated and you used silent pause
///       - You want seamless resume without UI state flicker
///       - The pause was for an internal atomic operation (not user-initiated)
///
/// @warning Must match the pause mode: if Pause(false) was called, use Resume(false).
///          Mismatched broadcast flags can leave UI in inconsistent state.
///
/// @see Emulator::Pause
void Emulator::Resume(bool broadcast)
{
    if (!_isPaused)
    {
        return;
    }

    if (!_mainloop)
    {
        // Cannot resume if mainloop not initialized
        return;
    }

    _stopRequested = false;
    _isPaused = false;
    // MainLoop::Run() will detect this via Emulator::IsPaused() check and resume.

    // Note: Don't unconditionally set _isRunning = true here.
    // In synchronous test mode, _isRunning may be false and should stay false.
    // The main RunAsync() path handles _isRunning appropriately.

    // Update state and broadcast only if requested
    if (broadcast)
    {
        SetState(StateResumed);

        // Broadcast notification - Emulator execution resumed
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleNumberPayload* payload = new SimpleNumberPayload(StateResumed);
        messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
    }
}

/// @brief Blocks the calling thread until the emulator is resumed
/// 
/// Used by breakpoint handlers to pause execution while waiting for
/// the debugger or user to resume. This is the single source of truth
/// for pause/resume synchronization.
void Emulator::WaitWhilePaused()
{
    while (_isPaused)
    {
        if (!_stopRequested)
        {
            // Wait in a loop if stop is not requested
            sleep_ms(20);
        }
        else
        {
            // Stop requested - exit the loop
            break;
        }
    }
}

void Emulator::Stop()
{
    // Use atomic compare-exchange to ensure only ONE thread executes stop logic
    // This prevents double-free of _asyncThread when Stop() is called multiple times
    bool expected = true;
    if (!_isRunning.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
        // Already stopped or another thread is currently stopping - safe to return
        return;
    }

    // Request emulator to stop
    _stopRequested = true;

    // If emulator was paused - un-pause, allowing mainloop to react
    // MainLoop::Run() will detect this via Emulator::IsPaused() check
    if (_isPaused)
    {
        _isPaused = false;
    }

    // TODO: handle IO shutting down
    // FDC: flush changes to disk image(s)
    // HDD: flush changes and unmount
    // Fully shut down video / sound

    // If executed in async thread - wait for thread finish and destroy it
    if (_asyncThread && _asyncThread->joinable())
    {
        _asyncThread->join();
        delete _asyncThread;
        _asyncThread = nullptr;
    }

    // Clear remaining state
    _stopRequested = false;
    _isPaused = false;

    // Broadcast notification - Emulator stopped
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StateStopped);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

// endregion

/// region <File operations>

bool Emulator::LoadSnapshot(const std::string& path)
{
    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
    {
        MLOGWARNING("LoadSnapshot rejected - emulator is being destroyed");
        return false;
    }

    bool result = false;

    /// region <Info logging>

    MLOGEMPTY();
    MLOGINFO("Loading snapshot from file: '%s'", path.c_str());

    /// endregion </Info logging>

    // Validate path exists
    std::string absolutePath = FileHelper::AbsolutePath(path);
    if (!FileHelper::FileExists(absolutePath))
    {
        MLOGERROR("Snapshot file not found: {}", absolutePath.c_str());
        return false;
    }

    // Validate file extension
    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(absolutePath));
    if (ext != "z80" && ext != "sna")
    {
        MLOGERROR("Invalid snapshot format: {}. Expected .z80 or .sna", ext.c_str());
        return false;
    }

    // Pause execution
    bool wasRunning = false;
    if (!IsPaused())
    {
        Pause();
        wasRunning = true;
    }

    if (ext == "sna")
    {
        /// region <Load SNA snapshot>
        LoaderSNA loaderSna(_context, absolutePath);
        result = loaderSna.load();

        /// region <Info logging>
        if (result)
        {
            MLOGINFO("SNA file loaded successfully, executing it...");
        }

        MLOGEMPTY();
        /// endregion </Info logging>

        /// endregion </Load SNA snapshot>
    }
    else if (ext == "z80")
    {
        /// region <Load Z80 snapshot>
        LoaderZ80 loaderZ80(_context, absolutePath);
        result = loaderZ80.load();

        /// region <Info logging>
        if (result)
        {
            MLOGINFO("Z80 file loaded successfully, executing it...");
        }

        MLOGEMPTY();
        /// endregion </Info logging>

        /// endregion </Load Z80 snapshot>
    }

    // Store snapshot path on success
    if (result)
    {
        _context->coreState.snapshotFilePath = absolutePath;
    }

    // Resume execution
    if (wasRunning)
    {
        // TODO: uncomment for the release
        Resume();
    }

    return result;
}

bool Emulator::SaveSnapshot(const std::string& path)
{
    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
    {
        MLOGWARNING("SaveSnapshot rejected - emulator is being destroyed");
        return false;
    }

    bool result = false;

    /// region <Info logging>

    MLOGEMPTY();
    MLOGINFO("Saving snapshot to file: '%s'", path.c_str());

    /// endregion </Info logging>

    // Resolve to absolute path
    std::string absolutePath = FileHelper::AbsolutePath(path);

    // Validate file extension
    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(absolutePath));
    if (ext != "sna" && ext != "z80")
    {
        MLOGERROR("Invalid snapshot format for save: {}. Supported: .sna, .z80", ext.c_str());
        return false;
    }

    // Pause execution
    bool wasRunning = false;
    if (!IsPaused())
    {
        Pause();
        wasRunning = true;
    }

    if (ext == "sna")
    {
        /// region <Save SNA snapshot>
        LoaderSNA loaderSna(_context, absolutePath);
        result = loaderSna.save();

        /// region <Info logging>
        if (result)
        {
            MLOGINFO("SNA file saved successfully: '%s'", absolutePath.c_str());
        }
        else
        {
            MLOGERROR("Failed to save SNA file: '%s'", absolutePath.c_str());
        }

        MLOGEMPTY();
        /// endregion </Info logging>

        /// endregion </Save SNA snapshot>
    }
    else if (ext == "z80")
    {
        /// region <Save Z80 snapshot>
        LoaderZ80 loaderZ80(_context, absolutePath);
        result = loaderZ80.save();

        /// region <Info logging>
        if (result)
        {
            MLOGINFO("Z80 file saved successfully: '%s'", absolutePath.c_str());
        }
        else
        {
            MLOGERROR("Failed to save Z80 file: '%s'", absolutePath.c_str());
        }

        MLOGEMPTY();
        /// endregion </Info logging>

        /// endregion </Save Z80 snapshot>
    }

    // Store snapshot path on success
    if (result)
    {
        _context->coreState.snapshotFilePath = absolutePath;
    }

    // Resume execution
    if (wasRunning)
    {
        Resume();
    }

    return result;
}

bool Emulator::LoadTape(const std::string& path)
{
    bool result = false;

    MLOGEMPTY();
    MLOGINFO("Loading tape from file: '%s'", path.c_str());

    // Validate and resolve path
    std::string resolvedPath = FileHelper::AbsolutePath(path);

    // Check file exists
    if (!FileHelper::FileExists(resolvedPath))
    {
        MLOGERROR("LoadTape() - File not found: '%s'", path.c_str());
        return false;
    }

    // Validate extension
    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(resolvedPath));
    if (ext != "tap" && ext != "tzx")
    {
        MLOGERROR("LoadTape() - Invalid tape format: .%s (expected .tap or .tzx)", ext.c_str());
        return false;
    }

    // Store validated path
    _context->coreState.tapeFilePath = resolvedPath;

    MLOGINFO("Tape file validated and ready: '%s'", resolvedPath.c_str());
    result = true;

    return result;
}

bool Emulator::LoadDisk(const std::string& path)
{
    bool result = false;

    MLOGEMPTY();
    MLOGINFO("Loading disk image from file: '%s'", path.c_str());

    // Validate and resolve path
    std::string resolvedPath = FileHelper::AbsolutePath(path);

    // Check file exists
    if (!FileHelper::FileExists(resolvedPath))
    {
        MLOGERROR("LoadDisk() - File not found: '%s'", path.c_str());
        return false;
    }

    // Validate extension
    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(resolvedPath));
    if (ext == "trd")
    {
        LoaderTRD loaderTrd(_context, resolvedPath);
        if (loaderTrd.loadImage())
        {
            // FIXME: use active drive, not fixed A:

            /// region <Free memory from previous disk image>
            if (_context->pBetaDisk)
            {
                _context->pBetaDisk->ejectDisk();
            }
            if (_context->coreState.diskDrives[0])
            {
                _context->coreState.diskDrives[0]->ejectDisk();
            }

            DiskImage* diskImage = _context->coreState.diskImages[0];

            if (diskImage != nullptr)
            {
                delete diskImage;
            }
            /// endregion </ree memory from previous disk image>

            /// region <Load new disk image and mount it>
            diskImage = loaderTrd.getImage();
            _context->coreState.diskImages[0] = diskImage;

            if (_context->coreState.diskDrives[0])
            {
                _context->coreState.diskDrives[0]->insertDisk(diskImage);
            }
            
            // Store file path for API queries
            _context->coreState.diskFilePaths[0] = resolvedPath;

            /// endregion </Load new disk image and mount it>
            
            result = true;  // Successfully loaded TRD disk
        }
    }

    if (ext == "scl")
    {
        LoaderSCL loader(_context, path);
        if (loader.loadImage())
        {
            // FIXME: use active drive, not fixed A:

            /// region <Free memory from previous disk image>
            if (_context->pBetaDisk)
            {
                _context->pBetaDisk->ejectDisk();
            }
            if (_context->coreState.diskDrives[0])
            {
                _context->coreState.diskDrives[0]->ejectDisk();
            }

            DiskImage* diskImage = _context->coreState.diskImages[0];

            if (diskImage != nullptr)
            {
                delete diskImage;
            }
            /// endregion </ree memory from previous disk image>

            /// region <Load new disk image and mount it>
            diskImage = loader.getImage();
            _context->coreState.diskImages[0] = diskImage;

            if (_context->coreState.diskDrives[0])
            {
                _context->coreState.diskDrives[0]->insertDisk(diskImage);
            }
            
            // Store file path for API queries
            _context->coreState.diskFilePaths[0] = resolvedPath;
            
            /// endregion </Load new disk image and mount it>
            
            result = true;  // Successfully loaded SCL disk
        }
    }

    return result;
}

/// endregion </File operations>

// region Controlled flow

void Emulator::CancelPendingStepOver()
{
    // Only relevant in debug mode — skip entirely during normal emulation
    if (!_featureManager || !_featureManager->isEnabled(Features::kDebugMode))
        return;

    if (_pendingStepOverBpId != 0 && _breakpointManager)
    {
        MLOGDEBUG("Emulator::CancelPendingStepOver - Removing orphaned step-over breakpoint ID %d", _pendingStepOverBpId);
        _breakpointManager->RemoveBreakpointByID(_pendingStepOverBpId);

        // Reactivate any breakpoints that were deactivated during the step-over
        for (uint16_t bpId : _stepOverDeactivatedBps)
        {
            _breakpointManager->ActivateBreakpoint(bpId);
        }

        _pendingStepOverBpId = 0;
        _stepOverDeactivatedBps.clear();
    }
}

void Emulator::RunSingleCPUCycle(bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] Z80& z80 = *_core->GetZ80();
    [[maybe_unused]] Memory& memory = *_context->pMemory;

    // TODO: synchronize with all timings within frame and I/O

    z80.Z80Step(skipBreakpoints);
    z80.OnCPUStep();

    // Notify the debugger that a step has been performed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);

    // New frame to be started
    if (z80.t >= config.frame)
    {
        _core->AdjustFrameCounters();
        _context->pScreen->ResetPrevTstate();
    }
}

void Emulator::RunNCPUCycles(unsigned cycles, bool skipBreakpoints)
{
    for (unsigned i = 0; i < cycles; i++)
    {
        RunSingleCPUCycle(skipBreakpoints);

        if (_stopRequested)
            break;
    }
}

void Emulator::RunFrame(bool skipBreakpoints)
{
    CancelPendingStepOver();

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // Use persistent target to prevent cumulative drift.
    // First frame step records the target; subsequent calls reuse it.
    if (!_hasFrameStepTarget)
    {
        _frameStepTargetPos = z80.t % config.frame;
        _hasFrameStepTarget = true;
    }
    unsigned targetPos = _frameStepTargetPos;

    // INT interrupt timing — must match Z80FrameCycle pattern exactly
    // Without this, HALT-based programs never have ISRs fire and video memory is never updated
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;

    // INT interrupt handling lasts for more than 1 frame (matches Z80FrameCycle)
    if (int_end >= frameLimit)
    {
        int_end -= frameLimit;
        z80.int_pending = true;
        int_occurred = true;
    }

    // Phase 1: Run until frame counter increments (crosses one frame boundary)
    uint64_t startFrame = _context->emulatorState.frame_counter;

    while (_context->emulatorState.frame_counter == startFrame && !_stopRequested)
    {
        // Handle interrupts before each instruction — critical for HALT to resume
        z80.ProcessInterrupts(int_occurred, int_start, int_end);

        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }
    }

    // Phase 2: We're now at the start of a new frame (z80.t is small).
    // Single-step until we reach or pass targetPos.
    if (targetPos > 0 && !_stopRequested)
    {
        // Reset interrupt state for new frame
        int_occurred = false;
        if (int_end >= frameLimit)
        {
            z80.int_pending = true;
            int_occurred = true;
        }

        while (z80.t < targetPos && !_stopRequested)
        {
            z80.ProcessInterrupts(int_occurred, int_start, int_end);
            z80.Z80Step(skipBreakpoints);
            z80.OnCPUStep();

            if (z80.t >= frameLimit)
            {
                _core->AdjustFrameCounters();
                _context->pScreen->ResetPrevTstate();
                break;  // Safety: don't cross another frame boundary
            }
        }
    }

    // NOTE: Per-t-state ULA rendering already happens inside the loop via:
    // z80.OnCPUStep() → MainLoop::OnCPUStep() → pScreen->UpdateScreen()
    // No batch RenderOnlyMainScreen() needed — it would destroy multicolor effects.

    // Notify the debugger that a frame step has been performed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunNFrames(unsigned frames, bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    // Run exactly N frames worth of t-states
    unsigned targetTStates = frameLimit * frames;
    unsigned elapsed = 0;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    while (elapsed < targetTStates && !_stopRequested)
    {
        unsigned prevT = z80.t;

        z80.ProcessInterrupts(int_occurred, int_start, int_end);
        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        // Track elapsed t-states (handling frame wrap)
        unsigned stepT = z80.t >= prevT ? (z80.t - prevT) : z80.t;
        elapsed += stepT;

        // Handle frame boundary
        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();

            // Notify after each frame so debugger/visualizers can update
            messageCenter.Post(NC_EXECUTION_CPU_STEP);
        }
    }
}

void Emulator::RunTStates(unsigned tStates, bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    unsigned targetT = z80.t + tStates;

    while (z80.t < targetT && !_stopRequested)
    {
        z80.ProcessInterrupts(int_occurred, int_start, int_end);
        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        // Handle frame boundary
        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
            if (targetT >= frameLimit)
            {
                targetT -= frameLimit;
            }
        }
    }


    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilScanline(unsigned targetLine, bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    unsigned targetT = targetLine * config.t_line;

    // If we've already passed this scanline in the current frame, complete the frame first
    if (z80.t >= targetT)
    {
        while (z80.t < frameLimit && !_stopRequested)
        {
            z80.ProcessInterrupts(int_occurred, int_start, int_end);
            z80.Z80Step(skipBreakpoints);
            z80.OnCPUStep();
        }

        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }
    }

    // Now run to the target scanline
    while (z80.t < targetT && !_stopRequested)
    {
        z80.ProcessInterrupts(int_occurred, int_start, int_end);
        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }
    }


    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
    messageCenter.Post(NC_SCANLINE_BOUNDARY);
}

void Emulator::RunNScanlines(unsigned count, bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    // Anti-drift strategy: calculate absolute target t-state position
    // Remember current position within scanline and advance by exactly N scanlines
    unsigned posInScanline = z80.t % config.t_line;
    unsigned targetT = z80.t + (count * config.t_line);

    // Adjust target so we stop at the same position within the target scanline
    unsigned targetPosInScanline = targetT % config.t_line;
    if (targetPosInScanline > posInScanline)
    {
        targetT -= (targetPosInScanline - posInScanline);
    }

    while (z80.t < targetT && !_stopRequested)
    {
        z80.ProcessInterrupts(int_occurred, int_start, int_end);
        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        // Handle frame boundary
        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();

            // targetT wraps with frame counter adjustment
            if (targetT >= frameLimit)
            {
                targetT -= frameLimit;
            }
        }
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
    messageCenter.Post(NC_SCANLINE_BOUNDARY);
}

void Emulator::RunUntilNextScreenPixel(bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    // Use the screen's precomputed raster state for the exact first paper pixel position
    // This accounts for VSync, VBlank, top border, HSync, HBlank, and left border timing
    unsigned paperStartT = _context->pScreen->GetPaperStartTstate();

    if (z80.t < paperStartT)
    {
        // Before paper in current frame — run to paper start
        while (z80.t < paperStartT && !_stopRequested)
        {
            z80.ProcessInterrupts(int_occurred, int_start, int_end);
            z80.Z80Step(skipBreakpoints);
            z80.OnCPUStep();

            if (z80.t >= frameLimit)
            {
                _core->AdjustFrameCounters();
                _context->pScreen->ResetPrevTstate();
            }
        }
    }
    else
    {
        // After paper start or in paper area — complete frame and run to paper start of next frame
        while (z80.t < frameLimit && !_stopRequested)
        {
            z80.ProcessInterrupts(int_occurred, int_start, int_end);
            z80.Z80Step(skipBreakpoints);
            z80.OnCPUStep();
        }

        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }

        // Now run to paper start in the new frame
        while (z80.t < paperStartT && !_stopRequested)
        {
            z80.ProcessInterrupts(int_occurred, int_start, int_end);
            z80.Z80Step(skipBreakpoints);
            z80.OnCPUStep();

            if (z80.t >= frameLimit)
            {
                _core->AdjustFrameCounters();
                _context->pScreen->ResetPrevTstate();
            }
        }
    }


    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilInterrupt(bool skipBreakpoints)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();

    // INT timing parameters
    unsigned int_start = config.intstart;
    unsigned int_end = config.intstart + config.intlen;

    // Safety limit: max 2 frames worth of t-states to prevent infinite loops
    unsigned safetyLimit = config.frame * 2;
    unsigned elapsed = 0;

    while (!_stopRequested)
    {
        unsigned prevT = z80.t;

        // --- Inline INT signal generation (replaces ProcessInterrupts) ---
        // Set int_pending when t-state enters the INT window [intstart, intstart+intlen)
        unsigned tInFrame = z80.t % config.frame;
        if (tInFrame >= int_start && tInFrame < int_end)
        {
            z80.int_pending = true;
        }
        else
        {
            z80.int_pending = false;
        }

        // If INT is pending and CPU accepts it, handle it
        if (z80.int_pending && z80.iff1 && z80.t != z80.eipos)
        {
            z80.HandleINT();

            // INT accepted — CPU is now at ISR entry point
            break;
        }

        // Execute one Z80 instruction
        z80.Z80Step(skipBreakpoints);
        z80.OnCPUStep();

        // Track elapsed t-states (handling frame wrap)
        unsigned stepT = z80.t >= prevT ? (z80.t - prevT) : z80.t;
        elapsed += stepT;

        // Handle frame boundary
        if (z80.t >= config.frame)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }

        // Safety: don't run more than 2 frames
        if (elapsed >= safetyLimit)
        {
            MLOGWARNING("Emulator::RunUntilInterrupt - Safety limit reached (%u t-states), no interrupt accepted", elapsed);
            break;
        }
    }


    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilCondition(std::function<bool(const Z80State&)> predicate, unsigned maxTStates)
{
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // INT interrupt timing — must match Z80FrameCycle pattern
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;
    bool int_occurred = false;
    if (int_end >= frameLimit) { int_end -= frameLimit; z80.int_pending = true; int_occurred = true; }

    unsigned elapsed = 0;

    while (!_stopRequested)
    {
        unsigned prevT = z80.t;

        z80.ProcessInterrupts(int_occurred, int_start, int_end);
        z80.Z80Step(true);  // Skip breakpoints for condition-based execution
        z80.OnCPUStep();

        // Track elapsed t-states
        unsigned stepT = z80.t >= prevT ? (z80.t - prevT) : z80.t;
        elapsed += stepT;

        // Handle frame boundary
        if (z80.t >= frameLimit)
        {
            _core->AdjustFrameCounters();
            _context->pScreen->ResetPrevTstate();
        }

        // Check predicate
        if (predicate(z80))
        {
            break;
        }

        // Enforce safety limit if specified
        if (maxTStates > 0 && elapsed >= maxTStates)
        {
            MLOGWARNING("Emulator::RunUntilCondition - Safety limit reached (%u t-states)", elapsed);
            break;
        }
    }


    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::StepOver()
{
    // Early exit if not initialized or no debug manager
    if (!_initialized || !_debugManager)
    {
        MLOGERROR("Emulator::StepOver() - not initialized or no debug manager");
        return;
    }

    // Get required components
    Z80State* z80 = GetZ80State();
    Memory* memory = GetMemory();
    Z80Disassembler* disassembler = _debugManager->GetDisassembler().get();
    BreakpointManager* bpManager = _breakpointManager;
    FeatureManager* fm = GetFeatureManager();

    if (!z80 || !memory || !disassembler || !bpManager || !fm)
    {
        MLOGERROR("Emulator::StepOver() - required components not available");
        return;
    }

    uint16_t currentPC = z80->pc;

    // Read instruction bytes to check if step-over is needed
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(currentPC + i);
    }

    if (!disassembler->shouldStepOver(buffer))
    {
        MLOGDEBUG("Emulator::StepOver() - instruction at 0x%04X doesn't need step-over, doing normal step", currentPC);
        RunSingleCPUCycle(true);
        return;
    }

    uint16_t nextInstructionAddress = disassembler->getNextInstructionAddress(currentPC, memory);
    if (nextInstructionAddress == currentPC)
    {
        MLOGDEBUG("Emulator::StepOver() - couldn't determine next instruction address, doing normal step");
        RunSingleCPUCycle(true);
        return;
    }

    MLOGDEBUG("Emulator::StepOver() - instruction requires step-over, next instruction at 0x%04X",
              nextInstructionAddress);

    // Deactivate breakpoints within the called function's scope
    std::vector<std::pair<uint16_t, uint16_t>> exclusionRanges =
        disassembler->getStepOverExclusionRanges(currentPC, memory, 5);
    std::vector<uint16_t> deactivatedBreakpoints;
    const auto& allBreakpoints = bpManager->GetAllBreakpoints();
    for (const auto& [bpId, bp] : allBreakpoints)
    {
        if (bp->active && (bp->type == BRK_MEMORY) && (bp->memoryType & BRK_MEM_EXECUTE))
        {
            for (const auto& range : exclusionRanges)
            {
                if (bp->z80address >= range.first && bp->z80address <= range.second)
                {
                    bpManager->DeactivateBreakpoint(bpId);
                    deactivatedBreakpoints.push_back(bpId);
                    MLOGDEBUG("Emulator::StepOver() - temporarily deactivated breakpoint at 0x%04X", bp->z80address);
                    break;
                }
            }
        }
    }

    // Create a temporary breakpoint at the next instruction
    BreakpointDescriptor* bpDesc = new BreakpointDescriptor();
    bpDesc->type = BreakpointTypeEnum::BRK_MEMORY;
    bpDesc->memoryType = BRK_MEM_EXECUTE;
    bpDesc->z80address = nextInstructionAddress;
    bpDesc->note = "StepOver";
    uint16_t stepOverBreakpointID = bpManager->AddBreakpoint(bpDesc);

    if (stepOverBreakpointID == BRK_INVALID)
    {
        MLOGERROR("Emulator::StepOver() - failed to set breakpoint at 0x%04X", nextInstructionAddress);
        // Restore any deactivated breakpoints before failing
        for (uint16_t id : deactivatedBreakpoints)
            bpManager->ActivateBreakpoint(id);
        RunSingleCPUCycle(true);
        return;
    }
    // Store tracking state for orphan cleanup
    _pendingStepOverBpId = stepOverBreakpointID;
    _stepOverDeactivatedBps = deactivatedBreakpoints;

    // Save original feature states
    bool originalDebugMode = fm->isEnabled(Features::kDebugMode);
    bool originalBreakpoints = fm->isEnabled(Features::kBreakpoints);
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kBreakpoints, true);

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    // Lambda captures this pointer to clear tracking state
    auto breakpoint_handler = [this, bpManager, stepOverBreakpointID, fm, originalDebugMode,
                              originalBreakpoints](int /*id*/, Message* message) mutable {
        if (!message || !message->obj)
            return;

        auto payload = static_cast<SimpleNumberPayload*>(message->obj);
        uint16_t triggeredBreakpointID = static_cast<uint16_t>(payload->_payloadNumber);

        if (triggeredBreakpointID == stepOverBreakpointID)
        {
            MLOGDEBUG("Emulator::StepOver() - cleanup for breakpoint ID %d", stepOverBreakpointID);
            
            // Remove breakpoint
            bpManager->RemoveBreakpointByID(stepOverBreakpointID);
            
            // Reactivate deactivated breakpoints
            for (uint16_t deactivatedId : _stepOverDeactivatedBps)
            {
                bpManager->ActivateBreakpoint(deactivatedId);
            }
            
            // Restore feature flags
            fm->setFeature(Features::kDebugMode, originalDebugMode);
            fm->setFeature(Features::kBreakpoints, originalBreakpoints);

            // Clear tracking state
            _pendingStepOverBpId = 0;
            _stepOverDeactivatedBps.clear();
            
            MLOGDEBUG("Emulator::StepOver() - cleanup complete");
        }
    };

    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, breakpoint_handler);

    // Resume execution - returns immediately (non-blocking)
    MLOGDEBUG("Emulator::StepOver() - Resuming execution to hit breakpoint at 0x%04X", nextInstructionAddress);
    Resume();
    
    // No blocking wait - UI stays responsive
}

/// Load ROM file (up to 64 banks to ROM area)
/// \param path File path to ROM file
bool Emulator::LoadROM(std::string path)
{
    Pause();

    ROM& rom = *_core->GetROM();

    bool result = rom.LoadROM(path, _memory->ROMBase(), MAX_ROM_PAGES);

    return result;
}

void Emulator::DebugOn()
{
    // Switch to slow but instrumented memory interface
    _core->UseDebugMemoryInterface();

    _isDebug = true;
    _z80->isDebugMode = true;
}

void Emulator::DebugOff()
{
    // Switch to fast memory interface
    _core->UseFastMemoryInterface();

    _isDebug = false;
    _z80->isDebugMode = false;
}

Z80State* Emulator::GetZ80State()
{
    return static_cast<Z80State*>(_z80);
}

// endregion

// region Status

// Identity and state methods

UUID Emulator::GetUUID() const
{
    return _uuid;
}

const std::string& Emulator::GetId() const
{
    return _emulatorId;
}

std::string Emulator::GetSymbolicId() const
{
    return _symbolicId;
}

void Emulator::SetSymbolicId(const std::string& symbolicId)
{
    _symbolicId = symbolicId;
    UpdateLastActivity();
}

// Timestamp helpers
void Emulator::UpdateLastActivity()
{
    _lastActivity = std::chrono::system_clock::now();
}

std::chrono::system_clock::time_point Emulator::GetCreationTime() const
{
    return _createdAt;
}

std::chrono::system_clock::time_point Emulator::GetLastActivityTime() const
{
    return _lastActivity;
}

std::string Emulator::GetUptimeString() const
{
    auto now = std::chrono::system_clock::now();
    auto duration = now - _createdAt;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration).count() % 24;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration).count() % 60;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count() % 60;

    std::ostringstream ss;
    ss << std::setw(2) << std::setfill('0') << hours << ":" << std::setw(2) << std::setfill('0') << minutes << ":"
       << std::setw(2) << std::setfill('0') << seconds;
    return ss.str();
}

EmulatorStateEnum Emulator::GetState()
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    return _state;
}

void Emulator::SetState(EmulatorStateEnum state)
{
    std::lock_guard<std::mutex> lock(_stateMutex);
    _state = state;
    UpdateLastActivity();
    MLOGINFO("Emulator %s state changed to: %s", _emulatorId.c_str(), getEmulatorStateName(state));
}

std::string Emulator::GetInstanceInfo()
{
    std::time_t createdTime = std::chrono::system_clock::to_time_t(_createdAt);
    std::time_t lastActivityTime = std::chrono::system_clock::to_time_t(_lastActivity);

    std::ostringstream ss;
    ss << "UUID: " << _emulatorId << "\n"
       << "Symbolic ID: " << (_symbolicId.empty() ? "[not set]" : _symbolicId) << "\n"
       << "Created at: " << std::ctime(&createdTime) << "Last activity: " << std::ctime(&lastActivityTime)
       << "Uptime: " << GetUptimeString() << "\n"
       << "State: " << getEmulatorStateName(_state);

    // ctime adds a newline, so we need to remove the last one
    std::string result = ss.str();
    if (!result.empty() && result[result.length() - 1] == '\n')
    {
        result.erase(result.length() - 1);
    }

    return result;
}

bool Emulator::IsRunning()
{
    return _isRunning;
}

bool Emulator::IsPaused()
{
    return _isPaused;
}

bool Emulator::IsDestroying()
{
    return _state == StateDestroying || _isReleased;
}

bool Emulator::IsDebug()
{
    return _isDebug;
}

std::string Emulator::GetStatistics()
{
    EmulatorState& state = _context->emulatorState;
    Memory& memory = *_context->pMemory;
    Z80& z80 = *_context->pCore->GetZ80();

    std::string dump = z80.DumpZ80State();
    std::string cpuState = string(StringHelper::Trim(dump));

    std::string result = StringHelper::Format("  Frame: %d\n", state.frame_counter);
    result +=
        StringHelper::Format("  t (frame-relative): %s\n", StringHelper::FormatWithThousandsDelimiter(z80.t).c_str());
    result += StringHelper::Format("  Memory:\n    %s\n", memory.DumpMemoryBankInfo().c_str());
    result += StringHelper::Format("  CPU: %s", cpuState.c_str());

    return result;
}


// endregion
