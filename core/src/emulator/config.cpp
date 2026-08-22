#include "stdafx.h"

#include "common/modulelogger.h"

#include "config.h"
#include "common/stringhelper.h"
#include "common/filehelper.h"
#include <filesystem>
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/memory/memory.h"
#include <cassert>
#include <array>
#include <algorithm>

#ifdef __linux__
	// Use ICU library for path conversion in SimpleINI parser
	#define SI_CONVERT_ICU
#endif

Config::Config(EmulatorContext* context)
{
	_context = context;
	_logger = context->pModuleLogger;
}

Config::~Config()
{
	_context = nullptr;
}

const char* Config::GetDefaultConfig()
{
	return "unreal.ini";
}

string Config::GetScreenshotsFolder()
{
	static string screenshotsPath;
	static bool initialized = false;
	
	if (!initialized)
	{
#ifdef __APPLE__
		// On macOS, check if we're running from a DMG or other read-only location
		std::string basePath = FileHelper::GetResourcesPath();
		std::string testPath = FileHelper::PathCombine(basePath, "/screenshots");
		
		// Try to create the directory to test if it's writable
		bool isWritable = false;
		try {
			if (!std::filesystem::exists(testPath)) {
				isWritable = std::filesystem::create_directories(testPath);
			} else {
				// Directory exists, check if it's writable by creating a test file
				std::string testFile = FileHelper::PathCombine(testPath, "/test.tmp");
				FILE* fp = fopen(testFile.c_str(), "w");
				if (fp) {
					fclose(fp);
					remove(testFile.c_str());
					isWritable = true;
				}
			}
		} catch (const std::exception&) {
			isWritable = false;
		}
		
		if (!isWritable) {
			// If not writable (e.g., running from DMG), use ~/Library/Application Support/UnrealNG/
			const char* homeDir = getenv("HOME");
			if (homeDir) {
				std::string dirPath = std::string(homeDir) + "/Library/Application Support/UnrealNG/screenshots";
				// Create the directory if it doesn't exist
				try {
					std::filesystem::create_directories(dirPath);
				} catch (const std::exception&) {
					// If we can't create the directory, fall back to temporary directory
					dirPath = "/tmp/UnrealNG/screenshots";
					std::filesystem::create_directories(dirPath);
				}
				screenshotsPath = dirPath;
			} else {
				// Fallback to temporary directory if HOME is not available
				screenshotsPath = "/tmp/UnrealNG/screenshots";
				std::filesystem::create_directories(screenshotsPath);
			}
		} else {
			// Location is writable, use it
			screenshotsPath = testPath;
		}
#else
		// On Windows and Linux, use the executable directory
		std::string basePath = FileHelper::GetExecutablePath();
		screenshotsPath = FileHelper::PathCombine(basePath, "/screenshots");
		
		// Create the directory if it doesn't exist
		try {
			std::filesystem::create_directories(screenshotsPath);
		} catch (const std::exception&) {
			// Ignore errors
		}
#endif
		initialized = true;
	}

	return screenshotsPath;
}

bool Config::LoadConfig()
{
	bool result = false;

	// First try to load config from executable directory
	std::string path = FileHelper::GetExecutablePath();
	if (!path.empty())
	{
        std::string configPath = FileHelper::PathCombine(path, GetDefaultConfig());
        std::string absoluteConfigPath = FileHelper::AbsolutePath(configPath);

		if (LoadConfig(absoluteConfigPath))
		{
			result = true;
		}
		else
		{
			// If not found in executable directory, try resources directory (especially for macOS app bundles)
			std::string resourcesPath = FileHelper::GetResourcesPath();
			if (!resourcesPath.empty() && resourcesPath != path) // Only try if different from executable path
			{
				std::string resourceConfigPath = FileHelper::PathCombine(resourcesPath, GetDefaultConfig());
				std::string absoluteResourceConfigPath = FileHelper::AbsolutePath(resourceConfigPath);

				if (LoadConfig(absoluteResourceConfigPath))
				{
					result = true;
				}
				else
				{
					MLOGERROR("Config::LoadConfig() - unable to process config file from executable or resources path");
				}
			}
			else
			{
				MLOGERROR("Config::LoadConfig() - unable to process config file");
			}
		}
	}
	else
	{
	    std::string error = "Config::LoadConfig() - Unable to determine executable path";
		MLOGERROR(error);
		throw std::logic_error(error);
	}

	return result;
}

