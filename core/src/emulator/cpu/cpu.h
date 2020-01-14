#pragma once
#include "stdafx.h"

#include "cputables.h"

class CPU
{
	// Ensure that all flag / decoding tables are initialized only once
	friend class CPUTables;
public:
	static CPUTables _cpuTables;

public:
	CPU();
	virtual ~CPU();

public:
	void CPUCycle();
};
