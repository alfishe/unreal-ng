#include "stdafx.h"

#include "common/logger.h"

#include "rom.h"

#include "common/collectionhelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "3rdparty/digestpp/digestpp.hpp"

using digestpp::md5;
using digestpp::sha256;

/// region <Constructors / destructors>

ROM::ROM(EmulatorContext* context)
{
	_context = context;
	_logger = _context->pModuleLogger;

	// TODO: load known ROM signatures from file

	// ZX-Spectrum 128K (Toaster)
    _signatures.insert({ "d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42", "Original 48K ROM" });
    _signatures.insert({ "3ba308f23b9471d13d9ba30c23030059a9ce5d4b317b85b86274b132651d1425", "128k ROM 0 (128k editor & menu)" });                  // 16Kb
	_signatures.insert({ "8d93c3342321e9d1e51d60afcd7d15f6a7afd978c231b43435a7c0757c60b9a3", "128k ROM 1 (48k BASIC)" });                           // 16Kb
    _signatures.insert({ "c1ff621d7910105d4ee45c31e9fd8fd0d79a545c78b66c69a562ee1ffbae8d72", "128k ROM (Toaster)" });                               // 32Kb

    // ZX-Spectrum 128K +2
    _signatures.insert({ "dae0690d8b433ea22b76b62520341f784071dbde0d02f50af0e3fd20fc6bca4a", "128k +2 ROM" });                                      // 32Kb

    // ZX-Spectrum 128K +3
    _signatures.insert({ "ee8218fa43ecb672ed45370114294228213a82318c6d1b007ec86bee3293d1f2", "128k +3 ROM" });                                      // 32Kb

    _signatures.insert({ "39973c2ca4f573cf6188f8eb16338d669c8fd0a78d2683fe059ce56002e7b246", "Gluck service ROM" });                                // 16Kb
    _signatures.insert({ "9d4bf28f2d1a9acac9907c918be3c3070f7250bc677919cface5e253a199fc7a", "HRom boot" });                                        // 16Kb

	// Pentagon-specific
    _signatures.insert({ "7b88abff5964f0cf38481ac51bf035be2c01b8827569876b3d15eb3ac340fef3", "Pentagon 128k ROM"} );                                // 32Kb
	_signatures.insert({ "633343620691a592c706d18c927fd539b7069a5d0fb7011bcd3bfc94be418681", "Pentagon 128k ROM 0 (128k with TR-DOS in menu)"} );   // 16Kb
    _signatures.insert({ "110020ff7a4e350999777261000442426838cc391be93ba3146abc9477dcc05f", "Pentagon 48k ROM 3 (48k for 128k)"});                 // 16Kb
    // "128k ROM 1 (48k BASIC)" for Pentagon 128K is the same as for original ZX-Spectrum 128k

    // Scorpion ZX-256
    _signatures.insert({ "07c190ae55887b73916526e49919f2b4a4e6acd68212fdd374e3cf1f7dd5225d", "Scorpion ZS256 ROM (Non-Prof)"} );                    // 64Kb

    // TR-DOS
    _signatures.insert({ "91259fca6a8ded428cc24046f5b48b31d4043f2afbd9087d8946eaf4e10d71a5", "TR-DOS v5.03 ROM" });                                 // 16Kb
    _signatures.insert({ "e21d37271d087eab5ef8f88d8f3a58c8c19da1fa857b9790eaa974b231db9e13", "TR-DOS v5.04T ROM" });                                // 16Kb
    _signatures.insert({ "1ef928538972ed8f0425c4469f3f471267393f7635b813f000de0fec4ea39fa3", "TR-DOS v5.04TM ROM" });                               // 16Kb
    _signatures.insert({ "075c87ddb55a2fb633373e2d7c834f03e5d44b9b70889499ece732f377f5d224", "TR-DOS v5.13f ROM" });                                // 16Kb
	//_signatures.insert({ "", "" });
}