bool Config::LoadConfig(string& filename)
{
	bool result = false;

	if (filename.empty())
	{
		MLOGERROR("Config::LoadConfig - Empty config filename provided");
		return result;
	}

	if (!FileHelper::FileExists(filename))
	{
		MLOGERROR("Config::LoadConfig - File '%s' does not exist", FileHelper::PrintablePath(filename).c_str());
		return result;
	}

	_configFilePath = filename;

	// Use SimpleINI config file manager
	CSimpleIniA inimanager;
	inimanager.SetUnicode();

	// Load and parse config file (internally within SimpleINI)
	SI_Error rc = inimanager.LoadFile(_configFilePath.c_str());
	if (rc == SI_OK)
	{
		MLOGDEBUG("Config::LoadConfig - config '%s' successfully loaded to SimpleINI parser", FileHelper::PrintablePath(_configFilePath).c_str());	// FileHelper::PrintablePath is mandatory since Logger works only with 'string' type and formatters

		result = true;
	}
	else
	{
        MLOGDEBUG("Config::LoadConfig - error during loading config '%s' by SimpleINI", FileHelper::PrintablePath(_configFilePath).c_str());	// FileHelper::PrintablePath is mandatory since Logger works only with 'string' type and formatters
	}

	// Populate configuration fields from config file data
	result = ParseConfig(inimanager);

	return result;
}

