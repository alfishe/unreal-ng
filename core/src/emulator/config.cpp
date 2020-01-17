#include "stdafx.h"

#include "common/logger.h"

#include "config.h"
#include "emulator/platform.h"

Config::Config()
{

}

Config::~Config()
{

}

void Config::LoadConfig(const char* filename)
{
	if (filename == nullptr || *filename == '\0')
	{
		LOGERROR("Config::LoadConfig - Empty config filename provided");
		return;
	}


}

unsigned Config::LoadROM(const char* path, uint8_t* bank, uint16_t max_banks)
{
	unsigned result = 0;

	if (path == nullptr || *path == '\0')
	{
		LOGERROR("Config::LoadROM - Empty ROM path supplied");

		memset(bank, 0xFF, max_banks * PAGE);
		return result;
	}

	FILE* romfile = fopen(path, "rb");
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