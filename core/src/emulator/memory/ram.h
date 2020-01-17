#pragma once
#include "stdafx.h"

#include "sysdefs.h"
#include "../platform.h"

// Max RAM size is 4MBytes. Each model has own limits. Max ram used for ZX-Evo / TSConf
#define MAX_RAM_SIZE (MAX_RAM_PAGES * PAGE)

// MAX_RAM_PAGES and PAGE defined in platform.h
#define RAM_BASE_M  _memory
//#define CACHE_M     (RAM_BASE_M + MAX_RAM_PAGES * PAGE)
//#define MISC_BASE_M (CACHE_M + MAX_CACHE_PAGES * PAGE)
//#define ROM_BASE_M  (MISC_BASE_M + MAX_MISC_PAGES * PAGE)

// RAM pages access helpers
#define page_ram(a) RAM_BASE_M + PAGE * (a)
#define page_rom(a) ROM_BASE_M + PAGE * (a)

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