bool Config::ParseConfig(CSimpleIniA& inimanager)
{
	bool result = false;

	CONFIG& config = _context->config;

	char line[FILENAME_MAX];

	// Global settings
	char configVersion[50];
	CopyStringValue(inimanager.GetValue("*", "UNREAL", nullptr, nullptr), configVersion, sizeof configVersion);	// Section with name "*" corresponds to global .ini file values (no group)

	// MISC section
	config.ConfirmExit = (uint8_t)inimanager.GetLongValue(misc, "ConfirmExit", 0);
	config.sleepidle = (uint8_t)inimanager.GetLongValue(misc, "ShareCPU", 0);

	config.reset_rom = RM_SOS;
	CopyStringValue(inimanager.GetValue(misc, "RESET", nullptr, nullptr), line, sizeof line); // What ROM bank to set active during reset
	if (StringHelper::CompareCaseInsensitive(line, "DOS", 3) == 0)
	{
		config.reset_rom = RM_DOS;
	}
	else if (StringHelper::CompareCaseInsensitive(line, "MENU", 4) == 0 ||
	         StringHelper::CompareCaseInsensitive(line, "BASIC128", 8) == 0 ||
	         StringHelper::CompareCaseInsensitive(line, "128", 3) == 0)
	{
		config.reset_rom = RM_128;
	}
	else if (StringHelper::CompareCaseInsensitive(line, "SYS", 3) == 0)
	{
		config.reset_rom = RM_SYS;
	}
	// Note: "BASIC" (or any other value) defaults to RM_SOS (48K ROM)

	// MISC::CMOS sub-section

	// MISC::ULA+ sub-section

	// MISC::TSConf sub-section

    // ROM set
    config.romSetName = inimanager.GetValue(rom, "ROMSET");

    if (!config.romSetName.empty())
    {
        config.use_romset = true;

        config.romSet128Path = inimanager.GetValue(config.romSetName.c_str(), romset_128);
        config.romSetSOSPath = inimanager.GetValue(config.romSetName.c_str(), romset_sos);
        config.romSetDOSPath = inimanager.GetValue(config.romSetName.c_str(), romset_dos);
        config.romSetSYSPath = inimanager.GetValue(config.romSetName.c_str(), romset_sys);
    }

    // Populate rom files for each platform
    CopyStringValue(inimanager.GetValue(rom, "PENTAGON", nullptr, nullptr), config.pent_rom_path, sizeof config.pent_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "48k", nullptr, nullptr), config.zx48_rom_path, sizeof config.zx48_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "128k", nullptr, nullptr), config.zx128_rom_path, sizeof config.zx128_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PLUS3", nullptr, nullptr), config.plus3_rom_path, sizeof config.plus3_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM1", nullptr, nullptr), config.atm1_rom_path, sizeof config.atm1_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM2", nullptr, nullptr), config.atm2_rom_path, sizeof config.atm2_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM3", nullptr, nullptr), config.atm3_rom_path, sizeof config.atm3_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "SCORP", nullptr, nullptr), config.scorp_rom_path, sizeof config.scorp_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PROFROM", nullptr, nullptr), config.prof_rom_path, sizeof config.prof_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "GMX", nullptr, nullptr), config.gmx_rom_path, sizeof config.gmx_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PROFI", nullptr, nullptr), config.profi_rom_path, sizeof config.profi_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "KAY", nullptr, nullptr), config.kay_rom_path, sizeof config.kay_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "QUORUM", nullptr, nullptr), config.quorum_rom_path, sizeof config.quorum_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "TSL", nullptr, nullptr), config.tsl_rom_path, sizeof config.tsl_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "LSY", nullptr, nullptr), config.lsy_rom_path, sizeof config.lsy_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PHOENIX", nullptr, nullptr), config.phoenix_rom_path, sizeof config.phoenix_rom_path);

	// ULA section (video signal timings)
	config.intfq = (uint8_t)inimanager.GetLongValue(ula, "int", 50);
	config.intstart = (unsigned)inimanager.GetLongValue(ula, "intstart", 0);
	config.intlen = (unsigned)inimanager.GetLongValue(ula, "intlen", 32);
	config.t_line = (unsigned)inimanager.GetLongValue(ula, "line", 224);		// CPU cycles per video line
	config.frame = (unsigned)inimanager.GetLongValue(ula, "frame", 71680);		// ZX48/128: 69888; Pentagon: 71680; ScorpionZS256: 69888;
	config.frame_duration_us = CalculateFrameDurationUs(config.frame);			// Pentagon: 20480us (48.83 FPS); ZX48/128: 19968us
	
	// Speed multiplier: 1x (default), 2x, 4x, 8x, 16x
		config.speed_multiplier = (uint8_t)inimanager.GetLongValue(ula, "speedmultiplier", 1);
		{
			static const std::array<uint8_t, 5> allowedMultipliers = { 1, 2, 4, 8, 16 };
			if (std::find(allowedMultipliers.begin(), allowedMultipliers.end(), config.speed_multiplier) == allowedMultipliers.end())
			{
				config.speed_multiplier = 1;  // Default to 1x if invalid value
			}
		}

	config.border_4T = (unsigned)inimanager.GetLongValue(ula, "4TBorder", 0);
	config.even_M1 = (unsigned)inimanager.GetLongValue(ula, "EvenM1", 0);
	config.floatbus = (unsigned)inimanager.GetLongValue(ula, "FloatBus", 0);
	config.floatdos = (unsigned)inimanager.GetLongValue(ula, "FloatDOS", 0);
	// Note: the original UnrealSpeccy "PortFF" option (simplified always-attribute
	// floating bus model) is intentionally not ported - UlaContention implements the
	// full architecture-aware floating bus (pixel/attr per fetch phase) instead.

	// Beta128 section
	config.trdos_present = inimanager.GetLongValue(beta128, "beta128", 1) ? true : false;
	config.trdos_traps = inimanager.GetLongValue(beta128, "Traps", 1) ? true : false;
	config.wd93_nodelay = inimanager.GetLongValue(beta128, "Fast", 0) ? true : false;  // Default: off (realistic WD1793 timing)
	config.trdos_interleave = (uint8_t)inimanager.GetLongValue(beta128, "IL", 1) - 1;
	if (config.trdos_interleave > 2)
		config.trdos_interleave = 0;
	config.fdd_noise = inimanager.GetLongValue(beta128, "Noise", 0) ? true : false;
	CopyStringValue(inimanager.GetValue(beta128, "BOOT", nullptr, nullptr), config.appendboot, sizeof config.appendboot);

	// INPUT section

	// HDD section

	// SOUND section
	config.sound.covoxFB = (int)inimanager.GetLongValue(sound, "CovoxFB", 0);
	config.sound.covoxDD = (int)inimanager.GetLongValue(sound, "CovoxDD", 0);

	// Core audio rate: auto | 44100 | 48000 | 88200 | 96000 | 176400 | 192000
	// (multirate plan phase 6). 0 = auto. Unsupported values fall back to auto.
	{
		long rate = inimanager.GetLongValue(sound, "CoreRate", 0);  // "auto" parses as 0
		switch (rate)
		{
			case 0:
			case 44100:
			case 48000:
			case 88200:
			case 96000:
			case 176400:
			case 192000:
				config.sound.coreRate = (unsigned)rate;
				break;
			default:
				MLOGWARNING("Config: unsupported [SOUND] CoreRate=%ld, using auto", rate);
				config.sound.coreRate = 0;
				break;
		}
	}

	// VIDEO section
	// A/V sync video delay: auto (-1) = match the audio path latency
	// (~2 frames); 0 = lowest input latency (audio trails by the ring depth)
	{
		long delay = inimanager.GetLongValue(video, "AVSyncDelayFrames", -1);  // "auto" parses as 0 - use -1 default
		config.videoPresentDelayFrames = (delay >= -1 && delay <= 3) ? (int)delay : -1;
	}

	// Emulated model
	CopyStringValue(inimanager.GetValue(misc, "HIMEM", "PENTAGON", nullptr), line, sizeof line);
	config.ramsize = inimanager.GetLongValue(misc, "RamSize", 128, nullptr);
	
	// Make sure we're emulating valid model & configuration
	if (DetermineModel(line, config.ramsize))
	{
		// Apply hardware-accurate INT timing defaults based on the selected model
		ApplyModelTimingDefaults(config);

		result = true;
	}
	else
	{
	    std::string message = StringHelper::Format("Unable to recognize ZX-Spectrum model selected in config. Model: %s, mem: %d", line, config.ramsize);
		MLOGERROR(message.c_str());
	}

	return result;
}

