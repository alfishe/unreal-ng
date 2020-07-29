#include "stdafx.h"

#include "common/logger.h"

#include "cpu.h"
#include <algorithm>
#include <cassert>
#include "emulator/ports/portdecoder.h"
#include <emulator/video/videocontroller.h>


// Instantiate CPU tables as static (only one instance per process)
CPUTables CPU::_cpuTables;

CPU::CPU(EmulatorContext* context)
{
	_context = context;
    _state = &_context->state;
    _config = &_context->config;

	// Instantiation sequence
    // Step 1       - Memory()

	// Step N - 1   - Z80()
	// Step N       - PortDecoder()

	_messageCenter = &MessageCenter::DefaultMessageCenter();

	// Register itself in context
	_context->pCPU = this;

	// Create memory subsystem (allocates all RAM/ROM regions)
	_memory = new Memory(context);
	_context->pMemory = _memory;

	// Instantiate ROM implementation
	_rom = new ROM(context);

	// Instantiate Keyboard implementation
	_keyboard = new Keyboard(context);
	_context->pKeyboard = _keyboard;

	// Instantiate sound manager
	_sound = new Sound(context);

	// Create HDD controller
	_hdd = new HDD(context);

	// Create Video controller
	VideoModeEnum mode = M_ZX48; // Make ZX the default video mode on start
	_screen = VideoController::GetScreenForMode(mode, _context);
	_context->pScreen = _screen;

    // Create main CPU core instance (Z80)
    _cpu = new Z80(context);
    UseFastMemoryInterface();    // Use fast memory interface by default

    // Instantiate ports decoder
    // As ports decoder should know and control all peripherals - instantiate it as last step
    MEM_MODEL model = context->config.mem_model;
    _ports = new Ports(context);
    _portDecoder = PortDecoder::GetPortDecoderForModel(model, _context);
    if (!_portDecoder)
    {
        LOGERROR("CPU::CPU - Unable to create port decoder for model %d", model);
        assert("No port decoder");
    }
    context->pPortDecoder = _portDecoder;
}

CPU::~CPU()
{
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

    _context->pMemory = nullptr;
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

	if (_rom != nullptr)
	{
		delete _rom;
		_rom = nullptr;
	}

	if (_cpu != nullptr)
	{
		delete _cpu;
		_cpu = nullptr;
	}

	_context = nullptr;
}

// Configuration methods
void CPU::UseFastMemoryInterface()
{
	_cpu->MemIf = _cpu->FastMemIf;
}

void CPU::UseDebugMemoryInterface()
{
	_cpu->MemIf = _cpu->DbgMemIf;
}