ROM::~ROM()
{
	_context = nullptr;

	MLOGDEBUG("ROM::~ROM()");
}

// endregion </Constructors / destructors>

std::string ROM::GetROMFilename()
{
    std::string result;

    const CONFIG& config = _context->config;

    switch (config.mem_model)
    {
        case MM_PENTAGON:
            result = config.pent_rom_path;
            break;
        case MM_SPECTRUM48:
            result = config.zx48_rom_path;
            break;
        case MM_SPECTRUM128:
            result = config.zx128_rom_path;
            break;
        case MM_PLUS3:
            result = config.plus3_rom_path;
            break;
        case MM_PROFI:
            result = config.profi_rom_path;
            break;
        case MM_SCORP:
            result = config.scorp_rom_path;
            break;
        case MM_PROFSCORP:
            result = config.prof_rom_path;
            break;
        case MM_KAY:
            result = config.kay_rom_path;
            break;
        case MM_ATM3:
            result = config.atm3_rom_path;					// TODO: Correct?
            break;
        case MM_ATM450:
            result = config.atm1_rom_path;
            break;
        case MM_ATM710:
            result = config.atm2_rom_path;					// TODO: Correct?
            break;
        case MM_QUORUM:
            result = config.quorum_rom_path;
            break;
        case MM_TSL:
            result = config.tsl_rom_path;
            break;
        case MM_LSY256:
            result = config.lsy_rom_path;
            break;
        case MM_PHOENIX:
            result = config.phoenix_rom_path;
            break;
        case MM_GMX:
            assert("Not implemented");
            break;
        default:
            break;
    };

    return result;
}

