#include "stdafx.h"

#include "common/logger.h"

#include "rom.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/cpu.h"
#include "emulator/memory/memory.h"

ROM::ROM(EmulatorContext* context)
{
	_context = context;
}

ROM::~ROM()
{
	_context = nullptr;
}

bool ROM::LoadROM()
{
	bool result = false;

	CONFIG& config = _context->config;
	Memory& memory = *_context->pMemory;
	
	string romname;

	switch (config.mem_model)
	{
		case MM_PENTAGON:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.pent_rom_path;
			break;
		case MM_PROFI:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.profi_rom_path;
			break;
		case MM_SCORP:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.scorp_rom_path;
			break;
		case MM_PROFSCORP:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.prof_rom_path;
			break;
		case MM_KAY:
			memory.base_128_rom = memory.ROMPageAddress(0);
			memory.base_sos_rom = memory.ROMPageAddress(1);
			memory.base_dos_rom = memory.ROMPageAddress(2);
			memory.base_sys_rom = memory.ROMPageAddress(3);
			romname = config.kay_rom_path;
			break;
		case MM_ATM3:
			memory.base_sos_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sys_rom = memory.ROMPageAddress(3);
			romname = config.atm3_rom_path;					// TODO: Correct?
			break;
		case MM_ATM450:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.atm1_rom_path;
			break;
		case MM_ATM710:
			memory.base_sos_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sys_rom = memory.ROMPageAddress(3);
			romname = config.atm2_rom_path;					// TODO: Correct?
			break;
		case MM_PLUS3:
			memory.base_128_rom = memory.ROMPageAddress(0);
			memory.base_sys_rom = memory.ROMPageAddress(1);
			memory.base_dos_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.plus3_rom_path;
			break;
		case MM_QUORUM:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.quorum_rom_path;
			break;
		case MM_TSL:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.tsl_rom_path;
			break;
		case MM_LSY256:
			memory.base_128_rom = memory.ROMPageAddress(0);
			memory.base_sos_rom = memory.ROMPageAddress(1);
			memory.base_dos_rom = memory.ROMPageAddress(3);
			memory.base_sys_rom = memory.ROMPageAddress(2);
			romname = config.lsy_rom_path;
			break;
		case MM_PHOENIX:
			memory.base_sys_rom = memory.ROMPageAddress(0);
			memory.base_dos_rom = memory.ROMPageAddress(1);
			memory.base_128_rom = memory.ROMPageAddress(2);
			memory.base_sos_rom = memory.ROMPageAddress(3);
			romname = config.phoenix_rom_path;
			break;
	};

	if (config.use_romset)
	{
		// Use separate files for each ROM bank
		result = LoadROMSet();
	}
	else
	{
		// Load model specific ROM bundles (single file)
		wstring wromname = StringHelper::StringToWideString(romname.c_str());
		if (!romname.empty() && FileHelper::FileExists(wromname))
		{
			// Try to load ROM up to 1024KB in size
			uint16_t loadedBanks = LoadROM(wromname, memory.ROMBase(), 64);

			result = true;

			// Check loaded ROM size
			if (config.mem_model == MM_PROFSCORP)
			{
				if (loadedBanks != 4 || loadedBanks != 8 || loadedBanks != 16)
				{
					LOGERROR("Incorrect ROM size for Scorpion ZS256 Prof. Should be 64|128|256 KB. Found %d", loadedBanks * PAGE);
					result = false;
				}
			}
			else if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
			{
				if (loadedBanks != 4 || loadedBanks != 8 || loadedBanks != 32 || loadedBanks != 64)
				{
					LOGERROR("Incorrect ROM size for ATM3/7.10. Should be 64|128|512|1024 KB. Found %d", loadedBanks * PAGE);
					result = false;
				}
				else
				{
					// ATM3 and 7.10 keep standard ROM set in last 4 banks
					uint8_t* lastPage = memory.ROMBase() + (loadedBanks - 4);
					memory.base_sos_rom = lastPage + 0 * PAGE;
					memory.base_dos_rom = lastPage + 1 * PAGE;
					memory.base_128_rom = lastPage + 2 * PAGE;
					memory.base_sys_rom = lastPage + 3 * PAGE;
				}
			}
			else if (config.mem_model == MM_GMX)
			{
				if (loadedBanks != 32)
				{
					LOGERROR("Incorrect ROM size for GMX. Should be 512 KB. Found %d", loadedBanks * PAGE);
					result = false;
				}
			}
			else
			{
				// All other models have 64KB (4 banks) ROM chips
				if (loadedBanks != 4)
				{
					LOGERROR("Incorrect ROM size. Should be 64 KB. Found %d", loadedBanks * PAGE);
					result = false;
				}
			}

			if (result)
			{
				LOGDEBUG("ROM successully loaded from file '%s'", romname.c_str());
			}
		}
		else
		{
			LOGERROR("Unable to load ROM file '%s'", romname.c_str());
		}
	}

	return result;
}

bool ROM::LoadROMSet()
{
	bool result = false;

	CONFIG& config = _context->config;
	Memory& memory = *_context->pCPU->GetMemory();

	// BASIC48 (SOS)
	wstring rompath = StringHelper::StringToWideString(config.sos_rom_path);
	bool result1 = LoadROM(rompath, memory.base_sos_rom);
	if (!result1)
	{
		LOGERROR("Unable to load BASIC48 (SOS) ROM from file: '%s'", config.sos_rom_path);
	}

	// BASIC128
	rompath = StringHelper::StringToWideString(config.zx128_rom_path);
	bool result2 = LoadROM(rompath, memory.base_128_rom);
	if (!result2)
	{
		LOGERROR("Unable to load BASIC128 ROM from file: '%s'", config.zx128_rom_path);
	}

	// DOS (TR-DOS)
	rompath = StringHelper::StringToWideString(config.dos_rom_path);
	bool result3 = LoadROM(rompath, memory.base_dos_rom);
	if (!result3)
	{
		LOGERROR("Unable to load DOS (TR-DOS) ROM from file: '%s'", config.dos_rom_path);
	}

	// Shadow (SYS)
	rompath = StringHelper::StringToWideString(config.sys_rom_path);
	bool result4 = LoadROM(rompath, memory.base_sys_rom);
	if (!result4)
	{
		LOGERROR("Unable to load Shadow (SYS) ROM from file: '%s'", config.sys_rom_path);
	}

	result = result1 & result2 & result3 & result4;

	return result;
}

//
// Loads up to <max_banks> ROM banks (16KB each). from file with filepath <path> to the buffer with address <bank>
//
uint16_t ROM::LoadROM(wstring& path, uint8_t* bank, uint16_t max_banks)
{
	uint16_t result = 0;

	if (path.empty())
	{
		LOGERROR("Config::LoadROM - Empty ROM path supplied");

		memset(bank, 0xFF, max_banks * PAGE);
		return result;
	}

	string normalizedPath = StringHelper::WideStringToString(path);
	FILE* romfile = fopen(normalizedPath.c_str(), "rb");
	if (romfile)
	{
		size_t size = fread(bank, 1, max_banks * PAGE, romfile);
		if (size && (size & (PAGE - 1)))
		{

			result = static_cast<uint16_t>(size / PAGE);
		}
		else
		{
			LOGERROR("Config::LoadROM - Incorrect ROM file size. Expected: %d, found %d", max_banks * PAGE, size);
		}

		fclose(romfile);
	}
	else
	{
		LOGERROR("Config::LoadROM - file %s not found", path);
	}


	return result;
}
