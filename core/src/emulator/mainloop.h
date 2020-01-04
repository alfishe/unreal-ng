#pragma once
#include "stdafx.h"

#include "cpu/cpu.h"

class MainLoop
{
protected:
	CPU* _cpu = nullptr;

public:
	MainLoop() = delete;	// Disable default constructor. C++ 11 or better feature
	MainLoop(CPU* cpu);
	virtual ~MainLoop();

	void Run(const bool& exit);

protected:
	void RunFrame();

	void InitSoundFrame();
	void InitVideoFrame();

	void ExecuteCPUFrame();
};