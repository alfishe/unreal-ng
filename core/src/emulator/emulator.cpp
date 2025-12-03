#include <loaders/snapshot/loader_z80.h>
#include <loaders/disk/loader_trd.h>
#include <loaders/disk/loader_scl.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <functional>

#include "emulator.h"

#include "common/filehelper.h"
#include "common/systemhelper.h"
#include "common/threadhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "base/featuremanager.h"
#include "3rdparty/message-center/messagecenter.h"
#include "loaders/snapshot/loader_sna.h"
#include "emulator/io/fdc/wd1793.h"

/// region <Constructors / Destructors>

Emulator::Emulator(LoggerLevel level) : Emulator("", level)
{
}

Emulator::Emulator(const std::string& symbolicId, LoggerLevel level)
{
    _emulatorId = GenerateUUID();
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

        MLOGDEBUG("Emulator::Emulator(symbolicId='%s', level=%d) - Instance created with UUID: %s", 
                 symbolicId.c_str(), level, _emulatorId.c_str());
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

        //std::string rompath = rom.GetROMFilename();
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
    ;	// Tape
    ;	// HDD/CD
    ;	// ZiFi
    ;	// GS / NGS

    // Create and initialize Debugger and related components
    ;	// Debugger

    // Create and initialize Scripting support
    ;	// Scripting host (Python or Lua?)


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

    ReleaseNoGuard();
}

void Emulator::ReleaseNoGuard()
{
    // Guard against null context (shouldn't happen, but be safe)
    if (!_context)
        return;
    
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

	[[maybe_unused]] unsigned cpuver = SystemHelper::GetCPUID(1, 0);	// Read Highest Function Parameter and ManufacturerID
	unsigned features = SystemHelper::GetCPUID(1, 1);	// Read Processor Info and Feature Bits
	host.mmx = (features >> 23) & 1;
	host.sse = (features >> 25) & 1;
	host.sse2 = (features >> 26) & 1;
	MLOGINFO("MMX:%s, SSE:%s, SSE2:%s", host.mmx ? "YES" : "NO", host.sse ? "YES" : "NO", host.sse2 ? "YES" : "NO");

	host.cpufq = SystemHelper::GetCPUFrequency();
#elif defined(__arm__) || defined (__aarch64__)
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
    _context->coreState.baseFreqMultiplier = speed;
}

///region <Integration interfaces>

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
    _context->pAudioManagerObj = obj;
    _context->pAudioCallback = callback;
}

///endregion </Integration interfaces>

//region Regular workflow

void Emulator::Reset()
{
	_core->Reset();
}

void Emulator::Start()
{
    // Skip if already running
    if (_isRunning)
    {
        MLOGWARNING("Emulator::Start() - already running");
        return;
    }

    // Skip if not initialized
    if (!_initialized)
    {
        MLOGERROR("Emulator::Start() - not initialized");
        return;
    }

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

    // Start new thread with name 'emulator' and execute Start() method from it
    _asyncThread = new std::thread([this]()
    {
        ThreadHelper::setThreadName("emulator");

        this->Start();
    });
}

void Emulator::Pause()
{
    if (_isPaused)
        return;

    _isPaused = true;
    _isRunning = false;

    _mainloop->Pause();
    
    // Update state
    SetState(StatePaused);
    
    // Broadcast notification - Emulator execution paused
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StatePaused);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

