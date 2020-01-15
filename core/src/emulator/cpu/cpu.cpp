#include "stdafx.h"

#include "cpu.h"
#include <algorithm>

using namespace std;

// Instantiate CPU tables as static (only one instance per process)
CPUTables CPU::_cpuTables;

CPU::CPU(EmulatorContext* context)
{
	_context = context;

	// Create main CPU core instance (Z80)
	_cpu = new Z80(context);

	// Create memory subsystem (allocates all RAM/ROM regions)
	_memory = new Memory(context);
	_context->pMemory = _memory;
}

CPU::~CPU()
{
	if (_cpu != nullptr)
	{
		delete _cpu;
		_cpu = nullptr;
	}

	_context->pMemory = nullptr;
	if (_memory != nullptr)
	{
		delete _memory;
		_memory = nullptr;
	}

	_context = nullptr;
}

Z80* CPU::GetZ80Instance()
{
	return _cpu;
}

Memory* CPU::GetMemory()
{
	return _memory;
}


// Configuration methods
void CPU::SetFastMemoryInterface()
{
	_cpu->MemIf = _cpu->FastMemIf;
}

void CPU::SetDebugMemoryInterface()
{
	_cpu->MemIf = _cpu->DbgMemIf;
}

void CPU::CPUFrameCycle()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;

	// Execute Z80 cycle
	if (_cpu->dbgchk)
	{
		// Use advanced (but slow) memory access interface when Debugger is on
		SetDebugMemoryInterface();
		_cpu->Z80FrameCycle();
	}
	else
	{
		// Use fast memory access when no Debugger used
		SetFastMemoryInterface();
		_cpu->Z80FrameCycle();
	}

	// Update frame stats
	state.t_states += config.frame;
	_cpu->t -= config.frame;
	_cpu->eipos -= config.frame;
	state.frame_counter++;

	if (config.mem_model == MM_TSL)
	{
		state.ts.intctrl.last_cput -= config.frame;
	}
}

void CPU::UpdateScreen()
{
	
}