bool Config::DetermineModel(const char* model, uint32_t ramsize)
{
	bool result = false;
	uint32_t maxMemory = 0;
	const char* fullModelName = nullptr;

	CONFIG& config = _context->config;

	// Null check for input parameter
	if (model == nullptr)
	{
		return false;
	}

	// Search for model in lookup dictionary
	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		// Null check before calling strlen to prevent crash
		if (mem_model[i].ShortName != nullptr)
		{
			if (StringHelper::CompareCaseInsensitive(model, mem_model[i].ShortName, strlen(mem_model[i].ShortName)) == 0)
			{
				config.mem_model = mem_model[i].Model;
				maxMemory = mem_model[i].AvailRAMs;
				fullModelName = mem_model[i].FullName;

				result = true;
				break;
			}
		}
	}

	// Check if config requested RAM size allowed for the selected model
	if (result)
	{
		if (ramsize & maxMemory) // Bit in mem_model.AvailRAMs will be set if available. All possible RAM size combinations [128:4096] are correspondent to bits. If 16Kb or 48Kb are planned - extended check logic required
		{
			MLOGINFO("Model '%s' (HIMEM=%s) with RAM Size: %dKb selected", fullModelName, model, ramsize);
			result = true;
		}
		else
		{
			result = false;

			string availableRAM;
			MLOGERROR("Requested RAM size: %dKb is not available for the model with HIMEM='%s' selected. Available size(s): %s", ramsize, model, availableRAM.c_str());
		}
	}
	else
	{
		MLOGERROR("Unknown model specified in config with HIMEM=%s and ramsize=%d", model, ramsize);
	}

	return result;
}

std::vector<TMemModel> Config::GetAvailableModels()
{
	std::vector<TMemModel> models;
	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		models.push_back(mem_model[i]);
	}
	return models;
}

const TMemModel* Config::FindModelByShortName(const std::string& shortName)
{
	// Handle empty or invalid input
	if (shortName.empty())
	{
		return nullptr;
	}

	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		// Null check before calling strlen to prevent crash
		if (mem_model[i].ShortName != nullptr)
		{
			if (StringHelper::CompareCaseInsensitive(shortName.c_str(), mem_model[i].ShortName, strlen(mem_model[i].ShortName)) == 0)
			{
				return &mem_model[i];
			}
		}
	}
	return nullptr;
}

void Config::CopyStringValue(const char* src, char* dst, size_t dst_len)
{
	if (src != nullptr && dst != nullptr && dst_len > 0)
	{
		std::string value = StripComment(src);

        size_t len = std::min(value.length(), dst_len - 1);
        memcpy(dst, value.c_str(), len);
        dst[len] = '\0';
	}
}

