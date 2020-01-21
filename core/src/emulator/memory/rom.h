#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include <string>

class ROM
{
protected:
	EmulatorContext* _context = nullptr;
	string _activeROMFile;

public:
	ROM() = delete;		// Disable default constructor. C++ 11 feature
	ROM(EmulatorContext* context);
	virtual ~ROM();

	[[nodiscard]] bool LoadROM();

	[[nodiscard]] bool LoadROMSet();
	[[nodiscard]] uint16_t LoadROM(wstring& path, uint8_t* bank, uint16_t max_banks = 1);
};

