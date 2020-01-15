#pragma once
#include "stdafx.h"

#include "common/logger.h"
#include "cpu/cpu.h"
#include "emulator/emulatorcontext.h"

class MainLoop
{
protected:
	CPU* _cpu = nullptr;
	EmulatorContext* _context = nullptr;

public:
	MainLoop() = delete;	// Disable default constructor. C++ 11 or better feature
	MainLoop(CPU* cpu, EmulatorContext* context);
	virtual ~MainLoop();

	void Run(const bool& exit);
	void Stop();

protected:
	void RunFrame();

	void InitSoundFrame();
	void InitVideoFrame();

	void ExecuteCPUCycle();
	void DoIdle();
};