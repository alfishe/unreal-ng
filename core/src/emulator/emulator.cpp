#include <loaders/snapshot/loader_z80.h>
#include <loaders/disk/loader_trd.h>
#include "stdafx.h"

#include "emulator.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/systemhelper.h"
#include "common/threadhelper.h"
#include "debugger/debugmanager.h"
#include "emulator/memory/memory.h"
#include "loaders/snapshot/loader_sna.h"

/// region <Constructors / Destructors>

Emulator::Emulator() : Emulator(LoggerLevel::LogTrace)
{
}

Emulator::Emulator(LoggerLevel level)
{
    LOGDEBUG("Emulator::Emulator(LoggerLevel level)");

    _loggerLevel = level;

    // Create and initialize emulator context. ModuleLogger will be initialized as well.
    _context = new EmulatorContext(_loggerLevel);
    if (_context != nullptr)
    {
        _logger = _context->pModuleLogger;
        _context->pEmulator = this;

        MLOGDEBUG("Emulator::Init - context created");
    }
    else
    {
        LOGERROR("Emulator::Emulator() - context creation failed");
        throw std::logic_error("Emulator::Emulator() - context creation failed");
    }
}

Emulator::~Emulator()
{
	LOGDEBUG("Emulator::~Emulator()");
}

/// endregion </Constructors / Destructors>

/// region <Initialization>

bool Emulator::Init()
{
    bool result = false;

    // Lock mutex until exiting current scope
    std::lock_guard<std::mutex> lock(_mutexInitialization);

    if (_initialized)
    {
        LOGERROR("Emulator::Init() - already initialized");
        throw std::logic_error("Emulator::Init() - already initialized");

        return result;
    }

	// Ensure that MessageCenter instance is up and running
	MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter(true);

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
			LOGDEBUG("Emulator::Init - CPU system core created");

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
        DiskImage* diskImage = _context->coreState.diskImages[i];

        if (diskImage)
        {
            delete diskImage;
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

#if defined(__x86__) || defined(__x86_64__)
    char cpuString[49];
	cpuString[0] = '\0';

	SystemHelper::GetCPUString(cpuString);
	LOGINFO("CPU ID: %s", cpuString);

	unsigned cpuver = SystemHelper::GetCPUID(1, 0);	    // Read Highest Function Parameter and ManufacturerID
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
	_core->Reset();

	_stopRequested = false;
	_isPaused = false;
	_isRunning = true;

//	ModuleLogger& logger = *_context->pModuleLogger;
//	logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_Z80, PlatformZ80SubmodulesEnum::SUBMODULE_Z80_M1);
//  logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_IO, PlatformIOSubmodulesEnum::SUBMODULE_IO_IN);
//  logger.TurnOffLoggingForModule(PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_MAINLOOP);

	//std::string snapshotPath = "/Volumes/SSDData/LocalGit/unreal/tests/loaders/sna/multifix.sna";
	//LoadSnapshot(snapshotPath);

    // Broadcast notification - Emulator started
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StateRun);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);

    // Pass execution to main loop
    // It will return only after stop request
	_mainloop->Run(_stopRequested);
}

void Emulator::StartAsync()
{
    // Stop existing thread
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

    // Broadcast notification - Emulator paused
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    SimpleNumberPayload* payload = new SimpleNumberPayload(StatePaused);
    messageCenter.Post(NC_EMULATOR_STATE_CHANGE, payload);
}

void Emulator::Resume()
{
    if (!_isPaused)
        return;

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
        //Resume();
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
            DiskImage* diskImage = loaderTrd.getImage();
            _context->coreState.diskImages[0] = diskImage;
        }
    }

    return result;
}

/// endregion </File operations>


//region Controlled flow

void Emulator::RunSingleCPUCycle(bool skipBreakpoints)
{
    const CONFIG& config = _context->config;
    Z80& z80 = *_core->GetZ80();
    Memory& memory = *_context->pMemory;

	// TODO: synchronize with all timings within frame and I/O

    z80.Z80Step(skipBreakpoints);
    z80.UpdateScreen();

    // New frame to be started
    if (z80.t >= config.frame)
    {
        _core->AdjustFrameCounters();
    }

#ifdef _DEBUG
    if (false)
    {
        // Use static buffer to save on strings reallocation. CPU cycle is most frequently called functionality.
        static char buffer[1024];
        z80.DumpZ80State(buffer, sizeof(buffer) / sizeof(buffer[0]));
        MLOGINFO(buffer);

        uint8_t *pcPhysicalAddress = memory.MapZ80AddressToPhysicalAddress(z80.m1_pc);
        uint8_t commandLen = 0;
        std::string pc = StringHelper::ToHexWithPrefix(z80.m1_pc, "");
        std::string command = _context->pDebugManager->GetDisassembler()->disassembleSingleCommand(pcPhysicalAddress, 6,
                                                                                                   &commandLen);
        std::string hex = DumpHelper::HexDumpBuffer(pcPhysicalAddress, commandLen);

        MLOGINFO("$%s: %s   %s", pc.c_str(), hex.c_str(), command.c_str());
    }
#endif
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

//endregion
