#include "stdafx.h"

#include "common/logger.h"

#include "config.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/platform.h"

Config::Config()
{

}

Config::~Config()
{

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
		LOGERROR("Config::LoadConfig - File '%s' does not exist", filename.c_str());
		return result;
	}

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