/// Load single ROM file based on information from config file and information about selected model
/// \return Result whether ROM file load was successful
bool ROM::LoadROM()
{
	bool result = false;

    const CONFIG& config = _context->config;
    Memory& memory = *_context->pMemory;
	
	std::string romname;

	switch (config.mem_model)
	{
		case MM_PENTAGON:
			memory.base_sys_rom = memory.ROMPageHostAddress(0); // Empty or system ROM
			memory.base_dos_rom = memory.ROMPageHostAddress(1); // TR-DOS 5.04T
			memory.base_128_rom = memory.ROMPageHostAddress(2); // Basic 128K
			memory.base_sos_rom = memory.ROMPageHostAddress(3); // 48K
			romname = config.pent_rom_path;
			break;
	    case MM_SPECTRUM48:
	        memory.base_sos_rom = memory.ROMPageHostAddress(0);
	        romname = config.zx48_rom_path;

	        memory.base_128_rom = nullptr;
	        memory.base_dos_rom = nullptr;
	        memory.base_sys_rom = nullptr;
	        break;
	    case MM_SPECTRUM128:
            memory.base_128_rom = memory.ROMPageHostAddress(0);
            memory.base_sos_rom = memory.ROMPageHostAddress(1);
            romname = config.zx128_rom_path;

            memory.base_dos_rom = nullptr;
            memory.base_sys_rom = nullptr;
            break;
        case MM_PLUS3:
            memory.base_128_rom = memory.ROMPageHostAddress(0);
            memory.base_sys_rom = memory.ROMPageHostAddress(1);
            memory.base_dos_rom = memory.ROMPageHostAddress(2);
            memory.base_sos_rom = memory.ROMPageHostAddress(3);
            romname = config.plus3_rom_path;
            break;
		case MM_PROFI:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.profi_rom_path;
			break;
		case MM_SCORP:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.scorp_rom_path;
			break;
		case MM_PROFSCORP:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.prof_rom_path;
			break;
		case MM_KAY:
			memory.base_128_rom = memory.ROMPageHostAddress(0);
			memory.base_sos_rom = memory.ROMPageHostAddress(1);
			memory.base_dos_rom = memory.ROMPageHostAddress(2);
			memory.base_sys_rom = memory.ROMPageHostAddress(3);
			romname = config.kay_rom_path;
			break;
		case MM_ATM3:
			memory.base_sos_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sys_rom = memory.ROMPageHostAddress(3);
			romname = config.atm3_rom_path;					// TODO: Correct?
			break;
		case MM_ATM450:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.atm1_rom_path;
			break;
		case MM_ATM710:
			memory.base_sos_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sys_rom = memory.ROMPageHostAddress(3);
			romname = config.atm2_rom_path;					// TODO: Correct?
			break;
		case MM_QUORUM:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.quorum_rom_path;
			break;
		case MM_TSL:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.tsl_rom_path;
			break;
		case MM_LSY256:
			memory.base_128_rom = memory.ROMPageHostAddress(0);
			memory.base_sos_rom = memory.ROMPageHostAddress(1);
			memory.base_dos_rom = memory.ROMPageHostAddress(3);
			memory.base_sys_rom = memory.ROMPageHostAddress(2);
			romname = config.lsy_rom_path;
			break;
		case MM_PHOENIX:
			memory.base_sys_rom = memory.ROMPageHostAddress(0);
			memory.base_dos_rom = memory.ROMPageHostAddress(1);
			memory.base_128_rom = memory.ROMPageHostAddress(2);
			memory.base_sos_rom = memory.ROMPageHostAddress(3);
			romname = config.phoenix_rom_path;
			break;
	    case MM_GMX:
            assert("Not implemented");
            break;
	    default:
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
					MLOGERROR("Incorrect ROM size for ATM3/7.10. Should be 64|128|512|1024 KB. Found %d", loadedBanks * PAGE_SIZE);
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
					MLOGERROR("Incorrect ROM size for GMX. Should be 512 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}
			else if (config.mem_model == MM_TSL)
			{
				if (loadedBanks != 32)
				{
					MLOGERROR("Incorrect ROM size for TS-Conf. Should be 512 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}
            else if (config.mem_model == MM_SPECTRUM48)
            {
                if (loadedBanks != 1)
                {
                    MLOGERROR("Incorrect ROM size. Should be 16 KB. Found %d", loadedBanks * PAGE_SIZE);
                    result = false;
                }
            }
			else if (config.mem_model == MM_SPECTRUM128)
            {
			    if (loadedBanks != 2)
                {
                    MLOGERROR("Incorrect ROM size. Should be 32 KB. Found %d", loadedBanks * PAGE_SIZE);
                    result = false;
                }
            }
			else
			{
				// All other models have 64KB (4 banks) ROM chips
				if (loadedBanks != 4)
				{
					MLOGERROR("Incorrect ROM size. Should be 64 KB. Found %d", loadedBanks * PAGE_SIZE);
					result = false;
				}
			}

			if (result)
			{
				MLOGDEBUG("ROM successully loaded from file '%s'", romname.c_str());
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
	Memory& memory = *_context->pCore->GetMemory();

	// BASIC48 (SOS)
	string romPath = config.romSetSOSPath;
	bool result1 = LoadROM(rompath, memory.base_sos_rom);
	if (!result1)
	{
		MLOGERROR("Unable to load BASIC48 (SOS) ROM from file: '%s'", config.sos_rom_path);
	}

	// BASIC128
    romPath = config.romSet128Path;
	bool result2 = LoadROM(romPath, memory.base_128_rom);
	if (!result2)
	{
		MLOGERROR("Unable to load BASIC128 ROM from file: '%s'", config.zx128_rom_path);
	}

	// DOS (TR-DOS)
    romPath = config.romSetDOSPath;
	bool result3 = LoadROM(romPath, memory.base_dos_rom);
	if (!result3)
	{
		MLOGERROR("Unable to load DOS (TR-DOS) ROM from file: '%s'", config.dos_rom_path);
	}

	// Shadow (SYS)
    romPath = config.romSetSYSPath;
	bool result4 = LoadROM(romPath, memory.base_sys_rom);
	if (!result4)
	{
		MLOGERROR("Unable to load Shadow (SYS) ROM from file: '%s'", config.sys_rom_path);
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
		MLOGERROR("ROM::LoadROM - Empty ROM path supplied");
		return result;
	}

	std::string resolvedPath = FileHelper::NormalizePath(path);
	if (!FileHelper::FileExists(resolvedPath))
	{
		// Try to use as relative path if not found using original path
		string executablePath = FileHelper::GetExecutablePath();
		resolvedPath = FileHelper::PathCombine(executablePath, resolvedPath);

		if (!FileHelper::FileExists(resolvedPath))
		{
			MLOGERROR("ROM::LoadROM - file %s not found", FileHelper::PrintablePath(resolvedPath).c_str());
			return result;
		}
	}

	FILE* romfile = fopen(resolvedPath.c_str(), "rb");
	if (romfile)
	{
		size_t size = fread(bank, 1, max_banks * PAGE_SIZE, romfile);
		if (size && !(size & (PAGE_SIZE - 1)))
		{

			result = static_cast<uint16_t>(size / PAGE_SIZE);
		}
		else
		{
			MLOGERROR("ROM::LoadROM - Incorrect ROM file size. Expected: %d, found %d", max_banks * PAGE_SIZE, size);
		}

		fclose(romfile);
	}
	else
	{
		MLOGERROR("ROM::LoadROM - unable to read from file %s", FileHelper::PrintablePath(resolvedPath).c_str());
	}


	return result;
}

void ROM::CalculateSignatures()
{
	CONFIG& config = _context->config;
	Memory& memory = *_context->pMemory;

	if (_ROMBanksLoaded == 0)
	{
		MLOGERROR("ROM::CalculateSignatures - no ROM loaded. Unable to calculate ROM signatures");
		return;
	}

	MLOGINFO("ROM Banks info (as loaded):");
	for (int i = 0; i < _ROMBanksLoaded; i++)
    {
	    std::string signature = CalculateSignature(memory.ROMPageHostAddress(i), 0x4000);
        LOGINFO("  ROM page %d: %s", i, GetROMTitle(signature).c_str());
    }

    LOGINFO("ROM Banks info (as mapped):");
	if (memory.base_sos_rom)
    {
        std::string signature = CalculateSignature(memory.base_sos_rom, 0x4000);
        LOGINFO("  base_sos_rom: %s", GetROMTitle(signature).c_str());
    }

    if (memory.base_128_rom)
    {
        std::string signature = CalculateSignature(memory.base_128_rom, 0x4000);
        LOGINFO("  base_128_rom: %s", GetROMTitle(signature).c_str());
    }

    if (memory.base_dos_rom)
    {
        std::string signature = CalculateSignature(memory.base_dos_rom, 0x4000);
        LOGINFO("  base_dos_rom: %s", GetROMTitle(signature).c_str());
    }

    if (memory.base_sys_rom)
    {
        std::string signature = CalculateSignature(memory.base_sys_rom, 0x4000);
        LOGINFO("  base_sys_rom: %s", GetROMTitle(signature).c_str());
    }
}

string ROM::CalculateSignature(uint8_t* buffer, size_t length)
{
	string result;

	if (buffer == nullptr || length == 0)
	{
		MLOGERROR("ROM::CalculateSignature - buffer shouldn't be nullptr and length needs to be > 0");
		return result;
	}

	result = sha256().absorb(buffer, length).hexdigest();

	return result;
}

std::string ROM::GetROMTitle(std::string& signature)
{
    static const char* EMPTY_SIGNATURE = "Empty signature";
    static const char* UNKNOWN_ROM = "Unknown ROM";

    std::string result = EMPTY_SIGNATURE;

    if (signature.size() > 0)
    {
        if (key_exists(_signatures, signature))
        {
            result = _signatures[signature];
        }
        else
        {
            result = StringHelper::Format("%s, <%s>", UNKNOWN_ROM, signature.c_str());
        }
    }

    return result;
}