void Emulator::Resume()
{
    if (!_isPaused)
    {
        return;
    }

	_stopRequested = false;
	_isPaused = false;

	_mainloop->Resume();

	_isRunning = true;

    // Broadcast notification - Emulator execution resumed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StateResumed);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

void Emulator::Stop()
{
    if (!_isRunning)
        return;

    // Request emulator to stop
    _stopRequested = true;

    // If emulator was paused - un-pause, allowing mainloop to react
    if (_isPaused)
    {
        _mainloop->Resume();
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

	// Set emulator state
    _isRunning = false;
	_stopRequested = false;
	_isPaused = false;

    // Broadcast notification - Emulator execution resumed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StateStopped);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

//endregion

/// region <File operations>

bool Emulator::LoadSnapshot(const std::string &path)
{
    bool result = false;

    /// region <Info logging>

    MLOGEMPTY();
    MLOGINFO("Loading snapshot from file: '%s'", path.c_str());

    /// endregion </Info logging>

    // Pause execution
    bool wasRunning = false;
    if (!IsPaused())
    {
        Pause();
        wasRunning = true;
    }

    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(path));
    if (ext == "sna")
    {
        /// region <Load SNA snapshot>
        LoaderSNA loaderSna(_context, path);
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
        LoaderZ80 loaderZ80(_context, path);
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

    // Resume execution
    if (wasRunning)
    {
        // TODO: uncomment for the release
        Resume();
    }

    return result;
}

bool Emulator::LoadTape(const std::string &path)
{
    bool result = false;

    MLOGEMPTY();
    MLOGINFO("Inserting tape from file: '%s'", path.c_str());

    _context->coreState.tapeFilePath = path;

    return result;
}

bool Emulator::LoadDisk(const std::string &path)
{
    bool result = false;

    MLOGEMPTY();
    MLOGINFO("Inserting drive A: disk image from file: '%s'", path.c_str());

    _context->coreState.diskFilePaths[0] = path;

    std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(path));
    if (ext == "trd")
    {
        LoaderTRD loaderTrd(_context, path);
        if (loaderTrd.loadImage())
        {
            // FIXME: use active drive, not fixed A:

            /// region <Free memory from previous disk image>
            _context->pBetaDisk->ejectDisk();
            _context->coreState.diskDrives[0]->ejectDisk();

            DiskImage* diskImage = _context->coreState.diskImages[0];

            if (diskImage != nullptr)
            {
                delete diskImage;
            }
            /// endregion </ree memory from previous disk image>

            /// region <Load new disk image and mount it>
            diskImage = loaderTrd.getImage();
            _context->coreState.diskImages[0] = diskImage;

            _context->coreState.diskDrives[0]->insertDisk(diskImage);

            /// endregion </Load new disk image and mount it>
        }
    }

    if (ext == "scl")
    {
        LoaderSCL loader(_context, path);
        if (loader.loadImage())
        {
            // FIXME: use active drive, not fixed A:

            /// region <Free memory from previous disk image>
            _context->pBetaDisk->ejectDisk();
            _context->coreState.diskDrives[0]->ejectDisk();

            DiskImage* diskImage = _context->coreState.diskImages[0];

            if (diskImage != nullptr)
            {
                delete diskImage;
            }
            /// endregion </ree memory from previous disk image>

            /// region <Load new disk image and mount it>
            diskImage = loader.getImage();
            _context->coreState.diskImages[0] = diskImage;

            _context->coreState.diskDrives[0]->insertDisk(diskImage);
            /// endregion </Load new disk image and mount it>
        }
    }

    return result;
}

/// endregion </File operations>


//region Controlled flow

void Emulator::RunSingleCPUCycle(bool skipBreakpoints)
{
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

void Emulator::RunUntilInterrupt()
{
    throw std::logic_error("Not implemented");
}

void Emulator::RunUntilCondition()
{
    throw std::logic_error("Not implemented");
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

    MLOGDEBUG("Emulator::StepOver() - instruction requires step-over, next instruction at 0x%04X", nextInstructionAddress);

    // Deactivate breakpoints within the called function's scope
    std::vector<std::pair<uint16_t, uint16_t>> exclusionRanges = disassembler->getStepOverExclusionRanges(currentPC, memory, 5);
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
        for (uint16_t id : deactivatedBreakpoints) bpManager->ActivateBreakpoint(id);
        RunSingleCPUCycle(true);
        return;
    }
    bpManager->SetBreakpointGroup(stepOverBreakpointID, "TemporaryBreakpoints");

    // Save original feature states
    bool originalDebugMode = fm->isEnabled(Features::kDebugMode);
    bool originalBreakpoints = fm->isEnabled(Features::kBreakpoints);
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kBreakpoints, true);

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::function<void(int, Message*)> breakpoint_handler;

    breakpoint_handler = 
        [this, bpManager, stepOverBreakpointID, deactivatedBreakpoints, fm, originalDebugMode, originalBreakpoints](int /*id*/, Message* message) mutable
        {
            if (!message || !message->obj) return;

            auto payload = static_cast<SimpleNumberPayload*>(message->obj);
            uint16_t triggeredBreakpointID = static_cast<uint16_t>(payload->_payloadNumber);

            if (triggeredBreakpointID == stepOverBreakpointID)
            {
                MLOGDEBUG("Emulator::StepOver() - lambda cleanup started for breakpoint ID %d", stepOverBreakpointID);
                bpManager->RemoveBreakpointByID(stepOverBreakpointID);
                for (uint16_t deactivatedId : deactivatedBreakpoints)
                {
                    bpManager->ActivateBreakpoint(deactivatedId);
                }
                fm->setFeature(Features::kDebugMode, originalDebugMode);
                fm->setFeature(Features::kBreakpoints, originalBreakpoints);
                
                // Signal the StepOver finalizer that processing is done and we cal wrap up
                _stepOverSyncEvent.Signal();
                MLOGDEBUG("Emulator::StepOver() - lambda finished.");
            }
        };

    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, breakpoint_handler);

    // Continue execution, then wait for the lambda to signal completion
    MLOGDEBUG("Emulator::StepOver() - Resuming execution to hit temporary breakpoint at 0x%04X", nextInstructionAddress);
    Resume();

    // We're waiting until breakpoint_handler lambda finishes
    _stepOverSyncEvent.Wait();

    // Now it's safe to remove the observer
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, breakpoint_handler);
    MLOGDEBUG("Emulator::StepOver() - Operation complete, observer removed.");
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

//endregion

//region Status

// Identity and state methods
const std::string& Emulator::GetId() const
{
    return _emulatorId;
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
    ss << std::setw(2) << std::setfill('0') << hours << ":"
       << std::setw(2) << std::setfill('0') << minutes << ":"
       << std::setw(2) << std::setfill('0') << seconds;
    return ss.str();
}

// ID management
std::string Emulator::GetUUID() const
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
       << "Created at: " << std::ctime(&createdTime)
       << "Last activity: " << std::ctime(&lastActivityTime)
       << "Uptime: " << GetUptimeString() << "\n"
       << "State: " << getEmulatorStateName(_state);
    
    // ctime adds a newline, so we need to remove the last one
    std::string result = ss.str();
    if (!result.empty() && result[result.length()-1] == '\n') {
        result.erase(result.length()-1);
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
    result += StringHelper::Format("  CPU cycles: %s\n", StringHelper::FormatWithThousandsDelimiter(z80.cycle_count).c_str());
    result += StringHelper::Format("  Memory:\n    %s\n", memory.DumpMemoryBankInfo().c_str());
    result += StringHelper::Format("  CPU: %s", cpuState.c_str());

    return result;
}

std::string Emulator::GenerateUUID()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++)
    {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++)
    {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++)
    {
        ss << dis(gen);
    };
    return ss.str();
}

//endregion