void CPU::Reset()
{
	static MessageCenter& messageCenter = *_messageCenter;
	static int topicID = messageCenter.RegisterTopic("CPU_RESET");
	messageCenter.Post(topicID, (void*)"CPU reset started");

	// Set default ROM according to config settings (can be overriden for advanced platforms like TS-Conf and ATM)
	_mode = static_cast<ROMModeEnum>(_config->reset_rom);

	/// region <Obsolete>
	/*
	state.pEFF7 &= config.EFF7_mask;
	state.pEFF7 |= EFF7_GIGASCREEN; // [vv] disable turbo
	{
		//config.frame = frametime;
		//_cpu.SetTpi(config.frame);
		//                if ((conf.mem_model == MM_PENTAGON)&&(comp.pEFF7 & EFF7_GIGASCREEN))conf.frame = 71680; //removed 0.37
		//apply_sound();
	} //Alone Coder 0.36.4
	
	state.p7FFD = state.pDFFD = state.pFDFD = state.p1FFD = 0;
	state.p7EFD = state.p78FD = state.p7AFD = state.p7CFD = state.gmx_config = state.gmx_magic_shift = 0;
	state.pLSY256 = 0;

	state.ulaplus_mode = 0;
	state.ulaplus_reg = 0;

	state.p00 = 0;		// Quorum
	state.p80FD = 0; 	// Quorum
	state.pBF = 0;		// ATM3
	state.pBE = 0;		// ATM3

	/// region <TSConf specific>
	// TODO: Move to TSConf plugin
	if (config.mem_model == MM_TSL)
	{
		// tsinit();

		switch (_mode)
		{
			case RM_SYS: {state.ts.memconf = 4; break; }
			case RM_DOS: {state.ts.memconf = 0; break; }
			case RM_128: {state.ts.memconf = 0; break; }
			case RM_SOS: {state.ts.memconf = 0; break; }
		}

		#ifdef MOD_VID_VD
				comp.vdbase = 0; comp.pVD = 0;
		#endif

		// load_spec_colors();
	}
	/// endregion </TSConf specific>

	// LSY256 specific
	if (config.mem_model == MM_LSY256)
		_mode = RM_SYS;

	// ATM specific
	if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
	{
		switch (_mode)
		{
			case RM_DOS:
				// Disable custom palette, Disable CP/M
				// Enable memory dispatcher
				// Enable default keyboard
				// Enable frame interrupts
				//set_atm_FF77(0x4000 | 0x200 | 0x100, 0x80 | 0x40 | 0x20 | 3);
				state.pFFF7[0] = 0x100 | 1; // trdos
				state.pFFF7[1] = 0x200 | 5; // ram 5
				state.pFFF7[2] = 0x200 | 2; // ram 2
				state.pFFF7[3] = 0x200;     // ram 0

				state.pFFF7[4] = 0x100 | 1; // trdos
				state.pFFF7[5] = 0x200 | 5; // ram 5
				state.pFFF7[6] = 0x200 | 2; // ram 2
				state.pFFF7[7] = 0x200;     // ram 0
				break;
			default:
				;
				//set_atm_FF77(0, 0);
		}
	}

	if (config.mem_model == MM_ATM450)
	{
		switch (_mode)
		{
			case RM_DOS:
				//set_atm_aFE(0x80 | 0x60);
				state.aFB = 0;
				break;
			default:
				//set_atm_aFE(0x80);
				state.aFB = 0x80;
		}
	}

	state.flags = 0;
	state.active_ay = 0;

	// Set base CPU clock frequency
	if (config.mem_model == MM_TSL)
	{
		// Set TSConf clock speed based on settings
		switch (state.ts.zclk)
		{
			case 0: SetCPUClockSpeed(1); break;
			case 1: SetCPUClockSpeed(2); break;
			case 2: SetCPUClockSpeed(4); break;
			case 3: SetCPUClockSpeed(4); break;
		}

		state.ts.intctrl.frame_len = (config.intlen * _cpu->rate) >> 8;
	}
	else
		SetCPUClockSpeed(1);		// turbo 1x (3.5MHz) for all other clones
	 */
	/// endregion </Obsolete>

	// Reset main Z80 CPU and all peripherals
	_cpu->Reset();					// Main Z80
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

	// Turn off TRDOS ROM by default
	if ((!_config->trdos_present && _mode == RM_DOS) ||
		(!_config->cache && _mode == RM_CACHE))
		_mode = RM_SOS;

	// Set ROM mode
	_memory->SetROMMode(_mode);

	_memory->SetROMMode(RM_128);

	// Reset counters
    _state->frame_counter = 0;
	_state->t_states = 0;

	messageCenter.Post(topicID, (void*)"CPU reset finished");
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

	_cpu->rate = (256 / multiplier);
}

void CPU::CPUFrameCycle()
{
	// Execute Z80 cycle
	if (_cpu->dbgchk)
	{
		// Use advanced (but slow) memory access interface when Debugger is on
		UseDebugMemoryInterface();
		_cpu->Z80FrameCycle();
	}
	else
	{
		// Use fast memory access when no Debugger used
		UseFastMemoryInterface();
		_cpu->Z80FrameCycle();
	}

	// Update frame stats
    _state->frame_counter++;

    _state->t_states += _config->frame;
	_cpu->t -= _config->frame;
	_cpu->eipos -= _config->frame;

	if (_config->mem_model == MM_TSL)
	{
        _state->ts.intctrl.last_cput -= _config->frame;
	}
}

void CPU::UpdateScreen()
{
	
}