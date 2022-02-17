#include "stdafx.h"

#include "common/modulelogger.h"

#include "cpu.h"
#include <algorithm>
#include <cassert>
#include "emulator/ports/portdecoder.h"
#include <emulator/video/videocontroller.h>
#include "emulator/video/zx/screenzx.h"


// Instantiate CPU tables as static (only one instance per process)
CPUTables CPU::_cpuTables;

/// region <Constructors / Destructors>

CPU::CPU(EmulatorContext* context)
{
	_context = context;
    _state = &_context->emulatorState;
    _config = &_context->config;
    _logger = _context->pModuleLogger;
}

CPU::~CPU()
{
    Release();

    _context = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <Initialization>
bool CPU::Init()
{
    bool result = false;

    // Instantiation sequence
    // Step 1       - Memory()

    // Step N - 1   - Z80()
    // Step N       - PortDecoder()

    // Register itself in context
    _context->pCPU = this;

    /// region <Frequency>

    uint32_t baseFrequency = 3'500'000; // Make 3.5MHz by default

    // See: https://k1.spdns.de/Develop/Projects/zxsp-osx/Info/nocash%20Sinclair%20ZX%20Specs.html
    // See: https://worldofspectrum.org/faq/reference/128kreference.htm
    switch (_context->config.mem_model)
    {
        MM_SPECTRUM48:
            baseFrequency = 3'500'000;
            break;
        MM_SPECTRUM128:
            baseFrequency = 3'546'900;
            break;
        default:
            baseFrequency = 3'500'000;
            break;
    }

    _state->base_z80_frequency = baseFrequency;
    _state->current_z80_frequency = baseFrequency;
    _state->current_z80_frequency_multiplier = 1;

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

    /// region <Sound manager>

    if (result)
    {
        result = false;

        // Instantiate sound manager
        _sound = new Sound(_context);

        if (_sound)
        {
            result = true;
        }
    }

    /// endregion </Sound manager>

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

        // Create main CPU core instance (Z80)
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
                LOGERROR("CPU::CPU - Unable to create port decoder for model %d", model);
                throw std::logic_error("No port decoder");
            }
        }
    }

    // endregion </Ports decoder>

    // Release all allocated object in case of at least single failure
    if (!result)
    {
        Release();
    }

    return result;
}

void CPU::Release()
{
    // Unegister itself from context
    _context->pCPU = nullptr;

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

    if (_sound != nullptr)
    {
        delete _sound;
        _sound = nullptr;
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
void CPU::UseFastMemoryInterface()
{
    _z80->MemIf = _z80->FastMemIf;
}

void CPU::UseDebugMemoryInterface()
{
    _z80->MemIf = _z80->DbgMemIf;
}

void CPU::Reset()
{
	MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
	int topicID = messageCenter.RegisterTopic("CPU_RESET");
	messageCenter.Post(topicID, new SimpleTextPayload("CPU reset started"));

	// Set default ROM according to config settings (can be overriden for advanced platforms like TS-Conf and ATM)
	_mode = static_cast<ROMModeEnum>(_config->reset_rom);

	// Reset main Z80 CPU and all peripherals
	_z80->Reset();					// Main Z80
	_memory->Reset();               // Memory
	_keyboard->Reset();             // Keyboard
	_sound->Reset();				// All sound devices (AY(s), COVOX, MoonSound, GS) and sound subsystem
	_screen->Reset();               // Reset all video subsystem
	//reset_tape();					// Reset tape loader state
	_hdd->Reset();					// Reset IDE controller
    _portDecoder->Reset();          // Reset peripheral port decoder

	// Input controllers reset
	//input.atm51.reset();
	// input.buffer.Enable(false);

	// Reset counters
    _state->frame_counter = 0;
	_state->t_states = 0;

	messageCenter.Post(topicID, new SimpleTextPayload("CPU reset finished"));
}

void CPU::Pause()
{
    _pauseRequested = true;

    _z80->Pause();
}

void CPU::Resume()
{
    _pauseRequested = false;

    _z80->Resume();
}

//
// Set main Z80 CPU clock speed
// Multplier from 3.5MHz
//
void CPU::SetCPUClockSpeed(uint8_t multiplier)
{
	if (multiplier == 0)
	{
		LOGERROR("CPU::SetCPUClockSpeed - Z80 clock frequency multiplier cannot be 0");
		assert(false);
	}

    _z80->rate = (256 / multiplier);
}

uint32_t CPU::GetBaseCPUFrequency()
{
    return _state->base_z80_frequency;
}

uint32_t CPU::GetCPUFrequency()
{
    return _state->current_z80_frequency;
}

uint16_t CPU::GetCPUFrequencyMultiplier()
{
    return _state->current_z80_frequency_multiplier;
};

void CPU::CPUFrameCycle()
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

    AdjustFrameCounters();
}

/// Perform corrections after each frame rendered
void CPU::AdjustFrameCounters()
{
    /// region <Input parameters validation>
    if (_z80->t < _config->frame)
        return;
    /// endregion </Input parameters validation>

    // Update frame stats
    _state->frame_counter++;

    // Update counter
    _state->t_states += _config->frame;

    // Re-adjust CPU frame t-state counter and interrupt position
    _z80->t -= _config->frame;
    _z80->eipos -= _config->frame;

    /// region <TSConf only>

    if (_config->mem_model == MM_TSL)
    {
        _state->ts.intctrl.last_cput -= _config->frame;
    }

    /// endregion </TSConf only>
}

void CPU::UpdateScreen()
{
	GetZ80()->UpdateScreen();
}