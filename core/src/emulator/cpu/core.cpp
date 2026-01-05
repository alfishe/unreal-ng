#include "stdafx.h"

#include "common/modulelogger.h"

#include "core.h"
#include <algorithm>
#include <array>
#include <cassert>
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/fdc/vg93.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/videocontroller.h"
#include "emulator/video/zx/screenzx.h"



// Instantiate Core tables as static (only one instance per process)
CPUTables Core::_cpuTables;

/// region <Constructors / Destructors>

Core::Core(EmulatorContext* context)
{
    _context = context;
    _state = &_context->emulatorState;
    _config = &_context->config;
    _logger = _context->pModuleLogger;
}

Core::~Core()
{
    Release();

    _context = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <Initialization>
bool Core::Init()
{
    bool result = false;

    // Instantiation sequence
    // Step 1       - Memory()

    // Step N - 1   - Z80()
    // Step N       - PortDecoder()

    // Register itself in context
    _context->pCore = this;

    /// region <Frequency>

    uint32_t baseFrequency = 3'500'000; // Make 3.5MHz by default

    // See: https://k1.spdns.de/Develop/Projects/zxsp-osx/Info/nocash%20Sinclair%20ZX%20Specs.html
    // See: https://worldofspectrum.org/faq/reference/128kreference.htm
    switch (_context->config.mem_model)
    {
        [[maybe_unused]] MM_SPECTRUM48:
            baseFrequency = 3'500'000;
            break;
        [[maybe_unused]] MM_SPECTRUM128:
            baseFrequency = 3'546'900;
            break;
        default:
            baseFrequency = 3'500'000;
            break;
    }

    _state->base_z80_frequency = baseFrequency;
    _state->current_z80_frequency = baseFrequency;
    _state->current_z80_frequency_multiplier = 1;
    _state->next_z80_frequency_multiplier = 1;  // Initialize queued multiplier

    // Initialize speed multiplier from configuration
    if (_config->speed_multiplier > 0 && _config->speed_multiplier <= 16)
    {
        _state->current_z80_frequency_multiplier = _config->speed_multiplier;
        _state->next_z80_frequency_multiplier = _config->speed_multiplier;
        _state->current_z80_frequency = baseFrequency * _config->speed_multiplier;
    }

    /// endregion </Frequency>

    /// region <Memory>

    // Create memory subsystem (allocates all RAM/ROM regions)
    _memory = new Memory(_context);
    if (_memory)
    {
        _context->pMemory = _memory;

        result = true;
    }

    /// endregion </Memory>

    /// region <ROM>

    if (result)
    {
        result = false;

        // Instantiate ROM implementation
        _rom = new ROM(_context);
        if (_rom)
        {
            result = true;
        }
    }

    /// endregion </ROM>

    /// region <Keyboard>

    if (result)
    {
        result = false;

        // Instantiate Keyboard implementation
        _keyboard = new Keyboard(_context);
        if (_keyboard)
        {
            _context->pKeyboard = _keyboard;

            result = true;
        }
    }

    /// endregion </Keyboard>

    /// region <Tape>

    if (result)
    {
        result = false;

        // Instantiate Tape interface implementation
        _tape = new Tape(_context);
        if (_tape)
        {
            _context->pTape = _tape;

            result = true;
        }
    }

    /// endregion </Tape>

    /// region <BetaDisk128 Interface>

    if (result)
    {
        result = false;

        // Instantiate BDI interface implementation
        _betaDisk = new WD1793(_context);
        if (_betaDisk)
        {
            _context->pBetaDisk = _betaDisk;

            result = true;
        }
    }

    /// endregion </BetaDisk128 Interface>

    /// region <Sound manager>

    if (result)
    {
        result = false;

        // Instantiate sound manager
        _sound = new SoundManager(_context);

        if (_sound)
        {
            _context->pSoundManager = _sound;

            result = true;
        }
    }

    /// endregion </Sound manager>

    /// region <Recording manager>

    if (result)
    {
        result = false;

        // Instantiate recording manager
        _recordingManager = new RecordingManager(_context);

        if (_recordingManager)
        {
            _context->pRecordingManager = _recordingManager;
            _recordingManager->Init();

            result = true;
        }
    }

    /// endregion </Recording manager>

    /// region <HDD>

    if (result)
    {
        result = false;

        // Create HDD controller
        _hdd = new HDD(_context);
        if (_hdd)
        {
            result = true;
        }
    }

    /// endregion </HDD>

    /// region <Video controller>

    if (result)
    {
        result = false;

        // Create Video controller
        VideoModeEnum mode = M_ZX48; // Make ZX the default video mode on start
        _screen = VideoController::GetScreenForMode(mode, _context);
        if (_screen)
        {
            _context->pScreen = _screen;

            result = true;
        }
    }

    /// endregion </Video controller>

    /// region <Z80>

    if (result)
    {
        result = false;

        // Create main Core core instance (Z80)
        _z80 = new Z80(_context);
        if (_z80)
        {
            UseFastMemoryInterface();    // Use fast memory interface by default

            result = true;
        }
    }

    /// endregion </Z80>

    /// region <Ports decoder>

    if (result)
    {
        result = false;

        // Instantiate ports decoder
        // As ports decoder should know and control all peripherals - instantiate it as last step
        MEM_MODEL model = _context->config.mem_model;
        _ports = new Ports(_context);
        if (_ports)
        {
            _portDecoder = PortDecoder::GetPortDecoderForModel(model, _context);
            if (_portDecoder)
            {
                _context->pPortDecoder = _portDecoder;

                result = true;
            }
            else
            {
                LOGERROR("Core::Core - Unable to create port decoder for model %d", model);
                throw std::logic_error("No port decoder");
            }
        }
    }

    // endregion </Ports decoder>

    /// region <Activate IO devices>
    if (_sound)
    {
        _sound->attachToPorts();
    }

    if (_betaDisk)
    {
        _betaDisk->attachToPorts();
    }

    /// endregion </Activate IO devices>

    // Release all allocated object in case of at least single failure
    if (!result)
    {
        Release();
    }

    return result;
}

void Core::Release()
{
    // Unregister itself from context
    _context->pCore = nullptr;

    _context->pPortDecoder = nullptr;
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }

    if (_ports != nullptr)
    {
        delete _ports;
        _ports = nullptr;
    }

    _context->pSoundManager = nullptr;
    if (_sound != nullptr)
    {
        // Detach sound chips from the PortDecoder
        _sound->detachFromPorts();

        delete _sound;
        _sound = nullptr;
    }

    _context->pRecordingManager = nullptr;
    if (_recordingManager != nullptr)
    {
        delete _recordingManager;
        _recordingManager = nullptr;
    }

    _context->pScreen = nullptr;
    if (_screen != nullptr)
    {
        delete _screen;
        _screen = nullptr;
    }

    if (_hdd != nullptr)
    {
        delete _hdd;
        _hdd = nullptr;
    }

    _context->pBetaDisk = nullptr;
    if (_betaDisk != nullptr)
    {
        _betaDisk->detachFromPorts();

        delete _betaDisk;
        _betaDisk = nullptr;
    }

    _context->pTape = nullptr;
    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    _context->pKeyboard = nullptr;
    if (_keyboard != nullptr)
    {
        delete _keyboard;
        _keyboard = nullptr;
    }

    if (_rom != nullptr)
    {
        delete _rom;
        _rom = nullptr;
    }

    _context->pMemory = nullptr;
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

    if (_z80 != nullptr)
    {
        delete _z80;
        _z80 = nullptr;
    }
}
/// endregion </Initialization>

