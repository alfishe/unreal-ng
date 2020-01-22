#pragma once
#include "stdafx.h"

#include "sysdefs.h"
#include "emulator/platform.h"
#include "emulator/memory/memory.h"


class RAM
{
// Public properties
public:
	static const uint32_t MaxRAMSize();

// Public methods
public:
	void FillRandom();
};