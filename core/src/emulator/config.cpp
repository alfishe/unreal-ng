#include "stdafx.h"

#include "common/logger.h"

#include "config.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/platform.h"

// Ini file section names
const char* Config::misc = "MISC";
const char* Config::video = "VIDEO";
const char* Config::ula = "ULA";
const char* Config::beta128 = "Beta128";
const char* Config::leds = "LEDS";
const char* Config::sound = "SOUND";
const char* Config::input = "INPUT";
const char* Config::colors = "COLORS";
const char* Config::ay = "AY";
const char* Config::saa1099 = "SAA1099";
const char* Config::atm = "ATM";
const char* Config::hdd = "HDD";
const char* Config::rom = "ROM";
const char* Config::ngs = "NGS";
const char* Config::zc = "ZC";

Config::Config(EmulatorContext* context)
{
	_context = context;
}

Config::~Config()
{
	_context = nullptr;
}

const char* Config::GetDefaultConfig()
{
	return "unreal.ini";
}

bool Config::LoadConfig()
{
	bool result = false;

	// Use 'unreal.ini' file located in the same folder as executable by default
	wstring path = FileHelper::GetExecutablePath();
	if (!path.empty())
	{
		wstring configPath = FileHelper::PathCombine(path, GetDefaultConfig());

		if (LoadConfig(configPath))
		{

		}
		else
		{

		}
	}
	else
	{
		LOGERROR("Config::LoadConfig() - Unable to determine executable path");
		assert("Config::LoadConfig() - Unable to determine executable path");
	}

	return result;
}

bool Config::LoadConfig(wstring& filename)
{
	bool result = false;

	if (filename.empty())
	{
		LOGERROR("Config::LoadConfig - Empty config filename provided");
		return result;
	}

	if (!FileHelper::FileExists(filename))
	{
		LOGERROR("Config::LoadConfig - File '%s' does not exist", FileHelper::NormalizeFilePath(filename).c_str());	// FileHelper::NormalizeFilePath is mandatory since Logger works only with 'string' type and formatters
		return result;
	}

	_configFilePath = filename;

	// Use SimpleINI config file manager
	CSimpleIniA inimanager;
	inimanager.SetUnicode();
	SI_Error rc = inimanager.LoadFile(_configFilePath.c_str());
	if (rc == SI_OK)
	{
		LOGDEBUG("Config::LoadConfig - config '%s' successfully loaded to SimpleINI parser", FileHelper::NormalizeFilePath(_configFilePath).c_str());	// FileHelper::NormalizeFilePath is mandatory since Logger works only with 'string' type and formatters

		result = true;
	}
	else
	{
		LOGERROR("Config::LoadConfig - error during loading config '%s' by SimpleINI", FileHelper::NormalizeFilePath(_configFilePath).c_str());	// FileHelper::NormalizeFilePath is mandatory since Logger works only with 'string' type and formatters
	}

	result = ParseConfig(inimanager);

	return result;
}

bool Config::ParseConfig(CSimpleIniA& inimanager)
{
	bool result = false;

	CONFIG& config = _context->config;

	// Global settings
	char configVersion[50];
	CopyStringValue(inimanager.GetValue("*", "UNREAL", nullptr, false), configVersion, sizeof configVersion);	// Section with name "*" corresponds to global .ini file values (no group)

	// MISC section
	config.ConfirmExit = (uint8_t)inimanager.GetLongValue(misc, "ConfirmExit", 0);
	config.sleepidle = (uint8_t)inimanager.GetLongValue(misc, "ShareCPU", 0);

	// MISC::CMOS sub-section

	// MISC::ULA+ sub-section

	// MISC::TSConf sub-section

	// ROM section
	CopyStringValue(inimanager.GetValue(rom, "PENTAGON", nullptr, false), config.pent_rom_path, sizeof config.pent_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "ATM1", nullptr, false), config.atm1_rom_path, sizeof config.atm1_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "ATM2", nullptr, false), config.atm2_rom_path, sizeof config.atm2_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "ATM3", nullptr, false), config.atm3_rom_path, sizeof config.atm3_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "SCORP", nullptr, false), config.scorp_rom_path, sizeof config.scorp_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "PROFROM", nullptr, false), config.prof_rom_path, sizeof config.prof_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "GMX", nullptr, false), config.gmx_rom_path, sizeof config.gmx_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "PROFI", nullptr, false), config.profi_rom_path, sizeof config.profi_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "KAY", nullptr, false), config.kay_rom_path, sizeof config.kay_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "PLUS3", nullptr, false), config.plus3_rom_path, sizeof config.plus3_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "QUORUM", nullptr, false), config.quorum_rom_path, sizeof config.quorum_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "TSL", nullptr, false), config.tsl_rom_path, sizeof config.tsl_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "LSY", nullptr, false), config.lsy_rom_path, sizeof config.lsy_rom_path);
	CopyStringValue(inimanager.GetValue(rom, "PHOENIX", nullptr, false), config.phoenix_rom_path, sizeof config.phoenix_rom_path);

	// INPUT section

	return result;
}

bool Config::DetermineModel()
{
	bool result = false;

	return result;
}

unsigned Config::LoadROM(wstring& path, uint8_t* bank, uint16_t max_banks)
{
	unsigned result = 0;

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

			result = static_cast<unsigned>(size / 1024);
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

void Config::LoadROMSet(CONFIG* config, const char* romeset)
{

}

void Config::CopyStringValue(const char* src, char* dst, size_t dst_len)
{
	if (src != nullptr && dst != nullptr && dst_len > 0)
	{
		string value = stripComment(src);

		strncpy(dst, value.c_str(), dst_len);
	}
}

string Config::stripComment(const char* src)
{
	string result;

	if (src != nullptr && *src != '\0')
	{
		string strSource = src;

		// Strip comments
		size_t pos = strSource.find_first_of(';');
		if (pos != string::npos)
		{
			result = strSource.substr(0, pos);
		}

		// Trim right
		pos = result.find_last_not_of(' ');
		if (pos != string::npos)
		{
			result.erase(pos + 1);
		}
		else
			result.clear();			// Whole value is whitespace
	}

	return result;
}