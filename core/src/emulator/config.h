#pragma once
#include "stdafx.h"

#include "3rdparty/simpleini/simpleini.h"
#include "emulator/emulatorcontext.h"
#include <string>

struct CONFIG;
class ModuleLogger;

class Config
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions for Module/Submodule>

	// Ini file section names
private:
	static const char* misc;
	static const char* video;
	static const char* ula;
	static const char* beta128;
	static const char* leds;
	static const char* sound;
	static const char* input;
	static const char* colors;
	static const char* ay;
	static const char* saa1099;
	static const char* atm;
	static const char* hdd;
	static const char* rom;
	static const char* ngs;
	static const char* zc;


	const struct TMemModel mem_model[N_MM_MODELS] =
	{
		{ "Pentagon", "PENTAGON",                MM_PENTAGON, 128,  RAM_128 | RAM_256 | RAM_512 | RAM_1024 },
        { "ZX-Spectrum 48k", "48K",              MM_SPECTRUM48, 48, RAM_48 },
        { "ZX-Spectrum 128k", "128k",            MM_SPECTRUM128, 128, RAM_128 },
        { "ZX-Spectrum +3", "PLUS3",             MM_PLUS3, 128,  RAM_128 },
		{ "TS-Config", "TSL",                    MM_TSL, 4096, RAM_4096 },
		{ "ZX-Evo", "ATM3",                      MM_ATM3, 4096, RAM_4096 },
		{ "ATM-Turbo 2+ v7.10", "ATM710",        MM_ATM710, 1024, RAM_128 | RAM_256 | RAM_512 | RAM_1024 },
		{ "ATM-Turbo v4.50", "ATM450",           MM_ATM450, 512,  RAM_512 | RAM_1024 },
		{ "Profi", "PROFI",                      MM_PROFI, 1024, RAM_1024 },
		{ "ZS Scorpion", "SCORPION",             MM_SCORP, 256,  RAM_256 | RAM_1024 },
		{ "ZS Scorpion + PROF ROM", "PROFSCORP", MM_PROFSCORP, 256,  RAM_256 | RAM_1024 },
		{ "ZS Scorpion + GMX", "GMX",			 MM_GMX, 2048,  RAM_2048 },
		{ "Nemo's KAY", "KAY",                   MM_KAY, 256,  RAM_256 | RAM_1024 },
		{ "Quorum", "QUORUM",                    MM_QUORUM, 1024, RAM_128 | RAM_1024 },
		{ "Orel' BK-08 (LSY)", "LSY256",         MM_LSY256, 256, RAM_256 },
		{ "ZXM-Phoenix v1.0", "PHOENIX",         MM_PHOENIX, 1024, RAM_1024 | RAM_2048 },
	};

protected:
	EmulatorContext* _context;
    std::string _configFilePath;

public:
	static const char* GetDefaultConfig();

public:
	Config() = delete;	// Disable default constructor. C++ 11 feature
	Config(EmulatorContext* context);
	virtual ~Config();

public:
    std::string GetScreenshotsFolder();

public:
	[[nodiscard]] bool LoadConfig();
	[[nodiscard]] bool LoadConfig(std::string& filename);
	[[nodiscard]] bool ParseConfig(CSimpleIniA& inimanager);

	[[nodiscard]] bool DetermineModel(const char* model, uint32_t ramsize);

	// Helper methods
protected:
	void CopyStringValue(const char* src, char* dst, size_t dst_len);
    std::string StripComment(const char* src);
    std::string PrintModelAvailableRAM(uint32_t availRAM);
};