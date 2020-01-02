#pragma once
#include "stdafx.h"

#include "cpu/cpu.h"

class Emulator
{
protected:
	CPU* _cpu = nullptr;

	bool _isPaused = false;
	bool _isRunning = false;

	// Constructors / destructors
public:
	Emulator();
	virtual ~Emulator();

public:
	// Initialization operations
	bool Init();
	void Release();

	// Emulator control cycle
	void Reset();
	void Run();
	void Pause();

	// Status methods
	bool IsRunning();
	bool IsPaused();
	bool IsDebug();

	// Debug methods

	// Counters method
	void ResetCountersAll();
	void ResetCounter();
};

