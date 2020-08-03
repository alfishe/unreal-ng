#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include <string>
#include <map>

/// region <Structures and type>

struct KnownROM
{
	const char* FullName;
	const char* Hash_SHA256;
};

typedef map<string, string> ROMSMap;
typedef pair<string, string> ROMSignature;

/// endregion </Structures and type>

class ROM
{
protected:
	EmulatorContext* _context = nullptr;
	std::string _activeROMFile;
	uint8_t _ROMBanksLoaded = 0;


	ROMSMap _signatures;

public:
	ROM() = delete;		// Disable default constructor. C++ 11 feature
	ROM(EmulatorContext* context);
	virtual ~ROM();

	[[nodiscard]] bool LoadROM();

	[[nodiscard]] bool LoadROMSet();
	[[nodiscard]] uint16_t LoadROM(string& path, uint8_t* bank, uint16_t max_banks = 1);

	// Signature-related methods
public:
	void CalculateSignatures();
	std::string CalculateSignature(uint8_t* buffer, size_t length);
};

