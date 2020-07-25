#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

class CPU;
class Memory;
class PortDecoder;
class Screen;
class DebugManager;

class EmulatorContext
{
public:
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

	// Memory controller instance
	Memory* pMemory;

	// Model-specific port decoder
	PortDecoder* pPortDecoder;

	// Video controller parameters and logic
	Screen* pScreen;

	DebugManager* pDebugManager;
};
