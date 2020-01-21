#pragma once
#include "stdafx.h"

#include "sysdefs.h"
#include "emulator/platform.h"
#include "emulator/memory/memory.h"


class RAM
{
protected:
	#ifdef CACHE_ALIGNED
		ATTR_ALIGN(4096)
		uint8_t _memory[PAGE * MAX_PAGES];
	#else // __declspec(align) not available, force u64 align with old method
		int64_t memory__[PAGE * MAX_PAGES / sizeof(int64_t)];
		uint8_t * const memory = (uint8_t*)memory__;
	#endif

// Public properties
public:
	static const uint32_t MaxRAMSize();

// Public methods
public:
	void FillRandom();
};