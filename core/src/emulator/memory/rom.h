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

typedef std::map<std::string, std::string> ROMSMap;
typedef std::pair<std::string, std::string> ROMSignature;

/// endregion </Structures and type>

class ROM
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_MEMORY;
    const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_ROM;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
	EmulatorContext* _context = nullptr;
	std::string _activeROMFile;
	uint8_t _ROMBanksLoaded = 0;

	ROMSMap _signatures;
	
	// Cached ROM titles by semantic type (populated by CalculateSignatures)
	std::string _sosRomTitle;    // 48K BASIC ROM
	std::string _128RomTitle;    // 128K editor/menu ROM
	std::string _dosRomTitle;    // TR-DOS ROM
	std::string _sysRomTitle;    // System/shadow ROM

	///endregion </Fields>

	/// region <Constructors / destructors>
public:
	ROM() = delete;		// Disable default constructor. C++ 11 feature
	ROM(EmulatorContext* context);
	virtual ~ROM();
    /// endregion </Constructors / destructors>

    std::string GetROMFilename();
	[[nodiscard]] bool LoadROM();

	[[nodiscard]] bool LoadROMSet();
	[[nodiscard]] uint16_t LoadROM(string& path, uint8_t* bank, uint16_t max_banks = 1);

	// Signature-related methods
public:
	/// @brief Digest every loaded ROM page and resolve the titles of the mapped ROM roles (48K, 128K, DOS, SYS)
	void CalculateSignatures();

	/// @brief Hex SHA-256 digest of a ROM page, served from the process-wide SignatureCache when the page was
	///        seen before (every Emulator::Init re-digests the same few images otherwise)
	/// @param buffer Page content
	/// @param length Page length in bytes
	/// @return Hex digest, or an empty string for a null/empty buffer
	std::string CalculateSignature(const uint8_t* buffer, size_t length);

	/// @brief Human-readable title of a known ROM image
	/// @param signature Hex SHA-256 digest as returned by CalculateSignature()
	/// @return Title from the known-ROM table, "Unknown ROM, <digest>" for an unlisted digest, "Empty signature" for ""
	std::string GetROMTitle(const std::string& signature);

	/// @brief Cached title of the mapped ROM role (48K, 128K, DOS, SYS) whose 16 KB page contains an address
	/// @param physicalAddress Host address inside a ROM page
	/// @return Title resolved by CalculateSignatures(), or an empty string when the address is in no mapped ROM page
	std::string GetROMTitleByAddress(const uint8_t* physicalAddress);
};