// Configuration methods
void Core::UseFastMemoryInterface()
{
    _z80->MemIf = _z80->FastMemIf;
}

void Core::UseDebugMemoryInterface()
{
    _z80->MemIf = _z80->DbgMemIf;
}

void Core::Reset()
{
	MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
	int topicID = messageCenter.RegisterTopic(NC_SYSTEM_RESET);
	messageCenter.Post(topicID, new SimpleTextPayload("Core reset started"));

	// Set default ROM according to config settings (can be overriden for advanced platforms like TS-Conf and ATM)
	_mode = static_cast<ROMModeEnum>(_config->reset_rom);

	// Reset main Z80 Core and all peripherals
	_z80->Reset();					// Main Z80
	_memory->Reset();               // Memory
	_keyboard->Reset();             // Keyboard
	_sound->reset();				// All sound devices (AY(s), COVOX, MoonSound, GS) and sound subsystem
	_screen->Reset();               // Reset all video subsystem
    _tape->reset();                 // Reset tape loader state
    _betaDisk->reset();             // BetaDisk floppy controller
	_hdd->Reset();					// Reset IDE controller
    _portDecoder->reset();          // Reset peripheral port decoder

	// Input controllers reset
	//input.atm51.reset();
	// input.buffer.Enable(false);

	// Reset counters
    _state->frame_counter = 0;

	messageCenter.Post(topicID, new SimpleTextPayload("Core reset finished"));
}

