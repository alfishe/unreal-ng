#include "stdafx.h"

#include "common/logger.h"

#include "rom.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/cpu.h"
#include "emulator/memory/memory.h"
#include "3rdparty/digestpp/digestpp.hpp"

using digestpp::md5;
using digestpp::sha256;

ROM::ROM(EmulatorContext* context)
{
	_context = context;

	// TODO: load known ROM signatures from file
	_signatures.insert({ "8d93c3342321e9d1e51d60afcd7d15f6a7afd978c231b43435a7c0757c60b9a3", "128k ROM 1 (48k BASIC)" });
	_signatures.insert({ "3ba308f23b9471d13d9ba30c23030059a9ce5d4b317b85b86274b132651d1425", "128k ROM 0 (128k editor & menu)" });
	_signatures.insert({ "1ef928538972ed8f0425c4469f3f471267393f7635b813f000de0fec4ea39fa3", "TR-DOS v5.04TM ROM" });
	_signatures.insert({ "9d4bf28f2d1a9acac9907c918be3c3070f7250bc677919cface5e253a199fc7a", "HRom boot" });
	//_signatures.insert({ "", "" });
}

ROM::~ROM()
{
	_context = nullptr;

	LOGDEBUG("ROM::~ROM()");
}

/// Load single ROM file based on information from config file and information about selected model
/// \return Result whether ROM file load was successful
bool ROM::LoadROM()
{
	bool result = false;

	static CONFIG& config = _context->config;
	static Memory& memory = *_context->pMemory;
	
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
	    case MM_SPECTRUM48:
	        memory.base_sos_rom = memory.ROMPageAddress(0);
	        romname = config.zx48_rom_path;

	        memory.base_128_rom = nullptr;
	        memory.base_dos_rom = nullptr;
	        memory.base_sys_rom = nullptr;
	        break;
	    case MM_SPECTRUM128:
            memory.base_128_rom = memory.ROMPageAddress(0);
            memory.base_sos_rom = memory.ROMPageAddress(1);
            romname = config.zx128_rom_path;

            memory.base_dos_rom = nullptr;
            memory.base_sys_rom = nullptr;
            break;
        case MM_PLUS3:
            memory.base_128_rom = memory.ROMPageAddress(0);
            memory.base_sys_rom = memory.ROMPageAddress(1);
            memory.base_dos_rom = memory.ROMPageAddress(2);
            memory.base_sos_rom = memory.ROMPageAddress(3);
            romname = config.plus3_rom_path;
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
		//wstring wromname = StringHelper::StringToWideString(romname.c_str());
		if (!romname.empty())
		{
			// Try to load ROM up to 1024KB (64 pages 16KiB each) in size
			uint16_t loadedBanks = LoadROM(romname, memory.ROMBase(), MAX_ROM_PAGES);
            _ROMBanksLoaded = loadedBanks;

			result = true;

			// Check loaded ROM size
			if (config.mem_model == MM_PROFSCORP)
			{
				if (loadedBanks != 4 || loadedBanks != 8 || loadedBanks != 16)
				{
					LOGERROR("Incorrect ROM size for Scorpion ZS256 Prof. Should be 64|128|256 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}
			else if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
			{
				if (loadedBanks != 4 || loadedBanks != 8 || loadedBanks != 32 || loadedBanks != 64)
				{
					LOGERROR("Incorrect ROM size for ATM3/7.10. Should be 64|128|512|1024 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
				else
				{
					// ATM3 and 7.10 keep standard ROM set in last 4 banks
					uint8_t* lastPage = memory.ROMBase() + (loadedBanks - 4);
					memory.base_sos_rom = lastPage + 0 * PAGE_SIZE;
					memory.base_dos_rom = lastPage + 1 * PAGE_SIZE;
					memory.base_128_rom = lastPage + 2 * PAGE_SIZE;
					memory.base_sys_rom = lastPage + 3 * PAGE_SIZE;
				}
			}
			else if (config.mem_model == MM_GMX)
			{
				if (loadedBanks != 32)
				{
					LOGERROR("Incorrect ROM size for GMX. Should be 512 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}
			else if (config.mem_model == MM_TSL)
			{
				if (loadedBanks != 32)
				{
					LOGERROR("Incorrect ROM size for TS-Conf. Should be 512 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}
            else if (config.mem_model == MM_SPECTRUM48)
            {
                if (loadedBanks != 1)
                {
                    LOGERROR("Incorrect ROM size. Should be 16 KB. Found %d", loadedBanks * PAGE_SIZE);
                    result = false;
                }
            }
			else if (config.mem_model == MM_SPECTRUM128)
            {
			    if (loadedBanks != 2)
                {
                    LOGERROR("Incorrect ROM size. Should be 32 KB. Found %d", loadedBanks * PAGE_SIZE);
                    result = false;
                }
            }
			else
			{
				// All other models have 64KB (4 banks) ROM chips
				if (loadedBanks != 4)
				{
					LOGERROR("Incorrect ROM size. Should be 64 KB. Found %d", loadedBanks * PAGE_SIZE);
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
	string rompath = config.sos_rom_path;
	bool result1 = LoadROM(rompath, memory.base_sos_rom);
	if (!result1)
	{
		LOGERROR("Unable to load BASIC48 (SOS) ROM from file: '%s'", config.sos_rom_path);
	}

	// BASIC128
	rompath = config.zx128_rom_path;
	bool result2 = LoadROM(rompath, memory.base_128_rom);
	if (!result2)
	{
		LOGERROR("Unable to load BASIC128 ROM from file: '%s'", config.zx128_rom_path);
	}

	// DOS (TR-DOS)
	rompath = config.dos_rom_path;
	bool result3 = LoadROM(rompath, memory.base_dos_rom);
	if (!result3)
	{
		LOGERROR("Unable to load DOS (TR-DOS) ROM from file: '%s'", config.dos_rom_path);
	}

	// Shadow (SYS)
    rompath = config.sys_rom_path;
	bool result4 = LoadROM(rompath, memory.base_sys_rom);
	if (!result4)
	{
		LOGERROR("Unable to load Shadow (SYS) ROM from file: '%s'", config.sys_rom_path);
	}

	result = result1 & result2 & result3 & result4;

	return result;
}

/// Loads up to <max_banks> ROM banks (16KB each). from file with filepath <path> to the buffer with address <bank>
/// \param path
/// \param bank
/// \param max_banks Max 16KiB banks to load
/// \return Number of banks loaded
uint16_t ROM::LoadROM(string& path, uint8_t* bank, uint16_t max_banks)
{
	uint16_t result = 0;

	// Clear whole ROM area before loading
	memset(bank, 0xFF, max_banks * PAGE_SIZE);

	if (path.empty())
	{
		LOGERROR("ROM::LoadROM - Empty ROM path supplied");
		return result;
	}

	wstring resolvedPath = StringHelper::StringToWideString(path);
	if (!FileHelper::FileExists(resolvedPath))
	{
		// Try to use as relative path if not found using original path
		wstring executablePath = FileHelper::GetExecutablePath();
		resolvedPath = FileHelper::PathCombine(executablePath, resolvedPath);

		if (!FileHelper::FileExists(resolvedPath))
		{
			LOGERROR("ROM::LoadROM - file %s not found", FileHelper::PrintablePath(resolvedPath).c_str());
			return result;
		}
	}

	string normalizedPath = FileHelper::PrintablePath(resolvedPath);
	FILE* romfile = fopen(normalizedPath.c_str(), "rb");
	if (romfile)
	{
		size_t size = fread(bank, 1, max_banks * PAGE_SIZE, romfile);
		if (size && !(size & (PAGE_SIZE - 1)))
		{

			result = static_cast<uint16_t>(size / PAGE_SIZE);
		}
		else
		{
			LOGERROR("ROM::LoadROM - Incorrect ROM file size. Expected: %d, found %d", max_banks * PAGE_SIZE, size);
		}

		fclose(romfile);
	}
	else
	{
		LOGERROR("ROM::LoadROM - unable to read from file %s", FileHelper::PrintablePath(resolvedPath).c_str());
	}


	return result;
}

void ROM::CalculateSignatures()
{
	CONFIG& config = _context->config;
	Memory& memory = *_context->pMemory;

	if (_ROMBanksLoaded == 0)
	{
		LOGERROR("ROM::CalculateSignatures - no ROM loaded. Unable to calculate ROM signatures");
		return;
	}

	LOGINFO("ROM Banks info (as loaded):");
	for (int i = 0; i < _ROMBanksLoaded; i++)
    {
	    std::string signature = CalculateSignature(memory.ROMPageAddress(i), 0x4000);
	    const char* detectedROM = _signatures.find(signature) != _signatures.end() ? _signatures[signature].c_str() : "Unknown ROM";
        LOGINFO("  ROM page %d: %s", i, detectedROM);
    }

    LOGINFO("ROM Banks info (as mapped):");
	if (memory.base_sos_rom)
    {
        std::string signature = CalculateSignature(memory.base_sos_rom, 0x4000);
        LOGINFO("  base_sos_rom: %s", _signatures.find(signature) != _signatures.end() ? _signatures[signature].c_str() : "Unknown ROM");
    }

    if (memory.base_128_rom)
    {
        std::string signature = CalculateSignature(memory.base_128_rom, 0x4000);
        LOGINFO("  base_128_rom: %s", _signatures.find(signature) != _signatures.end() ? _signatures[signature].c_str() : "Unknown ROM");
    }

    if (memory.base_dos_rom)
    {
        std::string signature = CalculateSignature(memory.base_dos_rom, 0x4000);
        LOGINFO("  base_dos_rom: %s", _signatures.find(signature) != _signatures.end() ? _signatures[signature].c_str() : "Unknown ROM");
    }

    if (memory.base_sys_rom)
    {
        std::string signature = CalculateSignature(memory.base_sys_rom, 0x4000);
        LOGINFO("  base_sys_rom: %s", _signatures.find(signature) != _signatures.end() ? _signatures[signature].c_str() : "Unknown ROM");
    }
}

string ROM::CalculateSignature(uint8_t* buffer, size_t length)
{
	string result;

	if (buffer == nullptr || length == 0)
	{
		LOGERROR("ROM::CalculateSignature - buffer shouldn't be nullptr and length needs to be > 0");
		return result;
	}

	result = sha256().absorb(buffer, length).hexdigest();

	return result;
}
