#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/platform.h"

class ModuleLogger;
class CPU;
class Emulator;
class Keyboard;
class MainLoop;
class Memory;
class PortDecoder;
class Screen;
class DebugManager;
class Z80Disassembler;

class EmulatorContext
{
    /// region <Child object references>
public:
    // Advanced logger instance
    ModuleLogger* pModuleLogger;

	// Global emulator configuration (MemoryRead from ini file)
	CONFIG config;

	// Emulated system state (ports, flags including peripheral devices)
	State state;

	// Temporary state for all extended platform features
	// TODO: rework and put into appropriate platform / state classes
	TEMP temporary;

	// Host system properties / context
	HOST host;

	// Computer system instance
	CPU* pCPU;

	// Keyboard controller instance
	Keyboard* pKeyboard;

	// Memory controller instance
	Memory* pMemory;

	// Model-specific port decoder
	PortDecoder* pPortDecoder;

	// Video controller parameters and logic
	Screen* pScreen;

	// Debug manager (includes Breakpoints, Labels and Disassembler)
	DebugManager* pDebugManager;
    /// endregion </Child object references>

    /// region <Parent object references>
public:
    Emulator* pEmulator;
    /// endregion </Parent object refereces>

    /// region <Constructors / destructors>
public:
    EmulatorContext();                      // Default constructor with LogTrace default logging level
    EmulatorContext(LoggerLevel level);     // Constructor allowing to specify default logging level
    EmulatorContext(Emulator* emulator, LoggerLevel level = LoggerLevel::LogTrace);    // Constructor registering reference to parent Emulator object
    virtual ~EmulatorContext();
    /// endregion </Constructors / destructors>
};