void Core::Pause()
{
    _pauseRequested = true;

    _z80->Pause();
}

void Core::Resume()
{
    _pauseRequested = false;

    _z80->Resume();
}

//
// Set main Z80 Core clock speed
// Multplier from 3.5MHz
//
void Core::SetCPUClockSpeed(uint8_t multiplier)
{
	if (multiplier == 0)
	{
		LOGERROR("Core::SetCPUClockSpeed - Z80 clock frequency multiplier cannot be 0");
		assert(false);
	}

    _z80->rate = (256 / multiplier);
}

uint32_t Core::GetBaseCPUFrequency()
{
    return _state->base_z80_frequency;
}

uint32_t Core::GetCPUFrequency()
{
    return _state->current_z80_frequency;
}

uint16_t Core::GetCPUFrequencyMultiplier()
{
    return _state->current_z80_frequency_multiplier;
};

//
// Set speed multiplier for emulation (1x, 2x, 4x, 8x, 16x)
// This scales the number of t-states executed per frame
//
void Core::SetSpeedMultiplier(uint8_t multiplier)
{
    // Validate multiplier is one of allowed values (use static list)
    static const std::array<uint8_t, 5> allowedMultipliers = { 1, 2, 4, 8, 16 };
    if (std::find(allowedMultipliers.begin(), allowedMultipliers.end(), multiplier) == allowedMultipliers.end())
    {
        LOGERROR("Core::SetSpeedMultiplier - Speed multiplier must be one of {1,2,4,8,16} (got %d)", multiplier);
        assert(false);
        return;
    }

    // Queue the multiplier change - it will be applied at the start of the next frame
    // This prevents mid-frame inconsistencies in timing calculations
    _state->next_z80_frequency_multiplier = multiplier;

    MLOGINFO("Core::SetSpeedMultiplier - Speed multiplier queued to %dx (will apply at next frame)", multiplier);
}

uint8_t Core::GetSpeedMultiplier() const
{
    return _state->current_z80_frequency_multiplier;
}

//
// Enable turbo/max speed mode - runs emulation as fast as possible
// withAudio: if true, continue generating audio samples (at increased pitch)
//
void Core::EnableTurboMode(bool withAudio)
{
    _context->config.turbo_mode = true;
    _context->config.turbo_mode_audio = withAudio;
    
    // Always mute audible output in turbo mode to avoid chipmunk sounds
    // Audio generation may still occur if withAudio=true (for recording)
    if (_context->pSoundManager)
    {
        _context->pSoundManager->mute();
    }
    
    MLOGINFO("Core::EnableTurboMode - Turbo mode enabled (audio generation: %s, audible: MUTED)", 
             withAudio ? "ON" : "OFF");
}

//
// Disable turbo mode and return to normal speed
//
void Core::DisableTurboMode()
{
    _context->config.turbo_mode = false;
    
    // Restore audible output
    if (_context->pSoundManager)
    {
        _context->pSoundManager->unmute();
    }
    
    MLOGINFO("Core::DisableTurboMode - Turbo mode disabled, audio unmuted");
}

//
// Check if turbo mode is currently active
//
bool Core::IsTurboMode() const
{
    return _context->config.turbo_mode;
}

void Core::CPUFrameCycle()
{
	// Execute Z80 cycle
	if (_z80->isDebugMode)
	{
		// Use advanced (but slow) memory access interface when Debugger is on
		UseDebugMemoryInterface();
		_z80->Z80FrameCycle();
	}
	else
	{
		// Use fast memory access when no Debugger used
		UseFastMemoryInterface();
		_z80->Z80FrameCycle();
	}

    uint32_t t = _context->pCore->GetZ80()->t;
    MLOGINFO("tState counter after the frame: %d", t);

    AdjustFrameCounters();

#ifdef ENABLE_MEMORY_MAPPING
    // Sync memory content to disk memory-mapped files
    _memory->SyncToDisk();
#endif
}

/// Perform corrections after each frame rendered
void Core::AdjustFrameCounters()
{
    /// region <Input parameters validation>
    // Calculate scaled frame limit based on speed multiplier
    uint32_t scaledFrame = _config->frame * _state->current_z80_frequency_multiplier;
    
    if (_z80->t < scaledFrame)
        return;
    /// endregion </Input parameters validation>

    // Update frame stats
    _state->frame_counter++;

    // Re-adjust Core frame t-state counter and interrupt position
    _z80->t -= scaledFrame;
    _z80->eipos -= scaledFrame;
}

void Core::UpdateScreen()
{
	GetZ80()->OnCPUStep();
}