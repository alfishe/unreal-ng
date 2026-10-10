#include "emulator.h"

#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/io/tape/tapeslot.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediamanager.h"
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
#include "debugger/labels/labelmanager.h"
#include <atomic>
#include <cstdlib>

#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/notifications.h"
#include "emulator/tstaterunbudget.h"
#include "emulator/media/mediamanager.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/memory/scorpion/scorpionromwindow.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/nex/loadernex.h"
#include "loaders/snapshot/loaderspg.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/tape/loader_tape.h"

/// Emulator::SetDefaultTtdRecordingRoot: where the engine writes recordings for instances created from now on
static std::mutex g_ttdRecordingRootMutex;
static std::string g_ttdRecordingRoot;

/// region <Constructors / Destructors>

Emulator::Emulator(LoggerLevel level) : Emulator("", level) {}

Emulator::Emulator(const std::string& symbolicId, LoggerLevel level)
{
    _uuid = unreal::UUID::Generate(); // Generate new unique UUID
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
        _context->emulatorId = _uuid;

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

    // Clean up FeatureManager BEFORE Release(), because Release() deletes _context.
    // Accessing _context->pFeatureManager after Release() is a use-after-free.
    if (_featureManager)
    {
        if (_context)
            _context->pFeatureManager = nullptr;
        delete _featureManager;
        _featureManager = nullptr;
    }

    // Ensure resources are released if Release() wasn't called explicitly
    if (_initialized.load(std::memory_order_acquire))
    {
        Release();
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
        // Use custom config path if set, otherwise resolve the config from
        // the selected platform model: configs/<model>/unreal.ini
        if (!_customConfigPath.empty())
        {
            result = _config->LoadConfigFile(_customConfigPath);
        }
        else
        {
            std::string configFolder = Config::GetConfigFolderForModel(_preferredModel, _preferredRamSize);
            result = _config->LoadConfig(configFolder);
        }

        if (result)
        {
            MLOGDEBUG("Emulator::Init - Config file successfully loaded");

            // Apply the programmatically-requested model (if any) now - before
            // any model-dependent subsystem (ROMs, port decoder, screen) reads
            // the config. Overrides the INI's HIMEM/RamSize selection and gets
            // canonical frame geometry for the model.
            if (_hasPreferredModel)
            {
                _context->config.mem_model = _preferredModel;
                _context->config.ramsize = _preferredRamSize;
                _config->ApplyModelTimingDefaults(_context->config, true /* canonicalGeometry */);
                MLOGINFO("Emulator::Init - Applied preferred model %d (INI HIMEM overridden)",
                         (int)_preferredModel);
            }

            // The caller's per-instance adjustments get the last word before
            // any device is created from the config
            if (_configOverride)
                _configOverride(_context->config);
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

        // Peripherals register their media slots while Core initializes
        if (_context->pMediaManager == nullptr)
            _context->pMediaManager = new MediaManager(_context);

        _core = new Core(_context);
        if (_core && _core->Init())
        {
            MLOGDEBUG("Emulator::Init - CPU system core created");

            _context->pCore = _core;

            _z80 = _core->GetZ80();
            _memory = _core->GetMemory();

            // The drives exist and the disk controllers are known: floppy slots register now
            _floppySlots = new FloppyDriveSlots(_context);
            _tapeSlot = new TapeSlot(_context);

            result = true;
        }
        else
        {
            // A configuration the machine refuses (the slot set's conflicts, Q8) says why
            if (_core)
                _initError = _core->GetInitError();
            MLOGERROR("Emulator::Init - CPU system core (or main peripheral devices) creation failed%s%s",
                      _initError.empty() ? "" : ": ", _initError.c_str());
            if (!_initError.empty())
                return false;
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

    // The labels of the known ROMs the machine runs (symbol bundles)
    if (result)
        ApplySymbolBundles();

    // Create TTD manager (per parent TDD §10.2). Always constructed; the
    // per-frame capture cost is gated by the cached _feature_ttd_enabled
    // bool in Memory (no work when timetravel feature is off). The manager
    // also exposes GetState() == Idle until StartRecording() is called.
    if (result)
    {
        // Both implementations exist; the selected one is what the core and the
        // verbs drive (Phase 5): the engine's controller by default
        ttd::TimeTravelManager* ttdManager = new ttd::TimeTravelManager(_context);
        _context->pTimeTravelManager = ttdManager;
        if (DefaultTimeTravelBackend() == TimeTravelBackend::Engine)
        {
            ttd::TimeTravelController* controller = new ttd::TimeTravelController(_context);
            {
                std::lock_guard<std::mutex> lock(g_ttdRecordingRootMutex);
                if (!g_ttdRecordingRoot.empty())
                    controller->SetShadowRecordingRoot(g_ttdRecordingRoot);
            }
            _context->pTimeTravelController = controller;
            _context->pTimeTravelHooks = controller;
            _context->ttdWriteSink = controller;
            MLOGDEBUG("Emulator::Init - time travel: the engine's controller");
        }
        else
        {
            _context->pTimeTravelHooks = ttdManager;
            _context->ttdWriteSink = ttdManager;
            MLOGDEBUG("Emulator::Init - time travel: v1's manager");
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


    // Configured media go in before the first reset: firmware may boot from them
    if (result && _context->pMediaManager && _config)
    {
        for (const std::string& line : _config->GetMediaReport())
            MLOGWARNING("Emulator::Init - media config: %s", line.c_str());
        for (const std::string& line : _context->pMediaManager->ApplyConfiguredMedia(_config->GetMediaSet()))
            MLOGWARNING("Emulator::Init - media: %s", line.c_str());
    }

    // Devices request machine-level actions through the context (the ZX-Evo
    // AVR's F12 soft reset): the sink is installed before the first reset so
    // the wiring is complete before the guest runs
    if (result && _context)
        _context->pSoftResetSink = this;

    // Reset CPU and set-up all ports / ROM and RAM pages
    if (result)
    {
        _core->Reset();

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

        // Power-on: start frame 0 (frame-start hooks incl. the default video
        // render init, CPU frame geometry) once the feature caches are live -
        // every later frame is started by the previous frame's boundary
        RestartFrame();

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
    // No context lease outlives the context, whoever releases. EmulatorManager
    // has already waited for the leases (with no lock held), so there this
    // never blocks
    BeginRetirement();
    std::unique_lock<std::shared_mutex> leaseLock(_leaseMutex);

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

    // The step-over audio hold must not outlive the sound manager it points to
    _stepOverHostHold.Release();

    // Cleanup any pending step-over operation (orphan cleanup; the emulation thread is stopped by now)
    if (const uint16_t pending = _pendingStepOverBpId.exchange(0))
    {
        MLOGDEBUG("Emulator::ReleaseNoGuard - Cleaning up orphaned step-over breakpoint ID %d", pending);
        FinishStepOver(pending, false);
    }

    // Release debug manager (and related components)
    if (_context->pDebugManager)
    {
        delete _context->pDebugManager;
        _context->pDebugManager = nullptr;
    }

    // Release TTD manager. The manager's destructor releases all page-store
    // refs held by the timeline before the page store itself goes away.
    _context->pTimeTravelHooks = nullptr;
    if (_context->pTimeTravelController)
    {
        delete _context->pTimeTravelController;   // clears the sinks it set
        _context->pTimeTravelController = nullptr;
    }
    if (_context->pTimeTravelManager)
    {
        delete _context->pTimeTravelManager;
        _context->pTimeTravelManager = nullptr;
    }
    _context->ttdWriteSink = nullptr;

    // Stop and release main loop
    if (_mainloop != nullptr)
    {
        _mainloop->Stop();

        delete _mainloop;
        _mainloop = nullptr;
    }

    // RZX playback: its hooks leave the CPU and context before they go (the
    // loop no longer steps)
    {
        std::lock_guard<std::mutex> lock(_rzxMutex);
        std::lock_guard<std::mutex> sessionLock(_rzxSessionMutex);
        _rzxSession.reset();
    }

    /// region <Release additional peripheral devices>
    // GS / NGS
    // ZiFi
    // HDD/CD
    // Tape
    // Floppy
    // The slots detach their disks before the drives go; the media manager
    // (deleted after Core) owns and frees the disk images
    delete _floppySlots;
    _floppySlots = nullptr;
    delete _tapeSlot;  // the deck drops its copy of the tape; the manager frees the medium
    _tapeSlot = nullptr;

    for (size_t i = 0; i < 4; i++)
    {
        FDD* diskDrive = _context->coreState.diskDrives[i];
        if (diskDrive != nullptr)
        {
            diskDrive->ejectDisk();
            delete diskDrive;

            _context->coreState.diskDrives[i] = nullptr;
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

    // After Core: its peripherals have unregistered their slots
    if (_context->pMediaManager != nullptr)
    {
        delete _context->pMediaManager;
        _context->pMediaManager = nullptr;
    }

    // Release Config
    if (_config != nullptr)
    {
        delete _config;
        _config = nullptr;
    }

    // Release EmulatorContext as last step. Null the member BEFORE the delete:
    // concurrent GetContext() readers (UI widgets polling on their own thread)
    // then observe null instead of a freed pointer.
    if (_context != nullptr)
    {
        EmulatorContext* releasedContext = _context;
        _context = nullptr;
        delete releasedContext;
    }

    // The ModuleLogger is owned by the context we just deleted: every MLOG* call on this object from now on
    // (destructor, a late SetState(), GetState() from a lingering UI reference) must see a null logger, not a
    // dangling one. Observed as an access violation in ~Emulator when the frontend dropped its last
    // shared_ptr<Emulator> AFTER EmulatorManager::RemoveEmulator had already Release()d the instance.
    _logger = nullptr;
    _initialized.store(false, std::memory_order_release);
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

bool Emulator::SetSpeedMultiplier(uint8_t multiplier)
{
    // A lockstep group applies the change to all its members at a frame boundary
    // (it keeps the latest request, so no "same speed" shortcut here)
    {
        std::lock_guard<std::mutex> lock(_speedInterceptorMutex);
        if (_speedInterceptor)
            return _core->CanSetSpeedMultiplier(multiplier) && _speedInterceptor(multiplier);
    }

    // Re-selecting the current speed is not a change, and a refused one (TTD
    // recording allows only 1x) must not cost the session either
    if (_core->GetHostSpeedMultiplier() == multiplier)
        return true;
    if (!_core->CanSetSpeedMultiplier(multiplier))
        return false;

    // TTD v1 (P1.6): speed change invalidates the recording because frame
    // timing is part of the determinism contract (parent TDD §4.2 + §5 row 13).
    // Simpler to invalidate than to model; revisit if it proves annoying.
    // A black box stops before the machine runs faster and starts again at 1x (D29)
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd && multiplier != 1)
        ttd->OnAccelerationChanging(true);
    if (ttd)
        ttd->OnConfigurationChange(ttd::TTDConfigChangeKind::SpeedMultiplier, "speed-multiplier-change");

    const bool changed = _core->SetSpeedMultiplier(multiplier);
    if (ttd && multiplier == 1)
        ttd->OnAccelerationChanging(false);
    return changed;
}

void Emulator::SetSpeedChangeInterceptor(std::function<bool(uint8_t)> interceptor)
{
    std::lock_guard<std::mutex> lock(_speedInterceptorMutex);
    _speedInterceptor = std::move(interceptor);
}

std::string Emulator::RecordingGuard(ttd::TTDGuardedAction action) const
{
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    return ttd ? ttd->RecordingGuard(action) : std::string();
}

/// Refuse an action that would drop or corrupt the recording in progress
/// (logged; copied to `error` when the caller takes one)
static bool RecordingAllows(const Emulator& emulator, ttd::TTDGuardedAction action, std::string* error = nullptr)
{
    const std::string reason = emulator.RecordingGuard(action);
    if (reason.empty())
        return true;
    if (error)
        *error = reason;
    LOGWARNING("%s", reason.c_str());
    return false;
}

void Emulator::EditMemoryFromTool(const char* source, const std::function<void()>& edit)
{
    NoteDebugChange();
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    // Recording, or a session being browsed: the engine continues a recording
    // paused at this point and records the edit (D9)
    const bool session = ttd && ttd->GetState() != ttd::TTDSessionState::Idle;
    const bool onEmulationThread = _mainloop && _mainloop->IsRunThread();
    const bool park = session && !onEmulationThread && IsRunning() && !IsPaused();
    if (park)
    {
        Pause(false);
        WaitForPauseConfirmation(1000);
    }

    // v1 keeps a marker; the edit's bytes go with it, so the engine's replay applies it
    if (session)
        ttd->BeginToolEdit();
    edit();
    if (session)
        ttd->EndToolEdit(source);

    if (park)
        Resume(false);
}

uint8_t Emulator::GetSpeedMultiplier() const
{
    return _core ? _core->GetSpeedMultiplier() : 1;
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

Emulator::ContextLease Emulator::LeaseContext()
{
    // Never block: a writer exists only once _retiring is set (the removal sets
    // it before WaitForContextLeases() / Release() take the lock exclusively),
    // and a blocking shared lock behind a waiting writer would deadlock a thread
    // that already holds a lease (rwlocks may prefer writers). try_lock_shared
    // may fail spuriously, so retry until it succeeds or the removal shows up
    std::shared_lock<std::shared_mutex> lock(_leaseMutex, std::defer_lock);
    while (!lock.try_lock())
    {
        if (_retiring.load())
            return {};
        std::this_thread::yield();
    }
    // Checked under the shared lock: a lease that passes is one the removal
    // waits for; one taken after BeginRetirement() is refused
    if (_retiring.load() || _isReleased || _context == nullptr)
        return {};
    return ContextLease(std::move(lock), _context);
}

void Emulator::BeginRetirement()
{
    _retiring.store(true);
}

void Emulator::WaitForContextLeases()
{
    std::unique_lock<std::shared_mutex> lock(_leaseMutex);
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

void Emulator::SetAudioCallback(void* obj, AudioCallback callback, const std::atomic<uint32_t>* occupancyFrames,
                                const AudioDeviceDescriptor* deviceDescriptor)
{
    // Use memory_order_release to ensure all previous writes are visible to the emulator thread
    _context->pAudioManagerObj.store(obj, std::memory_order_release);
    _context->pAudioCallback.store(callback, std::memory_order_release);
    _context->pAudioRingOccupancy.store(occupancyFrames, std::memory_order_release);
    _context->pAudioDeviceDescriptor.store(deviceDescriptor, std::memory_order_release);

    MLOGINFO("Emulator::SetAudioCallback() - Audio callback set: obj=%p, callback=%p", obj, (void*)callback);
}

void Emulator::SetAudioDeviceSampleRate(uint32_t rate)
{
    _context->pAudioDeviceSampleRate.store(rate, std::memory_order_release);

    // Device (re)established: restart DRC tracking from the fresh occupancy
    // instead of stale pre-reroute EMA/integrator state
    if (_context->pSoundManager)
    {
        _context->pSoundManager->resetDrcController();
    }

    // Core rate priority chain (runtime pin > device > [SOUND] CoreRate):
    // a device-rate CHANGE (hotplug / reroute at a different native rate)
    // re-derives the target and requests a full pipeline re-rate - every
    // digital filter re-derives for the new core rate at the next frame
    // boundary on the emulation thread (SoundManager::handleFrameStart
    // applies it there; deferred while a recording is in progress). With a
    // runtime pin the target is unchanged (no-op) and only the DRC base
    // ratio follows the device. The ini CoreRate never participates while
    // a device is attached - it cannot lock a UI client's rate.
    if (_context->pSoundManager)
    {
        _context->pSoundManager->reevaluateCoreRate();
    }
}

const AudioDeviceDescriptor* Emulator::GetAudioDeviceDescriptor() const
{
    return _context->pAudioDeviceDescriptor.load(std::memory_order_acquire);
}

void Emulator::ClearAudioCallback()
{
    // A released instance has no context and no audio path left to clear
    if (!_context)
        return;

    // Use memory_order_release to ensure the nullptr writes are visible to the emulator thread
    _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    _context->pAudioCallback.store(nullptr, std::memory_order_release);
    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
    _context->pAudioDeviceDescriptor.store(nullptr, std::memory_order_release);

    MLOGINFO("Emulator::ClearAudioCallback() - Audio callback cleared for emulator %s", _emulatorId.c_str());
}

/// endregion </Integration interfaces>

// region Disk autostart

Emulator::DiskAutostartResult Emulator::AutostartDisk(const std::string& path, uint8_t drive)
{
    DiskAutostartResult result;

    if (drive != 0)
    {
        result.message = "autostart only supports drive A (TR-DOS/Beta 128's \"RUN boot\" convention "
                          "always boots from drive A) - insert into drive " +
                          std::string(1, static_cast<char>('A' + drive)) + " without autostart instead";
        MLOGERROR("AutostartDisk: %s", result.message.c_str());
        return result;
    }

    std::string loadError;
    result.mounted = LoadDisk(path, drive, &loadError);
    if (!result.mounted)
    {
        result.message = loadError.empty() ? "Disk could not be loaded" : loadError;
        return result;
    }

    auto report = [&](const std::string& text, bool started, bool error) {
        result.message = text;
        if (error)
            MLOGERROR("%s", text.c_str());
        else
            MLOGINFO("%s", text.c_str());
        MessageCenter::DefaultMessageCenter().Post(
            NC_DISK_AUTOSTART, new DiskAutostartPayload(_context->emulatorId, text, started, error));
    };

    FDD* driveA = _context->coreState.diskDrives[0];
    DiskImage* image = driveA ? driveA->getDiskImage() : nullptr;
    DiskAutostart* autostart = _context->pDiskAutostart;
    if (image == nullptr || autostart == nullptr)
    {
        report("Disk autostart is not available", false, true);
        return result;
    }

    // Prepare + reset run with the emulator thread parked (image and machine state are edited)
    bool wasRunning = _isRunning && !_isPaused;
    if (wasRunning)
    {
        Pause();
        sleep_ms(20);
    }

    DiskAutostart::Plan plan = autostart->Prepare(*image);
    const bool start = plan.action == DiskAutostart::Action::Boot || plan.action == DiskAutostart::Action::BootNamed ||
                       plan.action == DiskAutostart::Action::BootCommander ||
                       plan.action == DiskAutostart::Action::BootGenerated;

    if (start)
    {
        // Same rule as Reset(): the recording session ends (TTD D42)
        if (_context->pTimeTravelHooks)
            _context->pTimeTravelHooks->EndSession("autostart");

        autostart->Disarm();
        _core->Reset(RM_DOS);  // Quick reset straight into TR-DOS: PC = 0 with the DOS ROM active
        RestartFrame();        // Frame 0 starts at the reset state (see Reset())
        if (_context->pTimeTravelHooks)
            _context->pTimeTravelHooks->OnMachineReset();
        if (plan.action == DiskAutostart::Action::BootNamed)
            autostart->Arm(plan.bootName);
    }

    if (wasRunning)
    {
        Resume();
    }

    result.started = start;
    report(plan.message, start, plan.action == DiskAutostart::Action::Unsupported);
    return result;
}

// endregion Disk autostart

// region Regular workflow

void Emulator::Reset(bool hardReset)
{
    // A reset leaves the recorded path: an RZX playback ends first
    if (IsRzxPlaying())
        StopRzx();

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

    // TTD: Reset must NEVER touch the recorded timeline. The recorded
    // history is the user's property — they should be able to replay it
    // at any time, regardless of live emulator state.
    //
    // If recording is active, we must STOP it first. Otherwise _core->Reset()
    // would teleport frame_counter back to 0, and the next OnFrameBoundary
    // would append a checkpoint at frame 0 to a timeline that already has
    // checkpoints at higher frame numbers — breaking the sorted invariant
    // and corrupting every future seek.
    //
    // StopRecording() transitions Recording → Idle while retaining the
    // timeline. The user can seek, replay, or resume from any captured
    // point. After _core->Reset() runs, the live emulator is at frame 0
    // with fresh state, but the timeline is untouched.
    //
    // See parent TDD §4.2 (StopRecording retains history) and §5.1
    // (markers are for nondeterministic INPUT events, not for state
    // teleports that happen AFTER recording stops).
    // D42: the reset ENDS the recording session (a new one only by the `ttdrestart` feature)
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->EndSession("reset");

    // Now perform reset while paused (safe, no race condition).
    // The live emulator state is teleported; the TTD timeline is not.
    if (_context && _context->pDiskAutostart)
        _context->pDiskAutostart->Disarm();  // A user reset cancels a pending autostart hook

    // The keys the host still holds survive the reset button: on the real
    // machine they stay down across the Z80 reset, and boot firmware reads
    // them (TS-BIOS: hold Symbol Shift at reset to enter its setup). A power
    // cycle is different - the FPGA reconfigures and the boot starts clean
    const Keyboard::InputState heldKeys = !hardReset && _context && _context->pKeyboard
        ? _context->pKeyboard->CaptureInputState()
        : Keyboard::InputState{};

    // A ROM selected at runtime (RequestRomReload: the Sprinter's BIOS) is read now, with the
    // machine paused and TTD recording stopped; the session that relied on the old ROM is invalid
    if (_romReloadPending.exchange(false))
    {
        ROM& rom = *_core->GetROM();
        if (rom.LoadROM())
        {
            rom.CalculateSignatures();
            ApplySymbolBundles();
        }
        else
            MLOGERROR("Emulator::Reset - the selected ROM could not be loaded");
        if (_context && _context->pTimeTravelHooks)
            _context->pTimeTravelHooks->OnConfigurationChange(ttd::TTDConfigChangeKind::RomReload, "rom-reload");
    }

    _core->Reset();

    if (!hardReset)
    {
        if (_context && _context->pKeyboard)
            _context->pKeyboard->RestoreInputState(heldKeys);
    }
    else if (_context && _context->pPortDecoder)
    {
        _context->pPortDecoder->PowerCycle();
    }

    // The interrupted frame is abandoned: frame 0 starts at the reset state
    RestartFrame();

    // A machine seeked into recorded history leaves it (the timeline is kept)
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->OnMachineReset();

    // Resume if it was running before
    if (wasRunning)
    {
        Resume();
    }
}

void Emulator::RequestNMI()
{
    // Mutating CPU state from a host thread (WebAPI / GUI) while the frame loop
    // runs would race the Z80 thread - use the same pause guard as Reset()
    bool wasRunning = _isRunning && !_isPaused;
    if (wasRunning)
    {
        Pause(false);
        WaitForPauseConfirmation();
    }

    _core->GetZ80()->RequestNonMaskedInterrupt();

    if (wasRunning)
        Resume(false);
}

bool Emulator::SetFrontPanelSwitch(FrontPanelSwitch sw, bool on)
{
    PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    if (!decoder || !decoder->HasFrontPanelSwitch(sw))
        return false;

    ttd::TTDInputEvent ev;
    ev.kind = ttd::TTDInputKind::FrontPanelSwitch;
    ev.key = static_cast<uint8_t>(sw);
    ev.pressed = on;
    if (_context->pTimeTravelHooks)
        return _context->pTimeTravelHooks->SubmitLiveInput(ev);
    return ttd::ApplyInputEvent(ev, ttd::InputDevicesOf(_context));
}

int Emulator::GetFrontPanelSwitch(FrontPanelSwitch sw) const
{
    const PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    if (!decoder || !decoder->HasFrontPanelSwitch(sw))
        return -1;
    return decoder->GetFrontPanelSwitch(sw) ? 1 : 0;
}

void Emulator::RequestMNI()
{
    bool wasRunning = _isRunning && !_isPaused;
    if (wasRunning)
    {
        Pause(false);
        WaitForPauseConfirmation();
    }

    CONFIG& config = _context->config;
    EmulatorState& state = _context->emulatorState;

    // The magic button arms two DD50 flip-flops at once (hardware-reference §9):
    // DD50.2 pulses /NMI and DD50.1 ("1-DOS/0-SOS") forces page 3 (TR-DOS) of
    // the CURRENT plane over the #0000-#3FFF window - the same mechanism
    // Beta128 uses for its magic button. Neither the #1FFD latch nor the
    // ProfROM plane register is touched: the plane survives the whole session,
    // and the firmware entry chain (#0066 -> #2A56 -> #0807 -> OUT (#1FFD),#12
    // at #0033) pages the service monitor itself. Paused at an instruction
    // boundary, the Z80 takes the NMI before its next fetch, so #0066 already
    // comes from the forced TR-DOS page. The trigger releases on the first
    // CPU read from >= #4000 (Memory::MemoryReadFast). On non-Scorpion models
    // the plain NMI pulse semantics apply
    if (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP)
    {
        if (config.mem_model == MM_PROFSCORP)
        {
            // The service monitor and its #0066 entry chain exist only in ProfROM
            // quadrant 0; every page of planes 1-3 carries the firmware's
            // "wrong plane" stub at #0066 (LD A,6 / OUT (#FE) / XOR A / OUT (#FE) /
            // JR - yellow/black stripes, DI forever). After the 128 boot menu times
            // out the firmware parks in plane 1, so a button press there hung the
            // machine. Select quadrant 0 (GAL state + #7EFD window) BEFORE the entry
            // page is mapped so #0066 is always fetched from plane-0 page 3.
            // Emulator-side decision: the GAL keeps its plane on /NMI (hardware
            // shows the stripes), but this button exists to reach the monitor.
            // See docs/inprogress/2026-09-07-scorpion-zs256-clone/profrom-nmi-gaps-and-findings.md 6.1
            if (ScorpionRomWindow* window = _context->pMemory->GetScorpionRomWindow())
                window->Reset(state);
        }

        state.scorpion.dosTrigger = 1;
        _context->pMemory->UpdateZ80Banks();
    }
    else if (IsProfiModel(config.mem_model))
    {
        // Profi "magic button" (Karabas video.vhd/TOP:1197-1199 `dos_act` set condition,
        // OR-ed with the #3Dxx M1 trap): NMI with DS80=0 raises the same CF_TRDOS latch
        // the M1 trap sets, landing #0000 on the SYS/DOS ROM exactly like a real 3Dxx
        // entry - UpdateZ80Banks() re-derives CF_LEAVEDOSADR/CF_DOSPORTS from CF_TRDOS
        // (Memory::UpdateZ80Banks, MM_PROFI branch), so the session closes on the normal
        // PC>=#4000 fetch with no extra bookkeeping here. DS80=1 (hi-res) blocks it per
        // Karabas - the button has no effect while the palette-writable hi-res mode is
        // active. Deliberately NOT gated on DFFD.4 (WOROM): the existing #3Dxx M1 trap
        // (Memory::UpdateZ80Banks) doesn't gate on it either (design §4.2/Q7 - the
        // Karabas RTL blocks entry there too, but that gate was "not adopted" to match
        // the Unreal/ZXMAK2 consensus); WOROM's RAM-at-#0000 override already makes the
        // ROM invisible regardless of the latch, so a second gate here would be redundant
        // and inconsistent with the M1 trap's own behaviour.
        if (!(state.pDFFD & 0x80))
        {
            state.flags |= CF_TRDOS;
            _context->pMemory->UpdateZ80Banks();
        }
    }

    // Boards that generate the NMI themselves (ZX-Evo: the AVR's PrintScreen
    // NMI waits for the next frame INT) take the request; everyone else gets
    // the plain /NMI pulse
    if (_context->pPortDecoder == nullptr || !_context->pPortDecoder->RequestBoardNmi())
        _core->GetZ80()->RequestNonMaskedInterrupt();

    if (wasRunning)
        Resume(false);
}

void Emulator::Start()
{
    // Skip if not initialized
    if (!_initialized)
    {
        MLOGERROR("Emulator::Start() - not initialized");
        return;
    }

    // StartAsync() raises the running flag on the caller thread before the
    // worker is spawned; the synchronous Start() path relies on this method
    // to raise it. The exchange() serves both roles and additionally reports
    // whether a Stop() already claimed the emulator (CAS true->false) between
    // StartAsync() returning and this point.
    const bool previouslyRunning = _isRunning.exchange(true, std::memory_order_acq_rel);
    if (!previouslyRunning && _stopRequested)
    {
        // Stop() won the start race: it has already claimed the running flag
        // and is joining this thread. Hand the flag back and exit instead of
        // clearing the stop request below - that would erase the only thing
        // MainLoop::Run() checks, leaving the loop unkillable and the joining
        // Stop() blocked in join() forever (seen as Emulator_Test hangs).
        _isRunning.store(false, std::memory_order_release);
        MLOGINFO("Emulator::Start() aborted - Stop() was requested during startup");
        return;
    }

    // Set running state (running flag is already true - see exchange above)
    // NOTE: deliberately NOT clearing _isPaused here. A Pause() issued
    // between StartAsync() returning and the thread reaching this point has
    // already stored the flag under _pauseWaitMutex; clobbering it here would
    // silently ignore that pause (the loop would run at full speed while the
    // caller believes it parked - observed as RunNFrames stepping the Z80
    // concurrently with the still-running emulation thread). StartAsync()
    // clears the flag itself BEFORE spawning this thread, so a fresh start is
    // never affected; honoring a late-landing pause is the correct semantic
    // (the per-instruction and frame-end park checks hold the loop until
    // Resume()).
    _stopRequested = false;

    // A Stop() may have landed anywhere between the exchange above and the
    // stop-request clear. If it did, the running flag is false again - honour
    // it instead of entering MainLoop::Run() with a cleared stop request.
    if (!_isRunning.load(std::memory_order_acquire))
    {
        MLOGINFO("Emulator::Start() aborted - Stop() raced the startup sequence");
        return;
    }

    // Broadcast notification - Emulator started (instance-tagged per GDB TDD §6.3)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    EmulatorStateChangePayload* payload = new EmulatorStateChangePayload(GetId(), StateRun);
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
        // Scheduling is NOT set here: MainLoop::Run owns it (UpdateRealtimeScheduling),
        // elevating only the active instance during cadenced playback. Elevating
        // every thread here left non-active instances and turbo runs real-time
        // forever - MainLoop believed they were not, so never dropped them - and
        // a time-constraint thread running flat out is throttled by the kernel

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

    // Set under _pauseWaitMutex: WaitWhilePaused()'s CV predicate reads this
    // flag while holding the same mutex, so the transition can't be missed.
    {
        std::lock_guard<std::mutex> lock(_pauseWaitMutex);
        _isPaused = true;
    }
    {
        std::lock_guard<std::mutex> lock(_prevStopMutex);
        _lastStop.reason = _pendingPauseCause.hit ? DebugStop::Reason::Breakpoint : DebugStop::Reason::Pause;
        _lastStop.breakpoint = _pendingPauseCause;
    }
    _stepOverHostHold.Release();  // a stepped run that pauses is over (breakpoint, user pause, shutdown)
    // NOTE: Do NOT set _isRunning = false here!
    // The emulator thread is still active, just paused.
    // Setting _isRunning = false would cause Stop() to skip _asyncThread->join(),
    // leading to a crash when RemoveEmulator() destroys memory while thread is still running.
    // MainLoop::Run() will detect this via Emulator::IsPaused() check.

    // Wait until the emulation thread actually parks in MainLoop's pause loop.
    // Setting the flag alone is not enough: MainLoop only checks the pause flag
    // between frames, so the in-flight frame keeps executing Z80 instructions
    // (and writing to memory) after this method would otherwise have returned.
    // Callers (tests, shared-memory migration, snapshot loading) rely on Pause()
    // meaning "no more emulated writes". WaitForPauseConfirmation returns
    // immediately when called from the emulation thread itself (breakpoint
    // handlers pause mid-frame) and may time out legitimately when execution
    // is already blocked inside a frame - proceed anyway in those cases.
    if (_mainloop && _isRunning)
    {
        _mainloop->WaitForPauseConfirmation(500);
    }

    // No more handleFrameEnd calls will arrive until Resume() - a device
    // mid-playback at this exact instant (GS in particular: see
    // SoundManager::onEmulatorPaused) would otherwise keep reporting
    // "active" for as long as the pause lasts, with nothing left to clear
    // it. Unconditional (not gated on broadcast): audio must actually stop
    // regardless of whether this pause is UI-visible.
    if (_context->pSoundManager)
    {
        _context->pSoundManager->onEmulatorPaused();
    }

    // Update state and broadcast only if requested
    // broadcast=false is used for internal operations like shared memory migration
    // where we don't want to trigger UI updates during the brief pause
    if (broadcast)
    {
        SetState(StatePaused);

        // Broadcast notification - Emulator execution paused (instance-tagged per GDB TDD §6.3),
        // with its cause: a breakpoint (OnBreakpointHit set it just before) or a request
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        EmulatorStateChangePayload* payload = new EmulatorStateChangePayload(GetId(), StatePaused);
        if (_pendingPauseCause.hit)
        {
            payload->pauseCause = PauseCause::Breakpoint;
            payload->breakpointId = _pendingPauseCause.breakpointId;
            payload->address = _pendingPauseCause.address;
            payload->hitKind = _pendingPauseCause.kind;
        }
        messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
    }
    _pendingPauseCause = BreakpointStop{};
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
    NoteRunStart();

    // Eagerly invalidate the pause confirmation from the park we are exiting.
    // The run loop clears it only after its parked wait wakes (up to its 20 ms
    // poll); until then a Pause() issued by the same thread (or another
    // control thread) would see a stale "parked" confirmation and return
    // while the loop is already executing the next frame - two Z80 drivers
    // at once (observed as heap corruption in shared collectors, e.g.
    // WD1793Collector::recordCommandStart, and as torn FSMEvent copies).
    //
    // BEFORE the flag flip and the wake-up, never after: invalidating last
    // raced a Pause() from another thread - the woken CPU thread saw the new
    // pause, re-parked and confirmed it, and this late invalidation then
    // erased that fresh confirmation while the thread slept on. Every
    // WaitForPauseConfirmation() caller burned its full timeout against a
    // thread that was in fact parked (500 ms stalls in DeZog history browsing)
    if (_mainloop)
        _mainloop->InvalidatePauseConfirmation();

    // A resumed machine runs paced to real time and is heard: a host output hold whose reason is not in effect
    // now (no direct run on any thread, no TTD replay, no turbo) is a holder that leaked it - drop it, so no
    // pause / step / seek sequence on any surface can leave the speakers silent (SoundManager::HostOutputHold)
    if (_context && _context->pSoundManager)
        _context->pSoundManager->reconcileHostOutputHolds(IsDirectStepping() || _stepOverHostHold.IsHeld(),
                                                          _context->ttdReplayActive, _context->config.turbo_mode);

    {
        // Mirror Pause(): the flag flip must be mutex-protected so the parked
        // CPU thread's CV predicate (WaitWhilePaused) can't miss the transition.
        std::lock_guard<std::mutex> lock(_pauseWaitMutex);
        _isPaused = false;
    }
    _stoppedAtExecBreakpoint = false;  // the run goes on from there
    _resumeCV.notify_all();  // Wake the CPU thread parked mid-frame at a breakpoint
    ResetLineStepAnchor();  // Full-speed run invalidates line-step anchor
    // MainLoop::Run() will detect this via Emulator::IsPaused() check and resume.

    // Note: Don't unconditionally set _isRunning = true here.
    // In synchronous test mode, _isRunning may be false and should stay false.
    // The main RunAsync() path handles _isRunning appropriately.

    // Update state and broadcast only if requested
    if (broadcast)
    {
        SetState(StateResumed);

        // Broadcast notification - Emulator execution resumed (instance-tagged per GDB TDD §6.3)
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        EmulatorStateChangePayload* payload = new EmulatorStateChangePayload(GetId(), StateResumed);
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
    // Fast path: not paused - no locking on the hot per-instruction check path.
    if (!_isPaused)
        return;

    // Parking mid-frame: publish the TTD summary first (see MainLoop::Run's
    // park). The caller executes the machine, so it may read the session
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->OnMachineParking();
    // The picture up to the beam for whoever looks while parked (Screen::CatchesUpOnEvents)
    if (Screen* screen = _context ? _context->pScreen : nullptr; screen && screen->CatchesUpOnEvents())
        screen->UpdateScreen();
    NoteDebugChange();   // a stop (a breakpoint's park inside the frame) the debugger snapshot's seq counts

    std::unique_lock<std::mutex> lock(_pauseWaitMutex);
    // Re-confirm on EVERY park iteration, not just the first. A rapid
    // Resume()->Pause() flip-flop (e.g. adapter resume immediately followed
    // by a control-thread Pause) wakes this thread, the predicate sees the
    // pause flag set again, and the thread re-parks - without re-confirming,
    // the new Pause()'s WaitForPauseConfirmation would burn its full timeout
    // (the frame-end park in MainLoop::Run re-confirms the same way).
    while (_isPaused && !_stopRequested)
    {
        // The CPU thread parks HERE mid-frame (breakpoint/watchpoint handler
        // or the per-instruction pause check in Z80FrameCycle). MainLoop is
        // blocked inside RunFrame() above this call and can never reach its
        // own park/confirm path, so confirm on its behalf - otherwise any
        // WaitForPauseConfirmation() caller burns the full timeout while the
        // CPU is in fact safely parked. Cleared by Resume()/Stop() via
        // InvalidatePauseConfirmation().
        if (_mainloop)
            _mainloop->ConfirmPauseFromCpu();

        // Wake on Resume()/Stop() in microseconds instead of the legacy
        // 20 ms sleep poll (which dominated rapid-debugger-stepping latency:
        // every step cycle paid one poll quantum). Bare wait + loop-head
        // re-check is deliberate: the flag transitions happen under this
        // same mutex, so a wakeup can never be missed.
        _resumeCV.wait(lock);
    }
}

/// @brief Block until the Z80 thread observes the pause flag and parks.
///
/// Emulator::Pause() is asynchronous — it sets _isPaused and returns. The
/// Z80 thread notices at the top of the next frame iteration and signals
/// _isPausedConfirmed via MainLoop's _pauseCV. This method wraps that CV
/// wait so callers that mutate emulator state right after Pause() (e.g.
/// TTD seek/step-back/step-forward via WebAPI) don't race with the
/// in-flight frame loop overwriting their freshly written state.
///
/// Returns true on confirmation, false on timeout. On timeout the caller
/// should proceed anyway — the mutation is still correct, just slightly
/// racy, and the alternative (blocking forever) is worse.
///
/// When the emulator is not running async (tests, synchronous mode), the
/// Z80 thread doesn't exist, _isPausedConfirmed never flips, and this
/// method correctly times out. Callers should not interpret timeout as
/// failure in those configurations.
bool Emulator::WaitForPauseConfirmation(uint32_t timeout_ms)
{
    if (!_mainloop)
        return false;

    return _mainloop->WaitForPauseConfirmation(timeout_ms);
}

void Emulator::Stop()
{
    // Every caller blocks here until the emulation thread has actually been
    // joined, not merely signaled. The old lock-free compare-exchange let a
    // caller that lost the race return immediately while the winner was still
    // inside _asyncThread->join() - IsRunning() already read false at that
    // point, so a concurrent EmulatorManager::RemoveEmulatorInstance() (gated
    // on IsRunning()) could skip its own Stop() call and run straight into
    // Release(), freeing Core's Screen / SoundManager / TimeTravelManager
    // while MainLoop::Run() was still mid-frame on the still-running thread
    // (observed as a SIGSEGV / pointer-authentication failure in
    // MainLoop::OnFrameStart() and SoundManager::handleFrameStart(),
    // 2026-10-02). A thread cannot join itself; guard that instead of
    // deadlocking if this is ever reached from the emulation thread itself.
    if (_asyncThread && _asyncThread->get_id() == std::this_thread::get_id())
    {
        MLOGERROR("Emulator::Stop() called from the emulation thread itself - ignored");
        return;
    }

    std::lock_guard<std::mutex> stopLock(_stopMutex);

    // Use atomic compare-exchange to ensure only ONE thread executes stop logic
    // This prevents double-free of _asyncThread when Stop() is called multiple times
    bool expected = true;
    if (!_isRunning.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
        // Already stopped - whoever won the race already ran the join above
        // under this same mutex, so the thread is genuinely gone by now.
        return;
    }

    // Request emulator to stop
    _stopRequested = true;

    // If emulator was paused - un-pause under the wait mutex and wake the
    // parked CPU thread, allowing mainloop to react and the async thread to
    // be joined below. MainLoop::Run() will detect this via Emulator::IsPaused()
    {
        std::lock_guard<std::mutex> lock(_pauseWaitMutex);
        _isPaused = false;
    }
    _resumeCV.notify_all();
    if (_mainloop)
        _mainloop->InvalidatePauseConfirmation();

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

    // Broadcast notification - Emulator stopped (instance-tagged per GDB TDD §6.3)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    EmulatorStateChangePayload* payload = new EmulatorStateChangePayload(GetId(), StateStopped);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

// endregion

/// region <File operations>

bool Emulator::LoadSnapshot(const std::string& path, const std::string& reportedPath,
                            const snapshot::Options& options)
{
    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
    {
        MLOGWARNING("LoadSnapshot rejected - emulator is being destroyed");
        return false;
    }

    /// region <Info logging>

    MLOGEMPTY();
    MLOGINFO("Loading snapshot from file: '%s'", path.c_str());

    /// endregion </Info logging>

    // Validate path exists
    std::string absolutePath = FileHelper::AbsolutePath(path);
    if (!FileHelper::FileExists(absolutePath))
    {
        MLOGERROR("Snapshot file not found: '%s'", absolutePath.c_str());
        if (_context)
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_FILE_LOADED,
                new FileLoadedPayload(_context->emulatorId, "snapshot", absolutePath, false));
        }
        return false;
    }

    // Validate file extension
    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(absolutePath));

    // An RZX recording opens like a snapshot: its start snapshot loads and the
    // recording plays on this machine (a model switch is RzxLauncher's job)
    if (ext == "rzx")
    {
        const rzx::PlayResult played = PlayRzx(absolutePath);
        if (!played.Ok() && _context)
        {
            MLOGERROR("RZX playback failed: %s", played.message.c_str());
            MessageCenter::DefaultMessageCenter().Post(
                NC_FILE_LOADED, new FileLoadedPayload(_context->emulatorId, "snapshot", absolutePath, false));
        }
        return played.Ok();
    }

    if (ext != "z80" && ext != "sna" && ext != "szx" && ext != "spg" && ext != "nex")
    {
        MLOGERROR("Invalid snapshot format: {}. Expected .z80, .sna, .szx, .spg or .nex", ext.c_str());
        if (_context)
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_FILE_LOADED,
                new FileLoadedPayload(_context->emulatorId, "snapshot", absolutePath, false));
        }
        return false;
    }

    // The file the user opened: a temporary image is reported as the file it came from
    const std::string openedPath = reportedPath.empty() ? absolutePath : reportedPath;
    return LoadSnapshotStaged(
        [&](std::string& error, const snapshot::Options& planned) {
            bool result = false;
            if (ext == "sna")
            {
                /// region <Load SNA snapshot>
                LoaderSNA loaderSna(_context, absolutePath);
                loaderSna.SetOptions(planned);
                result = loaderSna.load();
                _lastSnapshotReport = loaderSna.GetSnapshotReport();

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
                loaderZ80.SetOptions(planned);
                result = loaderZ80.load();
                _lastSnapshotReport = loaderZ80.GetSnapshotReport();

                /// region <Info logging>
                if (result)
                {
                    MLOGINFO("Z80 file loaded successfully, executing it...");
                }

                MLOGEMPTY();
                /// endregion </Info logging>

                /// endregion </Load Z80 snapshot>
            }
            else if (ext == "spg")
            {
                // TS-Conf SDK program (loaderspg.h): the TS-Conf machine only
                LoaderSPG loaderSpg(_context, absolutePath);
                loaderSpg.SetOptions(planned);
                result = loaderSpg.load();
                _lastSnapshotReport = loaderSpg.GetSnapshotReport();
                if (!result)
                    error = loaderSpg.GetError();
            }
            else if (ext == "nex")
            {
                // ZX Spectrum Next program (loadernex.h): the Next machine only
                LoaderNex loaderNex(_context);
                result = loaderNex.LoadFile(absolutePath);
                if (!result)
                    error = loaderNex.Error();
                else
                    _lastSnapshotReport = snapshot::Report();
            }
            else if (ext == "szx")
            {
                /// region <Load SZX snapshot>
                LoaderSZX loaderSzx(_context, absolutePath);
                loaderSzx.SetOptions(planned);
                result = loaderSzx.load();
                _lastSnapshotReport = loaderSzx.GetSnapshotReport();
                if (result)
                    MLOGINFO("SZX file loaded:\n%s", loaderSzx.GetReport().ToText().c_str());
                else
                    MLOGERROR("SZX load failed: %s", loaderSzx.GetError().c_str());
                /// endregion </Load SZX snapshot>
            }
            if (!result && error.empty())
                error = "the " + ext + " loader refused '" + absolutePath + "'";
            return result;
        },
        openedPath, options);
}

bool Emulator::LoadSnapshotData(const std::vector<uint8_t>& data, const std::string& extension,
                                const std::string& reportedPath, const snapshot::Options& options)
{
    if (_state == StateDestroying || _isReleased)
    {
        MLOGWARNING("LoadSnapshotData rejected - emulator is being destroyed");
        return false;
    }
    const std::string ext = StringHelper::ToLower(extension);
    if (ext != "z80" && ext != "sna" && ext != "szx" && ext != "spg")
    {
        MLOGERROR("Invalid snapshot format: %s. Expected z80, sna, szx or spg", ext.c_str());
        if (_context)
            MessageCenter::DefaultMessageCenter().Post(
                NC_FILE_LOADED, new FileLoadedPayload(_context->emulatorId, "snapshot", reportedPath, false));
        return false;
    }
    MLOGINFO("Loading %s snapshot from memory (%zu bytes) for '%s'", ext.c_str(), data.size(), reportedPath.c_str());
    return LoadSnapshotStaged(
        [&](std::string& error, const snapshot::Options& planned) { return ApplySnapshotData(data, ext, error, planned); },
        reportedPath, options);
}

bool Emulator::ApplySnapshotData(const std::vector<uint8_t>& data, const std::string& extension, std::string& error,
                                 const snapshot::Options& options)
{
    const std::string ext = StringHelper::ToLower(extension);
    if (ext == "sna")
    {
        LoaderSNA loader(_context, data, "memory");
        loader.SetOptions(options);
        const bool loaded = loader.load();
        _lastSnapshotReport = loader.GetSnapshotReport();
        if (loaded)
            return true;
        error = "the SNA image did not load";
        return false;
    }
    if (ext == "z80")
    {
        LoaderZ80 loader(_context, data, "memory");
        loader.SetOptions(options);
        const bool loaded = loader.load();
        _lastSnapshotReport = loader.GetSnapshotReport();
        if (loaded)
            return true;
        error = "the Z80 image did not load";
        return false;
    }
    if (ext == "szx" || ext == "zxs")
    {
        szx::Stage stage;
        if (!SzxReader::Parse(data.data(), data.size(), stage, error))
            return false;
        // The same road as a file: the image, the plan step, today's commit
        snapshot::Image image = LoaderSZX::BuildImage(stage, "memory");
        _lastSnapshotReport = snapshot::Report();
        const snapshot::Decision decision = snapshot::Pipeline::Plan(image, _context, options, _lastSnapshotReport);
        if (!decision.Proceeds())
        {
            error = _lastSnapshotReport.reason;
            return false;
        }
        szx::Report report;
        const bool committed = decision.action == snapshot::Decision::Action::Take
                                   ? decision.Commit(image, *_context, _lastSnapshotReport)
                                   : LoaderSZX::CommitImage(_context, image, stage, report, error);
        if (!committed && error.empty())
            error = _lastSnapshotReport.reason;
        LoaderSZX::AppendReport(report, _lastSnapshotReport);
        if (!committed)
        {
            _lastSnapshotReport.Refuse(error);
            return false;
        }
        MLOGINFO("SZX image loaded:\n%s", report.ToText().c_str());
        return true;
    }
    if (ext == "spg")
    {
        LoaderSPG loader(_context, data, "memory");
        loader.SetOptions(options);
        const bool loaded = loader.load();
        _lastSnapshotReport = loader.GetSnapshotReport();
        if (loaded)
            return true;
        error = loader.GetError();
        return false;
    }
    error = "snapshot type '" + ext + "' is not supported (sna, z80, szx, spg)";
    return false;
}

bool Emulator::LoadSnapshotStaged(const std::function<bool(std::string& error, const snapshot::Options& planned)>& load,
                                  const std::string& openedPath, const snapshot::Options& options)
{
    // Another snapshot replaces the machine an RZX playback runs on: it ends
    // first (the playback's own start snapshot loads with the player out)
    if (IsRzxPlaying())
        StopRzx();

    // Pause execution
    bool wasRunning = false;
    if (!IsPaused())
    {
        Pause();
        wasRunning = true;
    }

    // Pause() only sets a flag - the emulation thread finishes its current frame before
    // parking in MainLoop's pause loop. Wait for confirmation so the loader never resets
    // CPU/memory/screen state while a frame is still executing (this race can corrupt the
    // framebuffer when a WebAPI 'pause' is immediately followed by 'snapshot/load').
    // The wait may time out legitimately when paused inside a frame (breakpoint) or in
    // synchronous test mode - proceed anyway in those cases.
    // On iOS/embed hosts the mainloop can block on video present; use a short timeout
    // and proceed anyway - the loader's own locking is sufficient for safety.
    if (_mainloop && IsRunning())
    {
        bool confirmed = _mainloop->WaitForPauseConfirmation(100);
        if (!confirmed)
        {
            MLOGWARNING("LoadSnapshot: pause confirmation timed out, proceeding anyway");
        }
    }

    std::string error;
    bool result = false;
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    // TTD (D42): a snapshot load ENDS the recording session, like a reset (the history of this machine stays browsable; a new
    // session starts only by the `ttdrestart` feature). Nothing is refused - but the session ends only when the load really goes
    // ahead: the plan calls beforeCommit once it has decided to commit, so a refused load leaves the recording running
    snapshot::Options planned = options;
    planned.beforeCommit = [&]() {
        if (options.beforeCommit)
            options.beforeCommit();
        if (ttd)
            ttd->OnLoad(ttd::TTDLoadKind::Snapshot, "snapshot-load");
    };
    auto runLoad = [&]() { result = load(error, planned); };
    runLoad();
    // The loader reset the machine and replaced its state (ports, memory,
    // registers): start the frame again from the loaded state, so devices
    // and the video raster take their frame bases from it
    if (result)
        RestartFrame();
    if (!result && !error.empty())
        MLOGERROR("Snapshot load failed: %s", error.c_str());

    // Store snapshot path on success
    if (result)
        _context->coreState.snapshotFilePath = openedPath;

    // Resume execution
    if (wasRunning)
    {
        Resume();
    }

    if (_context)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_FILE_LOADED,
            new FileLoadedPayload(_context->emulatorId, "snapshot", openedPath, result));
    }

    return result;
}

rzx::RzxSession& Emulator::RzxSessionLocked()
{
    std::lock_guard<std::mutex> sessionLock(_rzxSessionMutex);
    if (!_rzxSession)
        _rzxSession = std::make_unique<rzx::RzxSession>(*this);
    return *_rzxSession;
}

rzx::PlayResult Emulator::PlayRzx(const std::string& path, const rzx::PlayerOptions& options)
{
    if (_state == StateDestroying || _isReleased || !_context)
    {
        rzx::PlayResult result;
        result.error = rzx::PlayError::Refused;
        result.message = "the emulator is being destroyed";
        return result;
    }
    std::lock_guard<std::mutex> lock(_rzxMutex);
    MLOGINFO("RZX playback: '%s'", path.c_str());
    rzx::PlayResult result = RzxSessionLocked().PlayFile(path, options);
    if (!result.Ok())
        MLOGWARNING("RZX playback refused: %s", result.message.c_str());
    return result;
}

rzx::PlayResult Emulator::PlayRzx(std::shared_ptr<const rzx::File> file, const std::string& sourcePath,
                                  const rzx::PlayerOptions& options)
{
    if (_state == StateDestroying || _isReleased || !_context)
    {
        rzx::PlayResult result;
        result.error = rzx::PlayError::Refused;
        result.message = "the emulator is being destroyed";
        return result;
    }
    std::lock_guard<std::mutex> lock(_rzxMutex);
    return RzxSessionLocked().Play(std::move(file), sourcePath, options);
}

bool Emulator::StopRzx()
{
    std::lock_guard<std::mutex> lock(_rzxMutex);
    return _rzxSession && _rzxSession->Stop();
}

bool Emulator::SeekRzx(uint64_t frame, std::string* error)
{
    std::lock_guard<std::mutex> lock(_rzxMutex);
    std::string reason;
    const bool ok = _rzxSession ? _rzxSession->Seek(frame, reason) : (reason = "no RZX recording played", false);
    if (!ok && error)
        *error = reason;
    return ok;
}

bool Emulator::IsRzxPlaying() const
{
    // The player pointer is the playback's own switch: set while the hooks are in
    return _context && _context->rzxPlayer != nullptr;
}

rzx::SessionStatus Emulator::GetRzxStatus() const
{
    // Not _rzxMutex: the status is read while a seek plays on (the GUI polls it)
    std::lock_guard<std::mutex> lock(_rzxSessionMutex);
    return _rzxSession ? _rzxSession->Status() : rzx::SessionStatus{};
}

rzx::RzxSession* Emulator::LoadedRzxSession()
{
    std::lock_guard<std::mutex> lock(_rzxSessionMutex);
    return _rzxSession && _rzxSession->Status().loaded ? _rzxSession.get() : nullptr;
}

namespace
{
/// The engine unless the process is started with UNREAL_TTD_BACKEND=v1 (the
/// previous recorder: re-recording v1's fixture corpus, a comparison, a fallback)
Emulator::TimeTravelBackend InitialTimeTravelBackend()
{
    const char* value = std::getenv("UNREAL_TTD_BACKEND");
    return value && std::string(value) == "v1" ? Emulator::TimeTravelBackend::V1 : Emulator::TimeTravelBackend::Engine;
}
std::atomic<Emulator::TimeTravelBackend> g_defaultTimeTravelBackend{InitialTimeTravelBackend()};
}

void Emulator::SetDefaultTtdRecordingRoot(const std::string& root)
{
    std::lock_guard<std::mutex> lock(g_ttdRecordingRootMutex);
    g_ttdRecordingRoot = root;
}

void Emulator::SetDefaultTimeTravelBackend(TimeTravelBackend backend)
{
    g_defaultTimeTravelBackend.store(backend);
}

Emulator::TimeTravelBackend Emulator::DefaultTimeTravelBackend()
{
    return g_defaultTimeTravelBackend.load();
}

bool Emulator::IsRzxExtension(const std::string& ext)
{
    return StringHelper::ToLower(ext) == "rzx";
}

bool Emulator::InspectSnapshot(const std::string& path, const snapshot::Options& options, StateNode& result,
                               std::string& error)
{
    if (_state == StateDestroying || _isReleased || !_context)
    {
        error = "the emulator is being destroyed";
        return false;
    }
    const std::string absolutePath = FileHelper::AbsolutePath(path);
    if (!FileHelper::FileExists(absolutePath))
    {
        error = "snapshot file not found: '" + absolutePath + "'";
        return false;
    }
    const std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(absolutePath));

    snapshot::Image image;
    if (ext == "sna")
    {
        LoaderSNA loader(_context, absolutePath);
        if (!loader.Stage())
        {
            error = "not a loadable SNA file: '" + absolutePath + "'";
            return false;
        }
        image = loader.GetSnapshotImage();
    }
    else if (ext == "z80")
    {
        LoaderZ80 loader(_context, absolutePath);
        if (!loader.Stage())
        {
            error = "not a loadable Z80 file: '" + absolutePath + "'";
            return false;
        }
        image = loader.GetSnapshotImage();
    }
    else if (ext == "szx")
    {
        if (!LoaderSZX::ReadImage(absolutePath, image, error))
            return false;
    }
    else if (ext == "spg")
    {
        if (!LoaderSPG::ReadSnapshotImage(absolutePath, image, error))
            return false;
    }
    else
    {
        error = "cannot inspect a '" + ext + "' file (sna, z80, szx, spg)";
        return false;
    }

    // The plan asks its questions and writes nothing
    snapshot::Report plan;
    snapshot::Options dryRun = options;
    dryRun.beforeCommit = nullptr;   // a dry plan commits nothing: it announces nothing
    const snapshot::Decision decision = snapshot::Pipeline::Plan(image, _context, dryRun, plan);
    result = StateNode::Object();
    result["path"] = absolutePath;
    result["image"] = snapshot::ToStateNode(image);
    result["plan"] = plan.ToStateNode();
    result["would_load"] = decision.Proceeds();
    result["would_commit"] = plan.commit;
    return true;
}

bool Emulator::SaveSnapshot(const std::string& path)
{
    _lastSaveResult = snapshot::SaveResult{};
    _lastSaveResult.path = path;
    auto refuse = [&](const std::string& reason, const std::string& needs = {}) {
        _lastSaveResult.reason = reason;
        _lastSaveResult.needs = needs;
        _lastSaveResult.text = "cannot save a snapshot: " + reason;
        MLOGERROR("%s", _lastSaveResult.text.c_str());
        return false;
    };

    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
        return refuse("the emulator is being destroyed");

    MLOGEMPTY();
    MLOGINFO("Saving snapshot to file: '%s'", path.c_str());

    // Resolve to absolute path
    const std::string absolutePath = FileHelper::AbsolutePath(path);
    _lastSaveResult.path = absolutePath;

    // Validate file extension
    const std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(absolutePath));
    const std::optional<snapshot::SaveFormat> format = snapshot::SaveFormatFromExtension(ext);
    if (!format)
        return refuse("'." + ext + "' is not a snapshot format this emulator saves: use .sna, .z80 or .szx", "format");
    _lastSaveResult.format = snapshot::ToText(*format);

    // Stop the machine and wait until it has parked: a save reads RAM, the CPU and the latches, which a frame in flight
    // would still be changing
    bool wasRunning = false;
    if (!IsPaused())
    {
        Pause();
        wasRunning = true;
    }
    if (!IsEmulationParked() && !WaitForPauseConfirmation(1000))
    {
        if (wasRunning)
            Resume();
        return refuse("the emulator did not stop within one second, so its state is not stable: nothing was saved", "pause");
    }

    const snapshot::SaveResult saved = snapshot::SaveSnapshotFile(*_context, *format, absolutePath);
    _lastSaveResult = saved;
    if (saved.ok)
    {
        MLOGINFO("%s", saved.text.c_str());
        for (const std::string& warning : saved.warnings)
            MLOGWARNING("%s", warning.c_str());
        // Store snapshot path on success
        _context->coreState.snapshotFilePath = absolutePath;
    }
    else
        MLOGERROR("%s", saved.text.c_str());
    MLOGEMPTY();

    if (wasRunning)
        Resume();

    return saved.ok;
}

snapshot::SaveFormats Emulator::SnapshotSaveFormats()
{
    if (!_context)
    {
        snapshot::SaveFormats none;
        none.view = "no machine";
        for (snapshot::SaveFormat f : {snapshot::SaveFormat::Sna, snapshot::SaveFormat::Z80, snapshot::SaveFormat::Szx})
        {
            snapshot::FormatStatus status;
            status.format = f;
            status.reason = "no machine";
            none.formats.push_back(status);
        }
        return none;
    }
    return snapshot::QuerySaveFormats(*_context);
}

bool Emulator::LoadTape(const std::string& path, std::string* error)
{
    auto fail = [&](const std::string& message) -> bool
    {
        if (error)
            *error = message;
        MLOGERROR("LoadTape: %s", message.c_str());
        return false;
    };

    if (_state == StateDestroying || _isReleased)
        return fail("emulator is being destroyed");
    if (!_context || !_context->pMediaManager || !_context->pMediaManager->HasSlot(TapeSlot::kId))
        return fail("this machine has no tape deck");

    MLOGEMPTY();
    MLOGINFO("Loading tape from '%s'", path.c_str());

    const std::string resolvedPath = FileHelper::AbsolutePath(path);
    const bool folder = FileHelper::IsFolder(resolvedPath);
    if (!folder && !FileHelper::FileExists(resolvedPath))
    {
        MessageCenter::DefaultMessageCenter().Post(NC_FILE_LOADED,
                                                   new FileLoadedPayload(_context->emulatorId, "tape", resolvedPath, false));
        return fail("file not found: '" + path + "'");
    }

    // TTD v1 (P1.6): tape insertion is a session-invalidating event in v1
    // (parent TDD §4.2 + §5 row 3 — tape *insertion/start/stop* commands
    // invalidate; only playback position is checkpointed). Refused while recording.
    // The session ends only once the tape is in (MediaManager::Insert): a file that fails to load keeps it
    if (!RecordingAllows(*this, ttd::TTDGuardedAction::LoadTape, error))
        return false;

    // The format registry probes and loads (every TapeLoaderRegistry format,
    // a folder built into a TZX); the swap happens with the emulator thread
    // parked, at once
    MediaSource source;
    source.type = folder ? MediaSourceType::Folder : MediaSourceType::File;
    source.path = resolvedPath;
    InsertOptions options;
    options.immediate = true;
    options.disposition = Disposition::Discard;  // a tape is never written: nothing to lose
    options.ttdReason = "tape-load";

    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    const MediaResult inserted = _context->pMediaManager->Insert(TapeSlot::kId, source, options);
    if (wasRunning)
        Resume();

    for (const std::string& line : inserted.report)
        MLOGWARNING("LoadTape: %s", line.c_str());

    MessageCenter::DefaultMessageCenter().Post(NC_FILE_LOADED,
                                               new FileLoadedPayload(_context->emulatorId, "tape", resolvedPath, inserted.Ok()));
    if (!inserted.Ok())
        return fail(inserted.message);
    return true;
}

bool Emulator::EjectTape(std::string* error)
{
    auto fail = [&](const std::string& message) -> bool
    {
        if (error)
            *error = message;
        MLOGERROR("EjectTape: %s", message.c_str());
        return false;
    };

    if (_state == StateDestroying || _isReleased)
        return fail("emulator is being destroyed");
    if (!_context || !_context->pMediaManager || !_context->pMediaManager->HasSlot(TapeSlot::kId))
        return fail("this machine has no tape deck");

    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    EjectOptions options;
    options.disposition = Disposition::Discard;
    const MediaResult ejected = _context->pMediaManager->Eject(TapeSlot::kId, options);
    // A tape named by path alone (set before the tape slot existed) goes too
    if (ejected.Ok() && _context->pTape && !_context->coreState.tapeFilePath.empty())
        _context->pTape->DetachImage();
    if (wasRunning)
        Resume();

    if (!ejected.Ok())
        return fail(ejected.message);
    return true;
}

bool Emulator::ParseBlankDiskFormat(const std::string& text, BlankDiskFormat& format)
{
    const std::string name = StringHelper::ToLower(text);
    if (name.empty() || name == "auto")
        format = BlankDiskFormat::Auto;
    else if (name == "unformatted" || name == "raw")
        format = BlankDiskFormat::Unformatted;
    else if (name == "plus3" || name == "+3" || name == "+3dos")
        format = BlankDiskFormat::Plus3;
    else
        return false;
    return true;
}

const char* Emulator::BlankDiskFormatName(BlankDiskFormat format)
{
    switch (format)
    {
        case BlankDiskFormat::Auto: return "auto";
        case BlankDiskFormat::Unformatted: return "unformatted";
        case BlankDiskFormat::Plus3: return "plus3";
    }
    return "?";
}

bool Emulator::CreateBlankDisk(uint8_t drive, BlankDiskFormat format, uint8_t cylinders, uint8_t sides,
                               std::string* error, BlankDiskResult* resolved)
{
    auto fail = [&](const std::string& message) -> bool
    {
        if (error)
            *error = message;
        MLOGERROR("CreateBlankDisk: %s", message.c_str());
        return false;
    };

    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
        return fail("emulator is being destroyed");

    if (drive >= 4)
        return fail("invalid drive index " + std::to_string(static_cast<int>(drive)) + " (valid range: 0-3 / A-D)");

    const std::string slotId = FloppyDriveSlot::IdFor(drive);
    if (!_context || !_context->pMediaManager || !_context->pMediaManager->HasSlot(slotId))
        return fail(std::string("drive ") + static_cast<char>('A' + drive) + " is not present on this machine");

    BlankFloppySpec spec;
    spec.format = BlankDiskFormatName(format);
    spec.cylinders = cylinders;
    spec.sides = sides;
    std::unique_ptr<DiskImage> image;
    const MediaResult built = FloppyFormats::CreateBlank(_context->config.mem_model == MM_PLUS3, spec, image);
    if (!built.Ok())
        return fail(built.message);
    ParseBlankDiskFormat(spec.format, format);
    cylinders = spec.cylinders;
    sides = spec.sides;

    // TTD: a new medium changes what the FDC reads, like a disk swap. Refused while recording.
    if (!RecordingAllows(*this, ttd::TTDGuardedAction::CreateDisk, error))
        return false;

    MediaSource blank;
    blank.type = MediaSourceType::Blank;
    auto medium = std::make_unique<Medium>(blank, AccessMode::Session, BlankDiskFormatName(format), std::move(image));

    // The swap happens with the emulator thread parked, at once (no swap delay)
    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    InsertOptions options;
    options.immediate = true;
    options.disposition = Disposition::Discard;  // a load always replaced the disk, writes and all
    options.ttdReason = "disk-create";
    const MediaResult inserted = _context->pMediaManager->Insert(slotId, std::move(medium), options);
    if (wasRunning)
        Resume();
    if (!inserted.Ok())
        return fail(inserted.message);

    if (resolved)
    {
        resolved->format = format;
        resolved->cylinders = cylinders;
        resolved->sides = sides;
    }

    MLOGINFO("Blank %s disk (%d cylinders, %d sides) inserted into drive %c", BlankDiskFormatName(format),
             int(cylinders), int(sides), static_cast<char>('A' + drive));
    return true;
}

bool Emulator::LoadDisk(const std::string& path, uint8_t drive, std::string* error)
{
    auto fail = [&](const std::string& message) -> bool
    {
        if (error)
            *error = message;
        MLOGERROR("LoadDisk: %s", message.c_str());
        return false;
    };

    // Guard against operations during destruction (thread safety)
    if (_state == StateDestroying || _isReleased)
        return fail("emulator is being destroyed");

    if (drive >= 4)
        return fail("invalid drive index " + std::to_string(static_cast<int>(drive)) + " (valid range: 0-3 / A-D)");

    const std::string slotId = FloppyDriveSlot::IdFor(drive);
    if (!_context || !_context->pMediaManager || !_context->pMediaManager->HasSlot(slotId))
        return fail(std::string("drive ") + static_cast<char>('A' + drive) + " is not present on this machine");

    MLOGEMPTY();
    MLOGINFO("Loading disk image from '%s' into drive %c", path.c_str(), static_cast<char>('A' + drive));

    const std::string resolvedPath = FileHelper::AbsolutePath(path);
    const bool folder = FileHelper::IsFolder(resolvedPath);
    if (!folder && !FileHelper::FileExists(resolvedPath))
    {
        MessageCenter::DefaultMessageCenter().Post(NC_FILE_LOADED,
                                                   new FileLoadedPayload(_context->emulatorId, "disk", resolvedPath, false));
        return fail("file not found: '" + path + "'");
    }

    // TTD v1 (P1.6): disk image swap teleports FDC + media state
    // (parent TDD §4.2 + §12.2). Refused while recording; otherwise the
    // session ends once the disk is in (MediaManager::Insert): a disk that fails to load keeps it.
    if (!RecordingAllows(*this, ttd::TTDGuardedAction::LoadDisk, error))
        return false;

    // The format registry probes and loads; the swap happens with the emulator
    // thread parked, at once (no swap delay: the caller expects the disk in)
    MediaSource source;
    source.type = folder ? MediaSourceType::Folder : MediaSourceType::File;
    source.path = resolvedPath;
    InsertOptions options;
    options.immediate = true;
    options.disposition = Disposition::Discard;  // a load always replaced the disk, writes and all
    options.ttdReason = "disk-load";

    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    const MediaResult inserted = _context->pMediaManager->Insert(slotId, source, options);
    if (wasRunning)
        Resume();

    for (const std::string& line : inserted.report)
        MLOGWARNING("LoadDisk: %s", line.c_str());

    MessageCenter::DefaultMessageCenter().Post(NC_FILE_LOADED,
                                               new FileLoadedPayload(_context->emulatorId, "disk", resolvedPath, inserted.Ok()));
    if (!inserted.Ok())
        return fail(inserted.message);
    return true;
}

bool Emulator::EjectDisk(uint8_t drive, bool force, std::string* error)
{
    auto fail = [&](const std::string& message) -> bool
    {
        if (error)
            *error = message;
        MLOGERROR("EjectDisk: %s", message.c_str());
        return false;
    };

    if (_state == StateDestroying || _isReleased)
        return fail("emulator is being destroyed");
    if (drive >= 4)
        return fail("invalid drive index " + std::to_string(static_cast<int>(drive)) + " (valid range: 0-3 / A-D)");

    const std::string slotId = FloppyDriveSlot::IdFor(drive);
    if (!_context || !_context->pMediaManager || !_context->pMediaManager->HasSlot(slotId))
        return fail(std::string("drive ") + static_cast<char>('A' + drive) + " is not present on this machine");

    // A medium leaving is a media set change: refused while a TTD recording
    // runs (the manager answers "recording"), like a load
    EjectOptions options;
    options.disposition = force ? Disposition::Discard : Disposition::None;

    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    const MediaResult ejected = _context->pMediaManager->Eject(slotId, options);
    if (wasRunning)
        Resume();

    if (!ejected.Ok())
        return fail(ejected.message);
    return true;
}

std::vector<std::string> Emulator::SupportedSnapshotExtensions()
{
    // rzx: an input recording, opened as its start snapshot plus the playback;
    // spg: a TS-Conf program (the TS-Conf machine only); nex: a ZX Spectrum Next program (the Next only)
    return {"sna", "z80", "szx", "spg", "nex", "rzx"};
}

std::vector<std::string> Emulator::SupportedTapeExtensions()
{
    return TapeLoaderRegistry::Instance().SupportedExtensions();
}

bool Emulator::IsTapeExtension(const std::string& ext)
{
    const std::vector<std::string> known = SupportedTapeExtensions();
    return std::find(known.begin(), known.end(), StringHelper::ToLower(ext)) != known.end();
}

std::vector<std::string> Emulator::SupportedDiskExtensions()
{
    return FloppyFormats::Extensions();
}

Emulator::DiskSaveResult Emulator::SaveDisk(uint8_t drive, const std::string& path, bool allowRetarget)
{
    DiskSaveResult result;

    if (drive >= 4 || !_context || !_context->pMediaManager)
    {
        result.reason = "Invalid drive";
        return result;
    }

    const std::string slotId = FloppyDriveSlot::IdFor(drive);
    MediaManager& manager = *_context->pMediaManager;
    const std::optional<SlotInfo> info = manager.Info(slotId);
    if (!info)
    {
        result.reason = "Invalid drive";
        return result;
    }
    if (!info->present)
    {
        result.reason = "No disk image in the drive";
        return result;
    }

    // The format writers walk the image with the emulator thread parked
    const bool wasRunning = !IsPaused();
    if (wasRunning)
        Pause();
    SaveOptions options;
    options.path = path;
    options.allowRetarget = allowRetarget;
    SaveOutcome outcome;
    const MediaResult saved = manager.Save(slotId, options, &outcome);
    if (wasRunning)
        Resume();

    if (!saved.Ok())
    {
        result.reason = saved.message;
        MLOGERROR("SaveDisk: %s", result.reason.c_str());
        return result;
    }

    result.saved = true;
    result.retargeted = outcome.retargeted;
    result.savedPath = outcome.savedPath;
    result.reason = outcome.note;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    if (outcome.retargeted)
    {
        MLOGWARNING("SaveDisk: %s - saved losslessly as '%s'", outcome.note.c_str(), outcome.savedPath.c_str());
        messageCenter.Post(NC_FDD_DISK_SAVE_RETARGETED, new FDDDiskPayload(GetId(), drive, outcome.savedPath, outcome.note), true);
    }
    messageCenter.Post(NC_FDD_DISK_WRITTEN, new FDDDiskPayload(GetId(), drive, outcome.savedPath), true);
    return result;
}

/// endregion </File operations>

// region Controlled flow

void Emulator::FinishStepOver(uint16_t breakpointId, bool restoreFeatures)
{
    if (_breakpointManager)
    {
        _breakpointManager->RemoveBreakpointByID(breakpointId);
        for (uint16_t bpId : _stepOverDeactivatedBps)
        {
            _breakpointManager->ActivateBreakpoint(bpId);
        }
    }
    _stepOverDeactivatedBps.clear();
    if (restoreFeatures && _featureManager)
    {
        _featureManager->setFeature(Features::kDebugMode, _stepOverRestoreDebugMode);
        _featureManager->setFeature(Features::kBreakpoints, _stepOverRestoreBreakpoints);
    }
    _stepOverHostHold.Release();  // the stepped run is over: audio follows the machine's pace again
}

void Emulator::CancelPendingStepOver()
{
    // Only relevant in debug mode — skip entirely during normal emulation
    if (!_featureManager || !_featureManager->isEnabled(Features::kDebugMode))
        return;

    if (_pendingStepOverBpId.load() != 0)
    {
        // The run to the temporary breakpoint may still be under way: park the machine before touching the
        // breakpoints its thread reads (the step that cancels pauses it anyway)
        if (IsRunning() && !IsPaused())
        {
            Pause();
            WaitForPauseConfirmation();
        }
        if (const uint16_t pending = _pendingStepOverBpId.exchange(0))
        {
            MLOGDEBUG("Emulator::CancelPendingStepOver - Removing orphaned step-over breakpoint ID %d", pending);
            FinishStepOver(pending, false);
        }
    }
    _stepOverHostHold.Release();
}

bool Emulator::OnBreakpointHit(uint16_t breakpointId, uint16_t address, BreakpointHitKind kind)
{
    // Temporary breakpoints of step over / step out, and hidden ones, are not shown to the user
    bool hidden = false;
    if (_context && _context->pDebugManager)
    {
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
        const BreakpointDescriptor* bp = brk.GetBreakpointById(breakpointId);
        hidden = bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints");
    }

    // The step over's temporary breakpoint ends its run here, on the emulation thread that reads the breakpoints:
    // removed before the machine parks, so nothing else changes the breakpoint set under a running CPU
    uint16_t expected = breakpointId;
    const bool stepOverDone = breakpointId != 0 && _pendingStepOverBpId.compare_exchange_strong(expected, 0);
    if (stepOverDone)
    {
        MLOGDEBUG("Emulator::OnBreakpointHit - step over done at breakpoint ID %d", breakpointId);
        FinishStepOver(breakpointId, true);
        hidden = true;
    }

    if (IsDirectStepping())
    {
        // Stepping on from the execution breakpoint the emulator is stopped at: its instruction runs
        if (kind == BreakpointHitKind::Execute && _passExecBreakpointArmed && address == _passExecBreakpointPc)
            return false;

        if (!_directStop.hit)
            _directStop = BreakpointStop{true, breakpointId, address, kind};
        if (kind == BreakpointHitKind::Execute)
        {
            _stoppedAtExecBreakpoint = true;
            _stoppedAtExecPc = address;
        }
        MessageCenter::DefaultMessageCenter().Post(NC_EXECUTION_BREAKPOINT,
                                                   new BreakpointTriggeredPayload(GetId(), breakpointId, address, hidden));
        // Execution: stop before the instruction. Memory and ports: the instruction completes, the run ends
        return kind == BreakpointHitKind::Execute;
    }

    // The emulator's own run: pause (with the cause), notify, park this thread until Resume()
    if (kind == BreakpointHitKind::Execute)
    {
        _stoppedAtExecBreakpoint = true;
        _stoppedAtExecPc = address;
    }
    _pendingPauseCause = BreakpointStop{true, breakpointId, address, kind};
    Pause();
    _pendingPauseCause = BreakpointStop{};  // Pause() did not consume it when it returned early (not running)

    MessageCenter::DefaultMessageCenter().Post(NC_EXECUTION_BREAKPOINT,
                                               new BreakpointTriggeredPayload(GetId(), breakpointId, address, hidden));
    if (stepOverDone)
        MessageCenter::DefaultMessageCenter().Post(NC_EXECUTION_CPU_STEP);   // the debugger's "step completed"
    WaitWhilePaused();
    return false;
}

Z80::StepResult Emulator::ExecuteStep(bool skipBreakpoints, bool* frameCompleted)
{
    Z80& z80 = *_core->GetZ80();

    // Leaving the instruction the emulator is stopped at (an execution breakpoint that fires
    // again at this step sets it again); only the run's first instruction may pass that breakpoint
    _stoppedAtExecBreakpoint = false;
    Z80::StepResult result = z80.StepInstruction(skipBreakpoints);
    _passExecBreakpointArmed = false;
    if (BreakpointManager* brk = GetBreakpointManager())
        brk->DisarmExecPass();

    const bool completed = z80.IsFrameComplete();
    if (completed)
    {
        // Frame boundary - the exact sequence MainLoop::RunFrame uses
        _core->FinishCPUFrame();
        if (_mainloop)
            _mainloop->CompleteFrame();
        else
            z80.BeginFrame();
    }

    if (frameCompleted)
        *frameCompleted = completed;

    return result;
}

void Emulator::RestartFrame()
{
    if (_mainloop)
        _mainloop->RestartFrame();
    else if (_core)
        _core->GetZ80()->BeginFrame();
}

void Emulator::RunSingleCPUCycle(bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;
    ResetLineStepAnchor();

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    ExecuteStep(skipBreakpoints);

    // Notify the debugger that a step has been performed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

Emulator::DirectStepScope::DirectStepScope(Emulator& emulator) : _emulator(emulator)
{
    const bool firstIn = _emulator._directStepDepth.fetch_add(1, std::memory_order_acq_rel) == 0;
    if (_emulator._context && _emulator._context->pSoundManager)
        _hostHold = SoundManager::HostOutputHold(_emulator._context->pSoundManager,
                                                 SoundManager::HostHoldReason::DirectRun);
    if (firstIn)
    {
        _emulator.NoteRunStart();
        // A new direct run: no breakpoint stop yet; its first instruction may leave the execution
        // breakpoint the emulator is stopped at (nothing executed since it stopped there)
        _emulator._directStop = BreakpointStop{};
        const uint16_t pc = _emulator._core ? _emulator._core->GetZ80()->pc : 0;
        _emulator._passExecBreakpointArmed = _emulator._stoppedAtExecBreakpoint && pc == _emulator._stoppedAtExecPc;
        _emulator._passExecBreakpointPc = pc;
        // The manager skips that one check entirely: no hit and no count (a hit-count policy sees the
        // breakpoint once, not again on stepping on from it)
        if (BreakpointManager* brk = _emulator.GetBreakpointManager())
        {
            if (_emulator._passExecBreakpointArmed)
                brk->ArmExecPass(pc);
            else
                brk->DisarmExecPass();
        }
    }
}

Emulator::DirectStepScope::~DirectStepScope()
{
    _hostHold.Release();
    if (_emulator._directStepDepth.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        {
            std::lock_guard<std::mutex> lock(_emulator._prevStopMutex);
            _emulator._lastStop.reason = _emulator._directStop.hit ? DebugStop::Reason::Breakpoint : DebugStop::Reason::Step;
            _emulator._lastStop.breakpoint = _emulator._directStop;
        }
        // What the run left mid-frame on a screen that catches up on its own events (Screen::CatchesUpOnEvents)
        if (Screen* screen = _emulator._context ? _emulator._context->pScreen : nullptr; screen && screen->CatchesUpOnEvents())
            screen->UpdateScreen();
        _emulator.NoteDebugChange();   // the direct run stopped
        // The GUI's one refresh, now it may read; the payload says whether a breakpoint ended the run
        auto* payload = new CpuStepPayload(_emulator.GetId());
        payload->stopped = _emulator._directStop.hit;
        payload->breakpointId = _emulator._directStop.breakpointId;
        payload->address = _emulator._directStop.address;
        payload->hitKind = _emulator._directStop.kind;
        MessageCenter::DefaultMessageCenter().Post(NC_EXECUTION_CPU_STEP, payload, true);
    }
}

unsigned Emulator::RunNCPUCycles(unsigned cycles, bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;
    ResetLineStepAnchor();

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    unsigned executed = 0;
    for (unsigned i = 0; i < cycles && !RunHalted(); i++)
    {
        ExecuteStep(skipBreakpoints);
        // An execution breakpoint stops the run before its instruction
        if (!(_directStop.hit && _directStop.kind == BreakpointHitKind::Execute))
            executed++;
    }

    // Notify the debugger that a step has been performed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
    return executed;
}

void Emulator::RunFrame(bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    ResetLineStepAnchor();

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates

        // Wait for the emulation thread to park - otherwise we would step the Z80
        // concurrently with the frame MainLoop is still finishing
        if (_mainloop)
            _mainloop->WaitForPauseConfirmation(250);
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();

    // Use persistent target to prevent cumulative drift.
    // First frame step records the target; subsequent calls reuse it.
    if (!_hasFrameStepTarget)
    {
        _frameStepTargetPos = z80.t % config.frame;
        _hasFrameStepTarget = true;
    }
    unsigned targetPos = _frameStepTargetPos;

    // Phase 1: Run until frame counter increments (crosses one frame boundary)
    uint64_t startFrame = _context->emulatorState.frame_counter;

    while (_context->emulatorState.frame_counter == startFrame && !RunHalted())
    {
        ExecuteStep(skipBreakpoints);
    }

    // Phase 2: We're now at the start of a new frame (z80.t is small).
    // Single-step until we reach or pass targetPos.
    if (targetPos > 0 && !RunHalted())
    {
        while (z80.t < targetPos && !RunHalted())
        {
            bool frameCompleted = false;
            ExecuteStep(skipBreakpoints, &frameCompleted);
            if (frameCompleted)
                break;  // Safety: don't cross another frame boundary
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
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    // Direct stepping must never race the emulation thread. Even when the
    // caller already paused, the park can still be in flight: Pause()
    // proceeds after its confirmation timeout (e.g. the thread was still
    // inside its startup sequence when the pause landed), and a mid-flight
    // frame here would step the Z80 concurrently with RunNFrames' own
    // stepping - two drivers corrupting shared state. Waiting is free when
    // already parked (the predicate is satisfied immediately) and fast-paths
    // when called from the emulation thread itself.
    if (IsRunning() && _mainloop)
    {
        _mainloop->WaitForPauseConfirmation(500);
    }

    Z80& z80 = *_core->GetZ80();

    // Run exactly N frames worth of t-states. The budget is in T-states of the frame length now in force and
    // follows a hardware clock switch (TStateRunBudget), so "N frames" stays N frames of emulated time
    TStateRunBudget budget = TStateRunBudget::Frames(z80._frameLimit, frames);

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    // A halted CPU's idle cycles may run in one go, as one per step would: those that start inside the budget
    Z80::IdleSkipScope idleSkip(z80, 0);

    while (!budget.Reached() && !RunHalted())
    {
        const uint32_t prevT = z80.t;
        const uint32_t limitBefore = z80._frameLimit;
        const uint64_t budgetEnd = static_cast<uint64_t>(prevT) + budget.Remaining();
        z80.idleSkipLimit = budgetEnd < UINT32_MAX ? static_cast<uint32_t>(budgetEnd) : UINT32_MAX;

        bool frameCompleted = false;
        ExecuteStep(skipBreakpoints, &frameCompleted);

        budget.Step(prevT, z80.t, limitBefore, z80._frameLimit, frameCompleted);

        // Notify after each frame so debugger/visualizers can update
        if (frameCompleted)
            messageCenter.Post(NC_EXECUTION_CPU_STEP);
    }
}

void Emulator::RunTStates(uint64_t tStates, bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;
    ResetLineStepAnchor();

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    // See RunNFrames: direct stepping must never race a mid-flight emulation
    // thread, even when the caller paused earlier and the park is still in
    // flight.
    if (IsRunning() && _mainloop)
    {
        _mainloop->WaitForPauseConfirmation(500);
    }

    Z80& z80 = *_core->GetZ80();

    // 64-bit: the target is counted from the current frame start and a long run spans many frames
    uint64_t targetT = static_cast<uint64_t>(z80.t) + tStates;

    // A halted CPU's idle cycles may run in one go, as one per step would: those that start before the target
    Z80::IdleSkipScope idleSkip(z80, 0);

    while (z80.t < targetT && !RunHalted())
    {
        const uint32_t limitBefore = z80._frameLimit;
        z80.idleSkipLimit = targetT < UINT32_MAX ? static_cast<uint32_t>(targetT) : UINT32_MAX;

        bool frameCompleted = false;
        ExecuteStep(skipBreakpoints, &frameCompleted);

        if (frameCompleted)
        {
            if (targetT >= limitBefore)
            {
                targetT -= limitBefore;
            }
            else
            {
                // targetT was within the frame that just ended — the
                // overshooting instruction already executed past it. Stop
                // to avoid running an entire extra frame (which would
                // inflate frame_counter and corrupt TTD probe records).
                break;
            }
        }
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilScanline(unsigned targetLine, bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();

    unsigned targetT = targetLine * config.t_line;

    // If we've already passed this scanline in the current frame, complete the frame first
    if (z80.t >= targetT)
    {
        bool frameCompleted = false;
        while (!frameCompleted && !RunHalted())
        {
            ExecuteStep(skipBreakpoints, &frameCompleted);
        }
    }

    // Now run to the target scanline
    while (z80.t < targetT && !RunHalted())
    {
        ExecuteStep(skipBreakpoints);
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
    messageCenter.Post(NC_SCANLINE_BOUNDARY);
}

void Emulator::RunNScanlines(unsigned count, bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();

    const unsigned t_line = config.t_line;

    // Anti-drift strategy: persistent anchor offset within scanline.
    // On the first line-step, capture our horizontal position within the scanline.
    // All subsequent steps target the same offset, so even though Z80 opcodes
    // overshoot by 0–19 t-states, the TARGET never drifts — only the single-step
    // landing jitters by at most one opcode width.
    if (_lineStepAnchorOffset < 0)
    {
        _lineStepAnchorOffset = static_cast<int>(z80.t % t_line);
    }

    const unsigned anchor = static_cast<unsigned>(_lineStepAnchorOffset);

    // Calculate the ideal target (where we'd land with infinite resolution); 64-bit, a long run spans many frames
    const uint64_t idealT = static_cast<uint64_t>(z80.t) + static_cast<uint64_t>(count) * t_line;

    // Find the two anchor-aligned positions that bracket idealT
    uint64_t anchorBefore = (idealT / t_line) * t_line + anchor;
    if (anchorBefore > idealT)
        anchorBefore -= t_line;
    const uint64_t anchorAfter = anchorBefore + t_line;

    // Pick whichever anchor point is closest to the ideal target
    uint64_t targetT;
    if (idealT - anchorBefore <= anchorAfter - idealT)
        targetT = anchorBefore;
    else
        targetT = anchorAfter;

    // Safety: target must advance past current position
    if (targetT <= z80.t)
        targetT += t_line;

    while (z80.t < targetT && !RunHalted())
    {
        const uint32_t limitBefore = z80._frameLimit;

        bool frameCompleted = false;
        ExecuteStep(skipBreakpoints, &frameCompleted);

        // targetT wraps with frame counter adjustment
        if (frameCompleted && targetT >= limitBefore)
        {
            targetT -= limitBefore;
        }
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
    messageCenter.Post(NC_SCANLINE_BOUNDARY);
}

void Emulator::ResetLineStepAnchor()
{
    _lineStepAnchorOffset = -1;
}

void Emulator::RunUntilNextScreenPixel(bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    Z80& z80 = *_core->GetZ80();

    // Use the screen's precomputed raster state for the exact first paper pixel position
    // This accounts for VSync, VBlank, top border, HSync, HBlank, and left border timing
    unsigned paperStartT = _context->pScreen->GetPaperStartTstate();

    if (z80.t >= paperStartT)
    {
        // After paper start or in paper area — complete frame first
        bool frameCompleted = false;
        while (!frameCompleted && !RunHalted())
        {
            ExecuteStep(skipBreakpoints, &frameCompleted);
        }
    }

    // Run to paper start (in the new frame when the one above was completed)
    while (z80.t < paperStartT && !RunHalted())
    {
        ExecuteStep(skipBreakpoints);
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilInterrupt(bool skipBreakpoints)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    Z80& z80 = *_core->GetZ80();

    // Safety limit: max 2 frames worth of t-states to prevent infinite loops
    TStateRunBudget budget = TStateRunBudget::Frames(z80._frameLimit, 2);

    while (!RunHalted())
    {
        const uint32_t prevT = z80.t;
        const uint32_t limitBefore = z80._frameLimit;

        bool frameCompleted = false;
        const Z80::StepResult step = ExecuteStep(skipBreakpoints, &frameCompleted);

        // INT accepted — CPU is now at ISR entry point
        if (step.intAccepted)
            break;

        budget.Step(prevT, z80.t, limitBefore, z80._frameLimit, frameCompleted);

        // Safety: don't run more than 2 frames
        if (budget.Reached())
        {
            MLOGWARNING("Emulator::RunUntilInterrupt - Safety limit reached (%llu t-states), no interrupt accepted",
                        static_cast<unsigned long long>(budget.Elapsed()));
            break;
        }
    }

    // Notify debugger
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_EXECUTION_CPU_STEP);
}

void Emulator::RunUntilCondition(std::function<bool(const Z80State&)> predicate, uint64_t maxTStates,
                                 bool notifyDebugger)
{
    DirectStepScope directStep(*this);
    CancelPendingStepOver();
    _hasFrameStepTarget = false;

    // Pause emulator if running — step commands always leave emulator paused
    if (IsRunning() && !IsPaused())
    {
        Pause();  // Broadcast pause so debugger UI updates
    }

    Z80& z80 = *_core->GetZ80();

    // maxTStates 0 = no limit
    TStateRunBudget budget(maxTStates);

    while (!_stopRequested)
    {
        const uint32_t prevT = z80.t;
        const uint32_t limitBefore = z80._frameLimit;

        bool frameCompleted = false;
        ExecuteStep(true, &frameCompleted);  // Skip breakpoints for condition-based execution

        budget.Step(prevT, z80.t, limitBefore, z80._frameLimit, frameCompleted);

        // Check predicate
        if (predicate(z80))
        {
            break;
        }

        // Enforce safety limit if specified
        if (maxTStates > 0 && budget.Reached())
        {
            MLOGWARNING("Emulator::RunUntilCondition - Safety limit reached (%llu t-states)",
                        static_cast<unsigned long long>(budget.Elapsed()));
            break;
        }
    }

    // Notify debugger
    if (notifyDebugger)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_EXECUTION_CPU_STEP);
    }
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

    // An earlier step over still under way (its breakpoint not reached): end it before this one starts
    CancelPendingStepOver();

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
    bpDesc->hidden = true;
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
    // Save original feature states
    _stepOverRestoreDebugMode = fm->isEnabled(Features::kDebugMode);
    _stepOverRestoreBreakpoints = fm->isEnabled(Features::kBreakpoints);
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kBreakpoints, true);

    // Tracking state: OnBreakpointHit ends the step on the emulation thread when this breakpoint fires
    _stepOverDeactivatedBps = deactivatedBreakpoints;
    _pendingStepOverBpId = stepOverBreakpointID;

    // Resume execution - returns immediately (non-blocking). The run to the temporary breakpoint is a step: its
    // sound stays off (host output hold) until it stops; taken before Resume so its reconcile sees the hold's run
    MLOGDEBUG("Emulator::StepOver() - Resuming execution to hit breakpoint at 0x%04X", nextInstructionAddress);
    if (_context && _context->pSoundManager)
        _stepOverHostHold = SoundManager::HostOutputHold(_context->pSoundManager, SoundManager::HostHoldReason::DirectRun);
    Resume();
    
    // No blocking wait - UI stays responsive
}

/// region <Step out helpers>

/// RET-family opcode detection: RET, RET cc, RETN/RETI (incl. undocumented ED aliases)
static bool IsReturnInstruction(uint16_t address, Memory* memory)
{
    if (!memory)
    {
        return false;
    }

    uint8_t opcode = memory->DirectReadFromZ80Memory(address);
    if (opcode == 0xC9) // RET
    {
        return true;
    }
    if ((opcode & 0xC7) == 0xC0) // RET cc (C0 C8 D0 D8 E0 E8 F0 F8)
    {
        return true;
    }
    if (opcode == 0xED)
    {
        switch (memory->DirectReadFromZ80Memory(address + 1))
        {
            case 0x45: // RETN
            case 0x55: // RETI (undocumented alias)
            case 0x5D: // RETI
            case 0x65: // RETN (undocumented alias)
            case 0x6D: // RETI (undocumented alias)
            case 0x75: // RETN (undocumented alias)
            case 0x7D: // RETI (undocumented alias)
                return true;
            default:
                return false;
        }
    }
    return false;
}

/// endregion </Step out helpers>

void Emulator::StepOut()
{
    // Early exit if not initialized
    if (!_initialized || !_core)
    {
        MLOGERROR("Emulator::StepOut() - not initialized");
        return;
    }

    Z80State* z80 = GetZ80State();
    Memory* memory = GetMemory();
    if (!z80 || !memory)
    {
        MLOGERROR("Emulator::StepOut() - required components not available");
        return;
    }

    // Step out = SP-tracking walk: run until a RET-family instruction sits at
    // or above the entry stack level, then execute it to land in the caller.
    // All steps skip breakpoints so debugger breakpoints inside the callee
    // cannot trap the walk.
    const uint16_t entrySP = z80->sp;

    // Fast path: standing on a return instruction — execute it directly
    if (IsReturnInstruction(z80->pc, memory))
    {
        RunSingleCPUCycle(true);
        return;
    }

    // Generous ceiling: deep call chains still return within ~2 s of emulated time
    const uint64_t safetyLimit = static_cast<uint64_t>(_context->config.frame) * 100;

    RunUntilCondition(
        [entrySP, memory](const Z80State& state) {
            return state.sp >= entrySP && IsReturnInstruction(state.pc, memory);
        },
        safetyLimit);

    // If the walk parked on the return instruction — execute it to land in the caller.
    // On a safety-limit stop the emulator stays paused at the current position.
    Z80State* current = GetZ80State();
    if (current && current->sp >= entrySP && IsReturnInstruction(current->pc, memory))
    {
        RunSingleCPUCycle(true);
    }
}

/// Load ROM file (up to 64 banks to ROM area)
/// \param path File path to ROM file
bool Emulator::LoadROM(std::string path)
{
    // Checked before pausing: a refusal leaves the machine as it was
    if (!RecordingAllows(*this, ttd::TTDGuardedAction::LoadRom))
        return false;

    Pause();

    // TTD v1 (P1.6): ROM reload changes immutable code/data backing every
    // checkpoint relies on (parent TDD §4.2 — "ROM reload" is listed
    // explicitly as a session invalidator).
    if (_context && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->OnConfigurationChange(ttd::TTDConfigChangeKind::RomReload, "rom-reload");

    ROM& rom = *_core->GetROM();

    bool result = rom.LoadROM(path, _memory->ROMBase(), MAX_ROM_PAGES);

    return result;
}

void Emulator::DebugOn()
{
    // Switch to slow but instrumented memory interface
    _isDebug = true;
    _z80->isDebugMode = true;
    _core->SelectMemoryInterface();
}

void Emulator::DebugOff()
{
    // Switch to fast memory interface
    _isDebug = false;
    _z80->isDebugMode = false;
    _core->SelectMemoryInterface();
}

// region <Video mode>

bool Emulator::SetOverscanMode(bool enable)
{
    if (!_context || !_context->pScreen)
        return false;

    Screen* screen = _context->pScreen;
    VideoModeEnum currentMode = screen->GetVideoMode();

    // Only Pentagon supports overscan
    if (currentMode != M_PENTAGON128K && currentMode != M_P384)
    {
        return false;  // ZX48/128 have no overscan
    }

    VideoModeEnum newMode = enable ? M_P384 : M_PENTAGON128K;

    if (newMode != currentMode)
    {
        // Pause emulation while changing video mode to avoid framebuffer access during reallocation
        bool wasRunning = IsRunning() && !IsPaused();
        if (wasRunning)
        {
            Pause(false);
        }

        // Record the user's intent FIRST: InitRaster re-detects the video mode
        // from config/ports every frame and would revert a bare SetVideoMode
        // back to the model's base mode on the next frame
        screen->SetOverscanForced(enable);
        screen->SetVideoMode(newMode);

        if (wasRunning)
        {
            Resume(false);
        }

        return true;
    }
    return false;
}

bool Emulator::IsOverscanMode() const
{
    if (!_context || !_context->pScreen)
        return false;

    return _context->pScreen->IsOverscanMode();
}

void Emulator::SetDisplayViewport(const DisplayViewport& viewport)
{
    if (_context && _context->pScreen)
    {
        _context->pScreen->SetDisplayViewport(viewport);
    }
}

const DisplayViewport& Emulator::GetDisplayViewport() const
{
    static DisplayViewport defaultViewport;
    if (!_context || !_context->pScreen)
        return defaultViewport;

    return _context->pScreen->GetDisplayViewport();
}

// endregion </Video mode>

Z80State* Emulator::GetZ80State()
{
    return static_cast<Z80State*>(_z80);
}

// endregion

// region Status

// Identity and state methods

unreal::UUID Emulator::GetUUID() const
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

bool Emulator::IsEmulationParked()
{
    return !_isRunning || !_mainloop || (_isPaused && _mainloop->IsPauseConfirmed());
}

void Emulator::NoteRunStart()
{
    // Only from a parked machine (a step called on a running one pauses it first): the copy must not race the
    // emulation thread
    if (_z80 && IsEmulationParked())
    {
        std::lock_guard<std::mutex> lock(_prevStopMutex);
        _prevStopState = *static_cast<Z80State*>(_z80);
        _hasPrevStop = true;
    }
    NoteDebugChange();
}

bool Emulator::RunAtFrameBoundary(const std::function<void()>& work, uint32_t timeoutMs)
{
    return _mainloop && _isRunning && _mainloop->RunAtFrameBoundary(work, timeoutMs);
}

void Emulator::NoteDebugChange()
{
    _debugSeq.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(_debugSeqMutex);  // a waiter between its check and its wait sees it
    }
    _debugSeqChanged.notify_all();
}

uint64_t Emulator::WaitDebugChange(uint64_t since, uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> lock(_debugSeqMutex);
    _debugSeqChanged.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() { return DebugSeq() != since; });
    return DebugSeq();
}

Emulator::CoherentMoment Emulator::RunAtCoherentMoment(const std::function<void()>& work, uint32_t timeoutMs)
{
    if (RunWhileParked(work))
        return CoherentMoment::Paused;
    // Not started and nobody steps it: nothing can change it
    if (!IsRunning() && !IsDirectStepping())
    {
        work();
        return CoherentMoment::Stopped;
    }
    // Running: the emulation thread takes it between two frames, without a pause
    if (IsRunning() && !IsPaused() && RunAtFrameBoundary(work, timeoutMs))
        return CoherentMoment::Frame;
    // It paused meanwhile, or a direct step runs on another thread: wait for the park, briefly
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (RunWhileParked(work))
            return CoherentMoment::Paused;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return CoherentMoment::Busy;
}

const char* Emulator::CoherentMomentName(CoherentMoment moment)
{
    switch (moment)
    {
        case CoherentMoment::Paused:
            return "paused";
        case CoherentMoment::Stopped:
            return "stopped";
        case CoherentMoment::Frame:
            return "frame";
        case CoherentMoment::Busy:
            break;
    }
    return "busy";
}

Emulator::DebugStop Emulator::LastStop() const
{
    std::lock_guard<std::mutex> lock(_prevStopMutex);
    return _lastStop;
}

bool Emulator::PreviousStopRegisters(Z80State& out) const
{
    std::lock_guard<std::mutex> lock(_prevStopMutex);
    if (!_hasPrevStop)
        return false;
    out = _prevStopState;
    return true;
}

bool Emulator::RunWhileParked(const std::function<void()>& work)
{
    std::lock_guard<std::mutex> lock(_pauseWaitMutex);
    if (!_isRunning || !_mainloop || !_isPaused || !_mainloop->IsPauseConfirmed() || IsDirectStepping())
        return false;
    work();
    return true;
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

void Emulator::ApplySymbolBundles()
{
    const char* setting = std::getenv("UNREAL_SYMBOL_BUNDLES");
    if (setting && std::string(setting) == "0")
        return;
    if (!_context || !_context->pDebugManager || !_context->pMemory || !_core || !_core->GetROM())
        return;
    LabelManager* labels = _context->pDebugManager->GetLabelManager();
    if (!labels)
        return;
    ROM& rom = *_core->GetROM();
    std::vector<std::string> pages;
    for (uint8_t i = 0; i < rom.GetROMBanksLoaded(); i++)
        pages.push_back(rom.CalculateSignature(_context->pMemory->ROMPageHostAddress(i), PAGE_SIZE));
    std::string folder = FileHelper::PathCombine(FileHelper::GetResourcesPath(), "symbols");
    if (!FileHelper::FileExists(FileHelper::PathCombine(folder, "manifest.json")))
        folder = FileHelper::PathCombine(FileHelper::GetExecutablePath(), "symbols");
    labels->ApplyBundles(folder, pages);
}