std::string Config::StripComment(const char* src)
{
    std::string result;

	if (src != nullptr && *src != '\0')
	{
        std::string strSource = src;

		// Strip comments
		size_t pos = strSource.find_first_of(';');
		if (pos != string::npos)
		{
			result = strSource.substr(0, pos);
		}
		else
		{
			result = strSource;
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

string Config::PrintModelAvailableRAM(uint32_t availRAM)
{
	stringstream ss;

	// 128Kb to 4096Kb (Bits 7 to 12)
	for (int i = 7; i <= 12; i++)
	{
		if (availRAM & (1 << i))
		{
			ss << (1 << i) << "KB; ";
		}
	}

	return ss.str();
}

void Config::ApplyModelTimingDefaults(CONFIG& config, bool canonicalGeometry)
{
    // Save user-specified INI values (if non-default)
    unsigned userIntstart = config.intstart;
    unsigned userIntlen   = config.intlen;

    // Apply hardware-accurate defaults per model.
    // Values derived from MiSTer HDL ula.sv INT generation logic:
    //   Pentagon: INT at vc=239, hc=326; converted to our raster geometry (see doc 18):
    //     paper first pixel at T=17944 (line 80 + 24T, see ScreenZX::CreateTstateLUT),
    //     real-Pentagon INT-to-paper distance = 17989T (Unreal Speccy conf.paper calibration,
    //     INT at frame wrap) => intstart = 17944 - 17989 + 71680 = 71635
    //   ZX-48K:   INT at vc=248, hc=4   → emulator t-state 1794  (2.6% through frame)
    //   ZX-128K:  INT at vc=248, hc=8   → emulator t-state 2056  (2.9% through frame)
    switch (config.mem_model)
    {
        case MM_PENTAGON:
            // MiSTer vc=239/hc=326 maps to 71619 in our frame, but our raster window places
            // paper 24T into the line (framebuffer x = T_in_line*2, paper x∈[48,304)).
            // +16T aligns INT-to-paper to the real-Pentagon 17989T distance. See doc 18.
            config.intstart = 71635;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM48:
            config.intstart = 1794;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM128:
        case MM_PLUS3:
            config.intstart = 2056;
            config.intlen   = 36;   // ZX-128K ULA has 72-HC INT = 36 T-states
            break;

        default:
            // Leave existing values for TSConf, ATM, Scorpion, Profi, etc.
            break;
    }

    // Allow INI override if user explicitly set non-default values
    // (i.e. not the old placeholder values 13/32)
    if (userIntstart != 0 && userIntstart != 13)
        config.intstart = userIntstart;
    if (userIntlen != 0 && userIntlen != 32)
        config.intlen = userIntlen;

    // Programmatically-requested models also get canonical frame geometry: the
    // INI in use typically describes a different machine (e.g. the global
    // Pentagon ini) so its frame/line values must not leak into the requested
    // model. INI-driven runs (per-model config dirs) pass false and are untouched.
    if (canonicalGeometry)
    {
        switch (config.mem_model)
        {
            case MM_SPECTRUM48:
                config.frame = 69888;   // 224 * 312
                config.t_line = 224;
                config.intstart = 1794;
                config.intlen = 32;
                break;
            case MM_SPECTRUM128:
            case MM_PLUS3:
                config.frame = 70908;   // 228 * 311
                config.t_line = 228;
                config.intstart = 2056;
                config.intlen = 36;
                break;
            case MM_PENTAGON:
                config.frame = 71680;   // 224 * 320
                config.t_line = 224;
                break;
            default:
                break;
        }
    }

    // ATM timing is ALWAYS applied (not conditional on canonicalGeometry) because
    // the ini file's Frame=99880 is a 7MHz preset label, not base-clock timing.
    // ATM uses ZX-compatible 312-line PAL at base clock; turbo is runtime state.
    if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
    {
        config.frame = 69888;   // 312 lines * 224 T/line (ZX-compatible PAL)
        config.t_line = 224;
        config.intstart = 2056; // ZX128-compatible INT timing
        config.intlen = 32;
    }

    // Invariant: frame_duration_us must be recomputed with config.frame
    // All models use the standard formula now (ATM uses ZX-compatible timing)
    if (config.frame_duration_us == 0 || config.mem_model == MM_SPECTRUM48 ||
        config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PLUS3 ||
        config.mem_model == MM_PENTAGON || config.mem_model == MM_ATM710 ||
        config.mem_model == MM_ATM3)
    {
        config.frame_duration_us = CalculateFrameDurationUs(config.frame);
    }

    MLOGINFO("ApplyModelTimingDefaults: model=%d intstart=%u intlen=%u frame=%u line=%u",
             config.mem_model, config.intstart, config.intlen, config.frame, config.t_line);
}