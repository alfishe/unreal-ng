#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "common/modulelogger.h"

class CPU;
class Keyboard;
class Memory;
class PortDecoder;
class Screen;
class DebugManager;

class EmulatorContext
{
public:
    // Settings for logger
    LoggerSettings loggerSettings;

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

	DebugManager* pDebugManager;
};
