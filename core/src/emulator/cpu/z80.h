#pragma once
#include "stdafx.h"

class Z80
{


public:
	Z80();
	virtual ~Z80();


public:
	uint8_t DirectMem(uint32_t addr) const;		// Direct access to global memory

	void Step();
};