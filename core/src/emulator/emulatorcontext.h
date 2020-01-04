#pragma once
#include "stdafx.h"

#include "platform.h"

class EmulatorContext
{
public:
	// Global emulator configuration (read from ini file)
	CONFIG config;

	// Emulated system state (ports, flags including peripheral devices)
	COMPUTER state;

	// Temporary state for all extended platform features
	// TODO: rework and put into appropriate platform / state classes
	TEMP temporary;

	// Host system properties / context
	HOST host;
};
