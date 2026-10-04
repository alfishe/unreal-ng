#include "stdafx.h"

#include "common/modulelogger.h"

#include "config.h"
#include "common/stringhelper.h"
#include "common/filehelper.h"
#include <filesystem>
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/atm/atmgeometry.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/io/serial/comportspec.h"
#include "emulator/io/serial/hayesmodempeer.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/serial/uart16550.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"
#include <cassert>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

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
		std::string dirPath = FileHelper::PathCombine(FileHelper::GetWritablePath(), "screenshots");
		try {
			std::filesystem::create_directories(FileHelper::ToFsPath(dirPath));
		} catch (const std::exception&) {
			// Ignore directory creation errors
		}
		screenshotsPath = dirPath;
		initialized = true;
	}

	return screenshotsPath;
}

bool Config::LoadConfig(const std::string& modelConfigName)
{
	if (modelConfigName.empty())
	{
		MLOGERROR("Config::LoadConfig - model config name is mandatory");
		return false;
	}

	// Model config always lives in configs/<modelConfigName>/unreal.ini
	std::string relativePath = FileHelper::PathCombine("configs", modelConfigName);
	relativePath = FileHelper::PathCombine(relativePath, GetDefaultConfig());

	// Search order: executable directory, then application resources (macOS app bundle)
	std::string searchedPaths;
	for (const std::string& basePath : { FileHelper::GetExecutablePath(), FileHelper::GetResourcesPath() })
	{
		if (basePath.empty())
			continue;

		std::string configPath = FileHelper::AbsolutePath(FileHelper::PathCombine(basePath, relativePath));
		if (FileHelper::FileExists(configPath))
		{
			return LoadConfigFile(configPath);
		}

		if (!searchedPaths.empty())
			searchedPaths += ", ";
		searchedPaths += FileHelper::PrintablePath(configPath);
	}

	MLOGERROR("Config::LoadConfig - no config for model '%s'; searched: %s",
	          modelConfigName.c_str(), searchedPaths.c_str());
	return false;
}

bool Config::LoadConfigFile(const std::string& filename)
{
	bool result = false;

	if (filename.empty())
	{
		MLOGERROR("Config::LoadConfigFile - Empty config filename provided");
		return result;
	}

	if (!FileHelper::FileExists(filename))
	{
		MLOGERROR("Config::LoadConfigFile - File '%s' does not exist", FileHelper::PrintablePath(filename).c_str());
		return result;
	}

	MLOGINFO("Config::LoadConfigFile - Loading config '%s'", FileHelper::PrintablePath(filename).c_str());

	_configFilePath = filename;

	// Load and parse config file
	IniFile inimanager;
	if (inimanager.LoadFile(_configFilePath))
	{
		MLOGDEBUG("Config::LoadConfigFile - config '%s' successfully loaded to INI parser", FileHelper::PrintablePath(_configFilePath).c_str());	// FileHelper::PrintablePath is mandatory since Logger works only with 'string' type and formatters

		result = true;
	}
	else
	{
        MLOGDEBUG("Config::LoadConfigFile - error during loading config '%s'", FileHelper::PrintablePath(_configFilePath).c_str());	// FileHelper::PrintablePath is mandatory since Logger works only with 'string' type and formatters
	}

	// Populate configuration fields from config file data
	result = ParseConfig(inimanager);

	return result;
}

bool Config::ParseIdeScheme(const char* value, IDE_SCHEME& scheme)
{
	static const std::pair<const char*, IDE_SCHEME> schemes[] = {
		{"NONE", IDE_NONE}, {"ATM", IDE_ATM}, {"NEMO", IDE_NEMO}, {"NEMO-A8", IDE_NEMO_A8},
		{"NEMO-DIVIDE", IDE_NEMO_DIVIDE}, {"SMUC", IDE_SMUC}, {"PROFI", IDE_PROFI}, {"DIVIDE", IDE_DIVIDE},
		{"SPRINTER", IDE_SPRINTER}};
	const std::string name = StringHelper::ToUpper(std::string(StringHelper::Trim(value ? value : "")));
	for (const auto& [text, id] : schemes)
	{
		if (name == text)
		{
			scheme = id;
			return true;
		}
	}
	return false;
}

const char* Config::IdeSchemeName(IDE_SCHEME scheme)
{
	switch (scheme)
	{
		case IDE_NONE: return "NONE";
		case IDE_ATM: return "ATM";
		case IDE_NEMO: return "NEMO";
		case IDE_NEMO_A8: return "NEMO-A8";
		case IDE_NEMO_DIVIDE: return "NEMO-DIVIDE";
		case IDE_SMUC: return "SMUC";
		case IDE_PROFI: return "PROFI";
		case IDE_DIVIDE: return "DIVIDE";
		case IDE_SPRINTER: return "SPRINTER";
	}
	return "?";
}

bool Config::ParseRamPowerOn(const std::string& value, RamPowerOn& mode)
{
	const std::string name = StringHelper::ToUpper(std::string(StringHelper::Trim(value)));
	if (name == "RANDOM")
	{
		mode = RamPowerOn::Random;
		return true;
	}
	if (name == "ZERO")
	{
		mode = RamPowerOn::Zero;
		return true;
	}
	return false;
}

const char* Config::RamPowerOnName(RamPowerOn mode)
{
	return mode == RamPowerOn::Zero ? "zero" : "random";
}

std::function<void(CONFIG&)> Config::RamPowerOnOverride(RamPowerOn mode)
{
	return [mode](CONFIG& config) { config.ramPowerOn = mode; };
}

bool Config::ParseEvoFpgaVariant(const char* value)
{
	if (value == nullptr || value[0] == '\0')
		return false;

	const size_t length = strlen(value);
	if (length == strlen("legacy") && StringHelper::CompareCaseInsensitive(value, "legacy", length) == 0)
		return true;
	if (length == strlen("trdemu") && StringHelper::CompareCaseInsensitive(value, "trdemu", length) == 0)
		return false;

	LOGWARNING("Config: unknown [EVO] Fpga='%s' - using the current trdemu BaseConf", value);
	return false;
}

bool Config::ParseConfig(IniFile& inimanager)
{
	bool result = false;

	CONFIG& config = _context->config;

	char line[FILENAME_MAX];

	// Global settings
	char configVersion[50];
	CopyStringValue(inimanager.GetValue("*", "UNREAL", nullptr), configVersion, sizeof configVersion);	// Section with name "*" corresponds to global .ini file values (no group)

	// MISC section
	config.ConfirmExit = (uint8_t)inimanager.GetLongValue(misc, "ConfirmExit", 0);
	config.sleepidle = (uint8_t)inimanager.GetLongValue(misc, "ShareCPU", 0);

	// Map INI 'RESET=' setting to initial ROM bank (ROMModeEnum).
	// Uses a table lookup to cleanly support aliases across configs (e.g. "128", "MENU", "BASIC128" -> RM_128; "BASIC", "48" -> RM_SOS).
	// Default: RM_SOS (48K BASIC ROM).
	struct ResetRomMapping
	{
		const char* name;
		uint8_t mode;
	};

	static constexpr ResetRomMapping resetMappings[] = {
		{ "DOS", RM_DOS },
		{ "MENU", RM_128 },
		{ "128", RM_128 },       // Introduced in commit 79bd9291 as default for Pentagon/Spectrum128
		{ "BASIC128", RM_128 },
		{ "BASIC", RM_SOS },
		{ "48", RM_SOS },
		{ "SYS", RM_SYS }
	};

	config.reset_rom = RM_SOS;
	CopyStringValue(inimanager.GetValue(misc, "RESET", nullptr), line, sizeof line); // What ROM bank to set active during reset
	for (const auto& mapping : resetMappings)
	{
		if (StringHelper::CompareCaseInsensitive(line, mapping.name, strlen(mapping.name)) == 0)
		{
			config.reset_rom = mapping.mode;
			break;
		}
	}

	// MISC::CMOS sub-section

	// MISC::ULA+ sub-section

    // ROM set. GetValue returns NULL when the [ROM] section or the key is
    // absent (a valid minimal config may carry neither) - a NULL const char*
    // assigned to std::string is UB, so map it to the empty name explicitly.
    const char* romSetName = inimanager.GetValue(rom, "ROMSET");
    config.romSetName = romSetName != nullptr ? romSetName : "";

    if (!config.romSetName.empty())
    {
        config.use_romset = true;

        config.romSet128Path = inimanager.GetValue(config.romSetName.c_str(), romset_128);
        config.romSetSOSPath = inimanager.GetValue(config.romSetName.c_str(), romset_sos);
        config.romSetDOSPath = inimanager.GetValue(config.romSetName.c_str(), romset_dos);
        config.romSetSYSPath = inimanager.GetValue(config.romSetName.c_str(), romset_sys);
    }

    // Populate rom files for each platform
    CopyStringValue(inimanager.GetValue(rom, "PENTAGON", nullptr), config.pent_rom_path, sizeof config.pent_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "48k", nullptr), config.zx48_rom_path, sizeof config.zx48_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "128k", nullptr), config.zx128_rom_path, sizeof config.zx128_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PLUS3", nullptr), config.plus3_rom_path, sizeof config.plus3_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PLUS2", nullptr), config.plus2_rom_path, sizeof config.plus2_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PLUS2A", nullptr), config.plus2a_rom_path, sizeof config.plus2a_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM1", nullptr), config.atm1_rom_path, sizeof config.atm1_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM2", nullptr), config.atm2_rom_path, sizeof config.atm2_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "ATM3", nullptr), config.atm3_rom_path, sizeof config.atm3_rom_path);

	// EVO section (ZX-Evo BaseConf): FPGA variant the ROM image expects
	config.atm.evo_legacy_fpga = ParseEvoFpgaVariant(inimanager.GetValue("EVO", "Fpga", nullptr)) ? 1 : 0;
	{
		const char* avr = inimanager.GetValue("EVO", "Avr", nullptr);
		Uart16550::AvrFirmware firmware = Uart16550::kLatestAvr;
		if (!Uart16550::ParseAvrFirmware(avr, firmware))
			MLOGWARNING("Config: unknown [EVO] Avr=%s, BASECONF (the latest NedoPC firmware) used", avr);
		config.atm.evo_avr = static_cast<uint8_t>(firmware);
	}
	{
		// [ATM] Kbc=: the keyboard controller of ATM Turbo 2+ boards (its real firmware on an MCS-51 core)
		const char* kbc = inimanager.GetValue("ATM", "Kbc", nullptr);
		Atm2Kbc::Firmware firmware = Atm2Kbc::kDefaultFirmware;
		// The Unreal Speccy key [INPUT] ATMKBD=0 (no controller) counts when Kbc= is not given
		if (!kbc && inimanager.GetValue(input, "ATMKBD", nullptr) && inimanager.GetLongValue(input, "ATMKBD", 1) == 0)
			firmware = Atm2Kbc::Firmware::None;
		else if (!Atm2Kbc::ParseFirmware(kbc, firmware))
			MLOGWARNING("Config: unknown [ATM] Kbc=%s, V41 used (NONE | V22-7 | V22-11 | V22-12 | V31-7 | V31-11 | V32-7 | "
			            "V32-11 | V40 | V41)", kbc);
		config.atm.kbc_firmware = static_cast<uint8_t>(firmware);
		config.atm.kbc_rom_path[0] = '\0';
		CopyStringValue(inimanager.GetValue(rom, "ATM2KBC", nullptr), config.atm.kbc_rom_path, sizeof config.atm.kbc_rom_path);
	}
	config.atm.evo_nvram_path[0] = '\0';  // a config without the key must not inherit a previous path
	CopyStringValue(inimanager.GetValue("EVO", "NvramFile", nullptr), config.atm.evo_nvram_path, sizeof config.atm.evo_nvram_path);
	{
		// TS-Conf: the TS-BIOS settings without an NVRAM file (boot-and-storage-notes.md §1)
		const std::string preset = inimanager.GetValue("EVO", "TsBiosNvram", "SDBOOT");
		if (preset == "SETUP" || preset == "setup")
			config.atm.ts_bios_sd_boot = 0;
		else
		{
			config.atm.ts_bios_sd_boot = 1;
			if (preset != "SDBOOT" && preset != "sdboot")
				MLOGWARNING("Config: unknown [EVO] TsBiosNvram='%s' - using SDBOOT (SDBOOT | SETUP)", preset.c_str());
		}
	}

	// PROFI section: battery-backed RTC cells
	config.profi_nvram_path[0] = '\0';  // a config without the key must not inherit a previous path
	CopyStringValue(inimanager.GetValue("PROFI", "NvramFile", nullptr), config.profi_nvram_path, sizeof config.profi_nvram_path);
	{
		// The board's video sync PROM (2026-10-01-profi-v3-v5 design section 5.1)
		const char* syncProm = inimanager.GetValue("PROFI", "SyncProm", nullptr);
		ProfiSyncProm prom = ProfiSyncProm::Default;
		if (!ParseProfiSyncProm(syncProm, prom))
			MLOGWARNING("Config: unknown [PROFI] SyncProm=%s, the board's own sync PROM used", syncProm);
		config.profi_sync_prom = static_cast<uint8_t>(prom);
	}
	// The boards' wait states and turbo switch (2026-10-01-profi-v3-v5 design section 6)
	config.profi_wait_phase = static_cast<uint8_t>(inimanager.GetLongValue("PROFI", "WaitPhase", 0) & 0x03);
	{
		const char* waitConfig = inimanager.GetValue("PROFI", "WaitConfig", "profi");
		config.profi_wait_pentagon = 0;
		if (waitConfig && StringHelper::CompareCaseInsensitive(waitConfig, "pentagon", 8) == 0)
			config.profi_wait_pentagon = 1;
		else if (waitConfig && StringHelper::CompareCaseInsensitive(waitConfig, "profi", 5) != 0)
			MLOGWARNING("Config: unknown [PROFI] WaitConfig=%s, profi (the video WAIT on) used", waitConfig);
	}
	config.profi_rom_wait = static_cast<uint8_t>(inimanager.GetLongValue("PROFI", "RomWait", 0) ? 1 : 0);
	config.profi_turbo = static_cast<uint8_t>(inimanager.GetLongValue("PROFI", "Turbo", 0) ? 1 : 0);
	config.profi_zq3_mhz = ProfiClampZq3(inimanager.GetLongValue("PROFI", "ZQ3MHz", kProfiZq3DefaultMHz));
	{
		const char* ayClock = inimanager.GetValue("PROFI", "AyClock", "old");
		config.profi_ay_clock_new = 0;
		if (ayClock && StringHelper::CompareCaseInsensitive(ayClock, "new", 3) == 0)
			config.profi_ay_clock_new = 1;
		else if (ayClock && StringHelper::CompareCaseInsensitive(ayClock, "old", 3) != 0)
			MLOGWARNING("Config: unknown [PROFI] AyClock=%s, old (1.5 MHz in hi-res) used", ayClock);
	}
	config.profi_cpm = static_cast<uint8_t>(inimanager.GetLongValue("PROFI", "CpmSwitch", 0) ? 1 : 0);
	{
		const char* decode = inimanager.GetValue("PROFI", "DffdDecode", "emulators");
		config.profi_dffd_decode = 0;
		if (decode && StringHelper::CompareCaseInsensitive(decode, "v50", 3) == 0 && decode[3] == '\0')
			config.profi_dffd_decode = 1;
		else if (decode && StringHelper::CompareCaseInsensitive(decode, "v506", 4) == 0)
			config.profi_dffd_decode = 2;
		else if (decode && StringHelper::CompareCaseInsensitive(decode, "emulators", 9) != 0)
			MLOGWARNING("Config: unknown [PROFI] DffdDecode=%s, emulators (A15=1, A13=0, A1=0) used", decode);
	}
	{
		// The extended port map (docs/inprogress/2026-10-01-profi-v3-v5/software-zoo.md): cpm | sys | v003
		const char* ext = inimanager.GetValue("PROFI", "ExtPorts", "cpm");
		config.profi_ext_ports = 0;
		if (ext && StringHelper::CompareCaseInsensitive(ext, "sys", 3) == 0 && ext[3] == '\0')
			config.profi_ext_ports = 1;
		else if (ext && StringHelper::CompareCaseInsensitive(ext, "v003", 4) == 0 && ext[4] == '\0')
			config.profi_ext_ports = 2;
		else if (ext && !(StringHelper::CompareCaseInsensitive(ext, "cpm", 3) == 0 && ext[3] == '\0'))
			MLOGWARNING("Config: unknown [PROFI] ExtPorts=%s, cpm (CP/M and ROM14) used", ext);
	}

	{
		// The keyboard on the connector X9 / KEYB (design section "Keyboard"): matrix | xt | xttable
		const char* keyboard = inimanager.GetValue("PROFI", "Keyboard", nullptr);
		ProfiKeyboard parsed = ProfiKeyboard::Default;
		if (!ParseProfiKeyboard(keyboard, parsed))
			MLOGWARNING("Config: unknown [PROFI] Keyboard=%s, the board's own used (v5: xt, v3: matrix; MATRIX | XT | XTTABLE)",
			            keyboard);
		config.profi_keyboard = static_cast<uint8_t>(parsed);
	}

	// SPRINTER section (Sprinter tdd-integration §1.1): start mode, front-panel turbo, CMOS image
	config.sprinter.fast_start = static_cast<uint8_t>(inimanager.GetLongValue("SPRINTER", "FastStart", 0) ? 1 : 0);
	config.sprinter.turbo_allowed = static_cast<uint8_t>(inimanager.GetLongValue("SPRINTER", "Turbo", 1) ? 1 : 0);
	config.sprinter.accel_int_suspend = static_cast<uint8_t>(inimanager.GetLongValue("SPRINTER", "AccelIntSuspend", 0) ? 1 : 0);
	config.sprinter.cmos_path[0] = '\0';  // a config without the key must not inherit a previous path
	CopyStringValue(inimanager.GetValue("SPRINTER", "CmosFile", nullptr), config.sprinter.cmos_path, sizeof config.sprinter.cmos_path);

	// ISA section (Sprinter ISA tdd §5, network tdd §12): what the two ISA-8 slots hold. A bad value keeps
	// the default for that key (logged); a kind this build does not have is refused later, with the reason
	// in the slot report - the machine always starts
	config.sprinter.isa = sprinterisa::DefaultConfig();
	for (int n = 0; n < sprinterisa::kSlots; n++)
	{
		sprinterisa::SlotConfig& slot = config.sprinter.isa.slot[n];
		const std::string prefix = "Slot" + std::to_string(n + 1);
		if (const char* v = inimanager.GetValue("ISA", prefix.c_str(), nullptr))
		{
			sprinterisa::CardKind kind;
			if (sprinterisa::ParseKind(v, kind))
				slot.kind = static_cast<uint8_t>(kind);
			else
				MLOGWARNING("Config: unknown [ISA] %s=%s (NONE | ZXBUS | RAM | NE2000 | EL3C509B | SPRINTERESP | MODEM | DUAL16552), %s kept",
				            prefix.c_str(), v, sprinterisa::KindName(static_cast<sprinterisa::CardKind>(slot.kind)));
		}
		const auto slotKind = static_cast<sprinterisa::CardKind>(slot.kind);
		if (slotKind == sprinterisa::CardKind::El3c509b)
			slot.chip = static_cast<uint8_t>(sprinterisa::El3Chip::Tpo);   // the NE2000 default's number means nothing here
		if (const char* v = inimanager.GetValue("ISA", (prefix + "Chip").c_str(), nullptr))
		{
			if (slotKind == sprinterisa::CardKind::El3c509b)
			{
				sprinterisa::El3Chip chip;
				if (sprinterisa::ParseEl3Chip(v, chip))
					slot.chip = static_cast<uint8_t>(chip);
				else
					MLOGWARNING("Config: unknown [ISA] %sChip=%s (EL3C509B: TPO | TP), TPO used", prefix.c_str(), v);
			}
			else
			{
				sprinterisa::Ne2000Chip chip;
				if (sprinterisa::ParseChip(v, chip))
					slot.chip = static_cast<uint8_t>(chip);
				else
					MLOGWARNING("Config: unknown [ISA] %sChip=%s (RTL8019AS | UM9003 | NE1000), RTL8019AS used", prefix.c_str(), v);
			}
		}
		const auto kind = static_cast<sprinterisa::CardKind>(slot.kind);
		if (kind == sprinterisa::CardKind::Modem)
		{
			slot.base = sprinterisa::kModemDefaultBase;
			slot.irq = sprinterisa::kModemDefaultIrq;
		}
		else if (kind == sprinterisa::CardKind::Dual16552)
			slot.irq = sprinterisa::kSerialDefaultIrqA;
		if (const char* v = inimanager.GetValue("ISA", (prefix + "Base").c_str(), nullptr))
		{
			uint16_t base = 0;
			if (kind == sprinterisa::CardKind::Modem)
			{
				if (sprinterisa::ParseModemBase(v, base))
					slot.base = base;
				else
					MLOGWARNING("Config: [ISA] %sBase=%s: the modem's COM base is 0x3F8 | 0x2F8 | 0x3E8 | 0x2E8, 0x%03X kept",
					            prefix.c_str(), v, slot.base);
			}
			else if (sprinterisa::ParseSlotBase(v, slotKind, base))
				slot.base = base;
			else
				MLOGWARNING("Config: [ISA] %sBase=%s: #200..#3E0 in steps of #%02X, #%03X kept", prefix.c_str(), v,
				            slotKind == sprinterisa::CardKind::El3c509b ? 0x10 : 0x20, slot.base);
		}
		slot.irq = static_cast<uint8_t>(inimanager.GetLongValue("ISA", (prefix + "Irq").c_str(), slot.irq) & 0x0F);
		if (kind == sprinterisa::CardKind::Modem && !sprinterisa::ValidModemIrq(slot.irq))
		{
			MLOGWARNING("Config: [ISA] %sIrq=%u: the modem's IRQ is 2 | 3 | 4 | 5 | 7, 4 used", prefix.c_str(), slot.irq);
			slot.irq = sprinterisa::kModemDefaultIrq;
		}
		if (kind == sprinterisa::CardKind::Dual16552 && !sprinterisa::ValidSerialJumper(0, slot.irq))
		{
			MLOGWARNING("Config: [ISA] %sIrq=%u: SprinterSerial's J5 joins COM1 to IRQ 3 or 2 (0 = open), 3 used",
			            prefix.c_str(), slot.irq);
			slot.irq = sprinterisa::kSerialDefaultIrqA;
		}
		slot.irqB = static_cast<uint8_t>(inimanager.GetLongValue("ISA", (prefix + "IrqB").c_str(), 0) & 0x0F);
		if (!sprinterisa::ValidSerialJumper(1, slot.irqB))
		{
			MLOGWARNING("Config: [ISA] %sIrqB=%u: SprinterSerial's J6 joins COM2 to IRQ 4 or 2 (0 = open), open used",
			            prefix.c_str(), slot.irqB);
			slot.irqB = 0;
		}
		if (const char* v = inimanager.GetValue("ISA", (prefix + "Decode").c_str(), nullptr))
		{
			const std::string d = StringHelper::ToUpper(std::string(v));
			if (d == "FULL" || d == "PARTIAL")
				slot.partialDecode = d == "PARTIAL" ? 1 : 0;
			else
				MLOGWARNING("Config: [ISA] %sDecode=%s: FULL | PARTIAL, FULL used", prefix.c_str(), v);
		}
		if (const char* v = inimanager.GetValue("ISA", (prefix + "Mac").c_str(), nullptr))
		{
			if (!sprinterisa::ParseMac(v, slot))
				MLOGWARNING("Config: [ISA] %sMac=%s: auto or aa:bb:cc:dd:ee:ff (a station address), auto used", prefix.c_str(), v);
		}
		// A UART card's line (SPRINTERESP: what its 16550 is wired to; ComPortSpec, default AT = the ESP-12F)
		if (const char* v = inimanager.GetValue("ISA", (prefix + "Peer").c_str(), nullptr))
		{
			ComPortSpec spec;
			std::string error;
			if (ComPortSpec::Parse(v, spec, error) && spec.ToString().size() < sizeof(slot.peer))
				std::snprintf(slot.peer, sizeof(slot.peer), "%s", spec.ToString().c_str());
			else
				MLOGWARNING("Config: [ISA] %sPeer=%s: %s - the card's default used", prefix.c_str(), v, error.c_str());
		}
		// SprinterSerial's COM2 (ComPortSpec, default NONE)
		if (const char* v = inimanager.GetValue("ISA", (prefix + "PeerB").c_str(), nullptr))
		{
			ComPortSpec spec;
			std::string error;
			if (ComPortSpec::Parse(v, spec, error) && spec.ToString().size() < sizeof(slot.peerB))
				std::snprintf(slot.peerB, sizeof(slot.peerB), "%s", spec.ToString().c_str());
			else
				MLOGWARNING("Config: [ISA] %sPeerB=%s: %s - NONE used", prefix.c_str(), v, error.c_str());
		}
	}

	// [ZC] (the Z-Controller SD card) is read by MediaConfig with the rest of the media set
    CopyStringValue(inimanager.GetValue(rom, "SCORP", nullptr), config.scorp_rom_path, sizeof config.scorp_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PROFROM", nullptr), config.prof_rom_path, sizeof config.prof_rom_path);
    // The shipped spectrum3 unreal.ini carries "rom\\scorp_prof401.ROM:0" - without
    // stripping, the ":0" leaks into the path and the ROM file lookup fails
    StripProfRomQuadrantSuffix(config.prof_rom_path, sizeof config.prof_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "GMX", nullptr), config.gmx_rom_path, sizeof config.gmx_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PROFI", nullptr), config.profi_rom_path, sizeof config.profi_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PROFI3", nullptr), config.profi3_rom_path, sizeof config.profi3_rom_path);
    config.profi_xt_rom_path[0] = '\0';  // empty = the reconstructed image rom/profixt/profi-xt-v1.27.rom
    CopyStringValue(inimanager.GetValue(rom, "PROFIXT", nullptr), config.profi_xt_rom_path, sizeof config.profi_xt_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "KAY", nullptr), config.kay_rom_path, sizeof config.kay_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "QUORUM", nullptr), config.quorum_rom_path, sizeof config.quorum_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "TSL", nullptr), config.tsl_rom_path, sizeof config.tsl_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "LSY", nullptr), config.lsy_rom_path, sizeof config.lsy_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "PHOENIX", nullptr), config.phoenix_rom_path, sizeof config.phoenix_rom_path);
    CopyStringValue(inimanager.GetValue(rom, "SPRINTER", nullptr), config.sprinter_rom_path, sizeof config.sprinter_rom_path);
#ifdef MOD_GSZ80
    // General Sound firmware ROM ([ROM] GS). Defaults to the shipped 32 KB
    // gs105a.rom (data/rom) so a fitted card always has firmware even when a
    // hand-written config omits the key; bootGS.rom is the 512 KB NeoGS flash
    // image - the LLE card would only use its first 32 KB (with a warning).
    // Relative paths resolve against the resources dir in SoundChip_GeneralSound::loadROM.
    CopyStringValue(inimanager.GetValue(rom, "GS", "rom/gs105a.rom"), config.gs_rom_path, sizeof config.gs_rom_path);
#endif

	// ULA section (video signal timings)
	config.intfq = (uint8_t)inimanager.GetLongValue(ula, "int", 50);
	config.intstart = (unsigned)inimanager.GetLongValue(ula, "intstart", 0);
	config.intlen = (unsigned)inimanager.GetLongValue(ula, "intlen", 32);
	config.t_line = (unsigned)inimanager.GetLongValue(ula, "line", 224);		// CPU cycles per video line
	config.frame = (unsigned)inimanager.GetLongValue(ula, "frame", 71680);		// ZX48/128: 69888; Pentagon: 71680; ScorpionZS256: 69888;
	config.frame_duration_us = CalculateFrameDurationUs(config.frame);			// Pentagon: 20480us (48.83 FPS); ZX48/128: 19968us
	config.profi_monochrome = (uint8_t)(inimanager.GetLongValue(ula, "ProfiMonochrome", 0) ? 1 : 0);
	
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
	{
		// Turbo VG (WD1793 clocked at 2 MHz while positioning): absent = the machine's own policy
		const long turboVg = inimanager.GetLongValue(beta128, "TurboVG", -1);
		config.fdcTurboVg = static_cast<int8_t>(turboVg < 0 ? -1 : (turboVg ? 1 : 0));
	}
	config.trdos_interleave = (uint8_t)inimanager.GetLongValue(beta128, "IL", 1) - 1;
	if (config.trdos_interleave > 2)
		config.trdos_interleave = 0;
	config.fdd_noise = inimanager.GetLongValue(beta128, "Noise", 0) ? true : false;
	CopyStringValue(inimanager.GetValue(beta128, "BOOT", nullptr), config.appendboot, sizeof config.appendboot);

	// [INPUT] HostKeyboard=: where the host keyboard goes (the ZX matrix, the PS/2 controller, both)
	{
		config.input.hostKeyboard[0] = '\0';
		CopyStringValue(inimanager.GetValue(input, "HostKeyboard", nullptr), config.input.hostKeyboard, sizeof config.input.hostKeyboard);
		HostKeyboardRoute route;
		if (!Keyboard::ParseHostRoute(config.input.hostKeyboard, route))
		{
			MLOGWARNING("Config: unknown [INPUT] HostKeyboard=%s, AUTO used (AUTO | MATRIX | PS2 | BOTH)", config.input.hostKeyboard);
			config.input.hostKeyboard[0] = '\0';
		}
	}

	// INPUT section - Kempston Mouse (design §7). Legacy Unreal Speccy keys:
	//   Mouse=NONE|KEMPSTON|AY   Wheel=NONE|KEMPSTON|KEYBOARD   SwapMouse=0|1   MouseScale=-3..3
	{
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(input, "Mouse", nullptr), line, sizeof line);
		config.input.mouseConfigured = line[0] != '\0';
		config.input.mouse = MOUSE_TYPE_KEMPSTON;
		if (StringHelper::CompareCaseInsensitive(line, "NONE", strlen("NONE")) == 0)
			config.input.mouse = MOUSE_TYPE_NONE;
		else if (StringHelper::CompareCaseInsensitive(line, "AY", strlen("AY")) == 0)
		{
			MLOGWARNING("Config: [INPUT] Mouse=AY is not emulated, no mouse fitted");
			config.input.mouse = MOUSE_TYPE_NONE;
		}
		else if (line[0] != '\0' && StringHelper::CompareCaseInsensitive(line, "KEMPSTON", strlen("KEMPSTON")) != 0)
			MLOGWARNING("Config: unsupported [INPUT] Mouse='%s', using KEMPSTON", line);

		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(input, "Wheel", nullptr), line, sizeof line);
		config.input.mousewheel = MOUSE_WHEEL_NONE;
		if (StringHelper::CompareCaseInsensitive(line, "KEMPSTON", strlen("KEMPSTON")) == 0)
			config.input.mousewheel = MOUSE_WHEEL_KEMPSTON;
		else if (StringHelper::CompareCaseInsensitive(line, "KEYBOARD", strlen("KEYBOARD")) == 0)
		{
			MLOGWARNING("Config: [INPUT] Wheel=KEYBOARD is not implemented, wheel disabled");
			config.input.mousewheel = MOUSE_WHEEL_NONE;
		}

		config.input.mouseswap = inimanager.GetLongValue(input, "SwapMouse", 0) ? 1 : 0;

		long scale = inimanager.GetLongValue(input, "MouseScale", 0);
		if (scale < -3 || scale > 3)
		{
			MLOGWARNING("Config: [INPUT] MouseScale=%ld out of range -3..3, using 0", scale);
			scale = 0;
		}
		config.input.mousescale = static_cast<char>(scale);

		config.input.mouseReleaseKey[0] = '\0';
		CopyStringValue(inimanager.GetValue(input, "MouseReleaseKey", nullptr), config.input.mouseReleaseKey,
		                sizeof config.input.mouseReleaseKey);
	}

	// INPUT section - Kempston joystick (joystick TDD J4, J6):
	//   Joystick=KEMPSTON|NONE (default KEMPSTON)
	//   JoystickKeys=up:kp8,down:kp2,left:kp4,right:kp6,fire:kp0 (absent: these defaults; empty: no host keys)
	{
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(input, "Joystick", nullptr), line, sizeof line);
		config.input.joystickConfigured = line[0] != '\0';
		config.input.joystick = 1;
		if (StringHelper::CompareCaseInsensitive(line, "NONE", strlen("NONE")) == 0)
			config.input.joystick = 0;
		else if (line[0] != '\0' && StringHelper::CompareCaseInsensitive(line, "KEMPSTON", strlen("KEMPSTON")) != 0)
			MLOGWARNING("Config: unsupported [INPUT] Joystick='%s', using KEMPSTON", line);

		const char* keys = inimanager.GetValue(input, "JoystickKeys", nullptr);
		config.input.joystickKeysConfigured = keys != nullptr;
		config.input.joystickKeys[0] = '\0';
		if (keys)
			CopyStringValue(keys, config.input.joystickKeys, sizeof config.input.joystickKeys);
	}

	// HDD section: the machine's IDE board and how its units are set up. The
	// images (Image0/1, HD0RO/1RO) are media: MediaConfig reads them
	{
		config.ide_scheme = IDE_NONE;
		if (const char* scheme = inimanager.GetValue(hdd, "Scheme", nullptr); scheme && !ParseIdeScheme(scheme, config.ide_scheme))
			MLOGWARNING("Config: [HDD] Scheme=%s is unknown: no IDE", scheme);
		// TS-Conf only: the FPGA stalls the Z80 for an IDE bus cycle (hardware-spec §8.3)
		config.ide_stall = inimanager.GetLongValue(hdd, "IdeStall", 0) != 0 ? 1 : 0;
		// Units 0-1: ide0 master / slave; units 2-3: ide1 (the Sprinter's second channel)
		static const char* const kUnitSlots[4] = {"ide0.master", "ide0.slave", "ide1.master", "ide1.slave"};
		for (int unit = 0; unit < 4; unit++)
		{
			IDE_CONFIG& ide = config.ide[unit];
			ide = IDE_CONFIG{};
			const std::string n = std::to_string(unit);
			if (const char* chs = inimanager.GetValue(hdd, ("CHS" + n).c_str(), nullptr))
			{
				unsigned c = 0, h = 0, s = 0;
				if (std::sscanf(chs, "%u/%u/%u", &c, &h, &s) == 3 && h <= 16 && s <= 255)
				{
					ide.c = c;
					ide.h = h;
					ide.s = s;
				}
				else
					MLOGWARNING("Config: [HDD] CHS%d=%s: expected C/H/S (heads up to 16)", unit, chs);
			}
			// A CD drive: CDn=1, or the unit's configured image is a CD image (an ISO, a CUE sheet)
			const char* cd = inimanager.GetValue(hdd, ("CD" + n).c_str(), nullptr);
			const char* image = inimanager.GetValue("MEDIA", kUnitSlots[unit], nullptr);
			if (!image || !*image)
				image = inimanager.GetValue(hdd, ("Image" + n).c_str(), nullptr);
			const std::string extension = image ? StringHelper::ToLower(FileHelper::GetFileExtension(image)) : std::string();
			const bool cdImage = extension == "iso" || extension == "cue";
			ide.cd = ((cd && std::atoi(cd) != 0) || cdImage) ? 1 : 0;
		}
	}

	// SOUND section
	config.sound.covoxFB = (int)inimanager.GetLongValue(sound, "CovoxFB", 0);
	config.sound.covoxDD = (int)inimanager.GetLongValue(sound, "CovoxDD", 0);
	config.sound.sd = (int)inimanager.GetLongValue(sound, "SD", 0);

	// Core audio rate: auto | 44100 | 48000 | 88200 | 96000 | 176400 | 192000
	// (multirate plan phase 6). 0 = auto. Decides the core rate ONLY while
	// no audio device is attached (headless runs - recordings and analyzers
	// at a chosen rate); a connected device always outranks it at runtime
	// (SoundManager::targetCoreRate priority chain). Unsupported values
	// fall back to auto.
	{
		long rate = inimanager.GetLongValue(sound, "CoreRate", 0);  // "auto" parses as 0
		if (rate == 0 || IsSupportedCoreRate(static_cast<uint32_t>(rate)))
		{
			config.sound.coreRate = (unsigned)rate;
		}
		else
		{
			MLOGWARNING("Config: unsupported [SOUND] CoreRate=%ld, using auto", rate);
			config.sound.coreRate = 0;
		}
	}

	// MoonSound (ZXM-MoonSound / YMF278B / OPL4): legacy enable + volume keys.
	// Card options live in the [MOONSOUND] section below.
	config.sound.moonsound = (int)inimanager.GetLongValue(sound, "MoonSound", 0);
	{
		long vol = inimanager.GetLongValue(sound, "MoonSoundVol", 8192);
		if (vol < 0 || vol > 8192)
		{
			MLOGWARNING("Config: [SOUND] MoonSoundVol=%ld out of range 0..8192, clamping", vol);
			vol = std::clamp(vol, 0L, 8192L);
		}
		config.sound.moonsound_vol = (int)vol;
	}

	// MOONSOUND section (card options; enable/volume are the [SOUND] keys above).
	// Wave ROM path: heritage unreal.ini ships it as [ROM] MOONSOUND=rom\opl4\YRW801...,
	// so read that key first; an explicit [MOONSOUND] WaveRom overrides it (6.2).
	CopyStringValue(inimanager.GetValue(rom, "MOONSOUND", nullptr), config.moonsound.waveRom, sizeof config.moonsound.waveRom);
	CopyStringValue(inimanager.GetValue(moonsound, "WaveRom", nullptr), config.moonsound.waveRom, sizeof config.moonsound.waveRom);
	{
		long ramKb = inimanager.GetLongValue(moonsound, "RamSizeKb", 1024);
		if (ramKb < 0 || ramKb > 1024)
		{
			MLOGWARNING("Config: [MOONSOUND] RamSizeKb=%ld out of range 0..1024, clamping", ramKb);
			ramKb = std::clamp(ramKb, 0L, 1024L);
		}
		config.moonsound.ramSizeKb = (unsigned)ramKb;
	}

	// RenderMode: hifi | authentic (default hifi — band-limited FM; authentic
	// is the HoldDrop reducer, opt-in until a hardware recording confirms it)
	line[0] = '\0';
	CopyStringValue(inimanager.GetValue(moonsound, "RenderMode", nullptr), line, sizeof line);
	config.moonsound.renderMode = (StringHelper::CompareCaseInsensitive(line, "authentic", strlen("authentic")) == 0) ? 0 : 1;

	// Quality: reference | highfidelity (default reference)
	line[0] = '\0';
	CopyStringValue(inimanager.GetValue(moonsound, "Quality", nullptr), line, sizeof line);
	config.moonsound.quality = (StringHelper::CompareCaseInsensitive(line, "highfidelity", strlen("highfidelity")) == 0) ? 1 : 0;

	// Punch: off | pcm | both (default off)
	line[0] = '\0';
	CopyStringValue(inimanager.GetValue(moonsound, "Punch", nullptr), line, sizeof line);
	config.moonsound.punch = (StringHelper::CompareCaseInsensitive(line, "pcm", strlen("pcm")) == 0) ? 1
	                      : (StringHelper::CompareCaseInsensitive(line, "both", strlen("both")) == 0) ? 2 : 0;

	// BoardAnalog: 0 | 1 (default 0)
	config.moonsound.boardAnalog = (inimanager.GetLongValue(moonsound, "BoardAnalog", 0) != 0) ? 1 : 0;

	// TurboSound slot device kind (TSFM design §3.1): AY (legacy two-AY pair,
	// default), FM (TSFM), Single (one AY: the chip-switch values #FE / #FF
	// select no register, as on a lone AY) or None (no sound chip fitted). Unknown values warn and fall back to AY; a missing
	// key keeps the default. The legacy [AY] Chip/Scheme keys are NOT honoured:
	// every shipped ini carries Chip=YM2203 and nothing ever parsed them, so
	// honouring them now would silently switch every machine to TSFM.
	{
		// Explicit default first: a missing key must reset to AY even when the
		// struct holds FM from a previous parse of another file.
		config.sound.turboSoundKind = TurboSoundKind::AY;
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(sound, "TurboSound", nullptr), line, sizeof line);
		if (StringHelper::CompareCaseInsensitive(line, "AY", strlen("AY")) == 0)
		{
			config.sound.turboSoundKind = TurboSoundKind::AY;
		}
		else if (StringHelper::CompareCaseInsensitive(line, "FM", strlen("FM")) == 0)
		{
			config.sound.turboSoundKind = TurboSoundKind::FM;
		}
		else if (StringHelper::CompareCaseInsensitive(line, "None", strlen("None")) == 0)
		{
			config.sound.turboSoundKind = TurboSoundKind::None;
		}
		else if (StringHelper::CompareCaseInsensitive(line, "Single", strlen("Single")) == 0)
		{
			config.sound.turboSoundKind = TurboSoundKind::Single;
		}
		else if (line[0] != '\0')
		{
			MLOGWARNING("Config: unsupported [SOUND] TurboSound='%s', using AY", line);
			config.sound.turboSoundKind = TurboSoundKind::AY;
		}
	}

	// FM loudness trim in dB relative to the hardware-derived default (0 = default)
	config.sound.tsfmFmTrimDb = inimanager.GetDoubleValue(sound, "TSFM_FmTrimDb", 0.0);

	// AY / SSG tone voicing (FilterVoicing profile ID): headphones (default:
	// classic bass + soft highs), classic (the pre-45812176 bass balance),
	// flat (hardware line out), warm (softer bass and highs), tv (TV
	// speaker) or small_speaker; alias
	// legacy = classic. Hidden (untuned) profiles are rejected like unknown
	// values: warn and keep the default. A missing key resets to the default
	// even when the struct holds another value from a previous parse
	{
		config.sound.ayVoicing = FilterVoicing::DEFAULT_PRESET;
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(sound, "AYVoicing", nullptr), line, sizeof line);
		if (line[0] != '\0')
		{
			FilterVoicing::Preset preset = FilterVoicing::DEFAULT_PRESET;
			if (FilterVoicing::parsePreset(line, preset))
				config.sound.ayVoicing = preset;
			else
				MLOGWARNING("Config: unsupported [SOUND] AYVoicing='%s', using %s", line,
				            FilterVoicing::presetId(FilterVoicing::DEFAULT_PRESET));
		}
	}

	// General Sound emulation kind ([SOUND] GSType, GS design §5.1):
	// Z80 = LLE coprocessor card, LW/LIGHT = lightweight in-tree mod player
	// (docs/inprogress/2026-09-19-general-sound), BASS = legacy
	// upstream HLE spelling kept as a deprecated alias of LW (no BASS library
	// is linked), NGS = NeoGS FPGA card (SoundChip_NeoGS, neogs-tdd.md),
	// NONE = no GS card. A missing key keeps NONE;
	// unknown values warn and fall back to NONE.
	{
		// Explicit default first: a missing key must reset to NONE even when
		// the struct holds Z80 from a previous parse of another file.
		config.sound.gsTypeKind = GSTypeKind::NONE;
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(sound, "GSType", nullptr), line, sizeof line);
		if (StringHelper::CompareCaseInsensitive(line, "Z80", strlen("Z80")) == 0)
		{
			config.sound.gsTypeKind = GSTypeKind::Z80;
		}
		else if (StringHelper::CompareCaseInsensitive(line, "LW", strlen("LW")) == 0 ||
		         StringHelper::CompareCaseInsensitive(line, "LIGHT", strlen("LIGHT")) == 0)
		{
			config.sound.gsTypeKind = GSTypeKind::LW;
		}
		else if (StringHelper::CompareCaseInsensitive(line, "BASS", strlen("BASS")) == 0)
		{
			// Upstream HLE spelling: same intent as LW (host-command-driven
			// mod player), implemented in-tree without the BASS library
			config.sound.gsTypeKind = GSTypeKind::LW;
			MLOGWARNING("Config: [SOUND] GSType=BASS is deprecated, using the in-tree lightweight card (GSType=LW)");
		}
		else if (StringHelper::CompareCaseInsensitive(line, "NGS", strlen("NGS")) == 0)
		{
			config.sound.gsTypeKind = GSTypeKind::NGS;
		}
		else if (line[0] != '\0' && StringHelper::CompareCaseInsensitive(line, "NONE", strlen("NONE")) != 0)
		{
			MLOGWARNING("Config: unsupported [SOUND] GSType='%s', using NONE", line);
		}
	}

	// GS volume on the shared 0-8192 ini scale (shipped GSVol=8000, same
	// domain as BeeperVol) and the reset-coupling flag: GSReset=1 makes the
	// ZX reset reinitialize the card too (Unreal: "if (gsreset) reset_gs()"),
	// GSReset=0 (the legacy default) keeps it running - separate subsystem
	// with its own #33 reset line
	config.sound.gs_vol = (int)inimanager.GetLongValue(sound, "GSVol", 8000);
	config.sound.gsreset = (uint8_t)inimanager.GetLongValue(sound, "GSReset", 0);
#ifdef MOD_GSZ80
	// Classic GS card RAM geometry ([SOUND] GSRamSize): 128 KB stock (the
	// default - the resulting ~0.3 s POST keeps scorpion-family fastdisk
	// boots past their 0x7E idle-signature probe, see verification BUG-6),
	// 256/512 KB expansion cards for software that requires them (Nether
	// Earth GS loads a 283 KB module and needs 512). Values outside 128-512
	// clamp to the nearest card size with a warning.
	{
		bool gsRamParsed = false;
		long gsRamKB = inimanager.GetLongValue(sound, "GSRamSize", 128, &gsRamParsed);
		const char* rawGsRam = inimanager.GetValue(sound, "GSRamSize", nullptr);
		if (rawGsRam != nullptr && rawGsRam[0] != '\0' && !gsRamParsed)
		{
			// IniFile strips inline comments with a backward scan (values may
			// legitimately contain ';'), so a comment carrying a SECOND ';' survives
			// the strip and the numeric conversion rejects the whole value. That
			// shipped as a silent 128 KB fallback on the GSRamSize=512 configs
			// (Nether Earth GS got a 128 KB card, its 283 KB module wrapped the
			// card and the music never played) - never let it hide behind the
			// default again.
			MLOGWARNING("Config: [SOUND] GSRamSize='%s' is not a plain number, using %ld (inline comments must not contain a second ';')",
			            rawGsRam, gsRamKB);
		}
		if (gsRamKB < 128 || gsRamKB > 512)
		{
			MLOGWARNING("Config: [SOUND] GSRamSize=%ld out of range (128-512), clamped", gsRamKB);
			gsRamKB = gsRamKB < 128 ? 128 : 512;
		}
		config.sound.gsRamKB = (unsigned)gsRamKB;
	}
#endif
#ifdef MOD_GSZ80
	// NeoGS card ([NGS] section, neogs-tdd.md §6). The classic GS card has
	// its own fixed geometry ([SOUND] GSRamSize), so nothing here reaches it
	// (verification BUG-6). SDCARD is the original UnrealSpeccy key, kept as
	// an alias of SDCardImage so existing configs load.
	{
		NeoGSConfig& ngsConfig = config.ngs;
		ngsConfig = NeoGSConfig{};

		auto choice = [&](const char* key, const char* fallback, std::initializer_list<const char*> names) -> int
		{
			line[0] = '\0';
			CopyStringValue(inimanager.GetValue(ngs, key, fallback), line, sizeof line);
			int index = 0;
			for (const char* name : names)
			{
				if (StringHelper::CompareCaseInsensitive(line, name, strlen(name)) == 0 && strlen(line) == strlen(name))
					return index;
				index++;
			}
			MLOGWARNING("Config: unsupported [NGS] %s='%s', using %s", key, line, fallback);
			index = 0;
			for (const char* name : names)
			{
				if (StringHelper::CompareCaseInsensitive(fallback, name, strlen(name)) == 0)
					return index;
				index++;
			}
			return 0;
		};

		const char* flash = inimanager.GetValue(ngs, "Flash", nullptr);
		if (flash && flash[0])
			CopyStringValue(flash, ngsConfig.flashPath, sizeof ngsConfig.flashPath);
		ngsConfig.flashId = choice("FlashId", "st", {"st", "amd"}) == 1 ? NeoGSConfig::FlashId::AMD : NeoGSConfig::FlashId::ST;
		ngsConfig.fpga = choice("Fpga", "current", {"current", "d"}) == 1 ? NeoGSConfig::Fpga::D : NeoGSConfig::Fpga::Current;

		// 2 MB and 4 MB boards only; anything else snaps to the nearer one
		long ramKB = inimanager.GetLongValue(ngs, "RamSize", 4096);
		ngsConfig.ramKB = ramKB <= 3072 ? 2048u : 4096u;
		if (ramKB != 2048 && ramKB != 4096)
			MLOGWARNING("Config: [NGS] RamSize=%ld is not a NeoGS size (2048 | 4096), using %u", ramKB, ngsConfig.ramKB);

		ngsConfig.boot = choice("Boot", "loader", {"loader", "direct"}) == 1 ? NeoGSConfig::Boot::Direct : NeoGSConfig::Boot::Loader;
		ngsConfig.bootDelayMs = static_cast<unsigned>(std::clamp<long>(inimanager.GetLongValue(ngs, "BootDelayMs", 0), 0, 10000));

		CopyStringValue(inimanager.GetValue(ngs, "SDCardImage", nullptr), ngsConfig.sdCardPath, sizeof ngsConfig.sdCardPath);
		if (!ngsConfig.sdCardPath[0])
			CopyStringValue(inimanager.GetValue(ngs, "SDCARD", nullptr), ngsConfig.sdCardPath, sizeof ngsConfig.sdCardPath);
		static constexpr NeoGSConfig::SDType sdTypes[] = {NeoGSConfig::SDType::Auto, NeoGSConfig::SDType::SDSC, NeoGSConfig::SDType::SDHC};
		ngsConfig.sdType = sdTypes[choice("SDType", "auto", {"auto", "sdsc", "sdhc"})];
		ngsConfig.sdWriteProtect = inimanager.GetLongValue(ngs, "SDWriteProtect", 0) != 0;
		static constexpr NeoGSConfig::WriteMode writeModes[] = {NeoGSConfig::WriteMode::Session, NeoGSConfig::WriteMode::Persist, NeoGSConfig::WriteMode::Off};
		ngsConfig.sdWrite = writeModes[choice("SDWrite", "session", {"session", "persist", "off"})];
		ngsConfig.flashWrite = writeModes[choice("FlashWrite", "session", {"session", "persist", "off"})];

		static constexpr NGSMP3SupportKind mp3Kinds[] = {NGSMP3SupportKind::None, NGSMP3SupportKind::Stub, NGSMP3SupportKind::Software};
		ngsConfig.mp3Support = mp3Kinds[choice("MP3Support", "software", {"none", "stub", "software"})];
		ngsConfig.mp3Chip = choice("Mp3Chip", "vs1001", {"vs1001", "vs1011"}) == 1 ? NeoGSConfig::Mp3Chip::VS1011 : NeoGSConfig::Mp3Chip::VS1001;
		ngsConfig.mp3Gain = std::clamp(inimanager.GetDoubleValue(ngs, "Mp3Gain", 1.0), 0.0, 8.0);
		ngsConfig.volume = static_cast<unsigned>(std::clamp<long>(inimanager.GetLongValue(ngs, "Volume", 8000), 0, 8192));
		ngsConfig.zxDmaWatch = choice("ZxDmaWatch", "selected", {"selected", "always"}) == 1 ? NeoGSConfig::ZxDmaWatch::Always
		                                                                                     : NeoGSConfig::ZxDmaWatch::Selected;
		ngsConfig.zxDmaWatchFrames = static_cast<unsigned>(std::clamp<long>(inimanager.GetLongValue(ngs, "ZxDmaWatchFrames", 5), 1, 3000));
		static constexpr NeoGSConfig::StereoMode stereoModes[] = {NeoGSConfig::StereoMode::Separated, NeoGSConfig::StereoMode::GS,
		                                                          NeoGSConfig::StereoMode::Mono};
		ngsConfig.stereoMode = stereoModes[choice("StereoMode", "separated", {"separated", "gs", "mono"})];
	}
#endif
	// Anti-alias decimator tier: Reference (default) | HighFidelity. Unknown
	// values warn and keep the default; a missing key resets to it
	{
		config.sound.decimatorHighFidelity = false;
		line[0] = '\0';
		CopyStringValue(inimanager.GetValue(sound, "DecimatorQuality", nullptr), line, sizeof line);
		if (StringHelper::CompareCaseInsensitive(line, "HighFidelity", strlen("HighFidelity")) == 0)
			config.sound.decimatorHighFidelity = true;
		else if (line[0] != '\0' && StringHelper::CompareCaseInsensitive(line, "Reference", strlen("Reference")) != 0)
			MLOGWARNING("Config: unsupported [SOUND] DecimatorQuality='%s', using Reference", line);
	}
	// VIDEO section
	// A/V sync video delay: auto (-1) = match the audio path latency
	// (~2 frames); 0 = lowest input latency (audio trails by the ring depth)
	{
		long delay = inimanager.GetLongValue(video, "AVSyncDelayFrames", -1);  // "auto" parses as 0 - use -1 default
		config.videoPresentDelayFrames = (delay >= -1 && delay <= 3) ? (int)delay : -1;
	}

	// Media set: [MEDIA] + legacy keys; relative paths are relative to the config file
	{
		std::string configFolder;
		if (!_configFilePath.empty())
		{
			const auto parent = FileHelper::ToFsPath(_configFilePath).parent_path().u8string();
			configFolder.assign(parent.begin(), parent.end());
		}
		_mediaReport.clear();
		_mediaSet = MediaConfig::FromIni(inimanager, configFolder, &_mediaReport);
	}

	// Emulated model
	CopyStringValue(inimanager.GetValue(misc, "HIMEM", "PENTAGON"), line, sizeof line);
	config.ramsize = inimanager.GetLongValue(misc, "RamSize", 128);
	{
		const char* powerOn = inimanager.GetValue(misc, "RAMPowerOn", "RANDOM");
		config.ramPowerOn = RamPowerOn::Random;
		if (!ParseRamPowerOn(powerOn ? powerOn : "", config.ramPowerOn))
			MLOGWARNING("Config: unknown [MISC] RAMPowerOn='%s' - using RANDOM (RANDOM | ZERO)", powerOn);
	}
	
	// The Scorpion Turbo+ logic firmware: SC15.1 (default) or SC15.3. It also decides Even M1 (below)
	{
		const char* logic = inimanager.GetValue(misc, "ScorpionTurboLogic", "SC15.1");
		const std::string v = logic ? logic : "SC15.1";
		if (v == "SC15.3")
			config.scorpionTurboLogic = ScorpionTurboLogic::SC153;
		else
		{
			config.scorpionTurboLogic = ScorpionTurboLogic::SC151;
			if (v != "SC15.1")
				MLOGWARNING("Config: unknown [MISC] ScorpionTurboLogic='%s' - using SC15.1 (SC15.1 | SC15.3)", logic);
		}
	}
	if (config.scorpionTurboLogic == ScorpionTurboLogic::SC153)
		config.even_M1 = 0;  // the SC15.3 firmware has no Even M1 ([ULA] EvenM1 is read above)

	// TS-Conf video DAC (the firmware build: STATUS VDAC_VER, the palette curve).
	// NONE is the standard build (the IDE board, PWM colours); a video DAC sits
	// on the IDE connector. TS_VDAC2=1 selects the VDAC2 (FT812) build
	{
		const char* vdac = inimanager.GetValue(misc, "TS_VDAC", "NONE");
		const std::string v = vdac ? vdac : "NONE";
		if (v == "3BIT")
			config.ts_vdac = 1;
		else if (v == "4BIT")
			config.ts_vdac = 2;
		else if (v == "5BIT")
			config.ts_vdac = 3;
		else
		{
			config.ts_vdac = 0;
			if (v != "NONE" && v != "OFF")
				MLOGWARNING("Config: unknown [MISC] TS_VDAC='%s' - using NONE (NONE | 3BIT | 4BIT | 5BIT)", vdac);
		}
		if (inimanager.GetLongValue(misc, "TS_VDAC2", 0) != 0)
			config.ts_vdac = 7;
	}
	// [VDAC2] RomImage: the FT812's ROM fonts (vdac2-integration-design.md §3, §10).
	// Resolved by the card like the other ROMs (working dir, executable, resources);
	// a missing file only leaves the ROM fonts blank
	CopyStringValue(inimanager.GetValue(vdac2, "RomImage", "rom/ft81x.rom"), config.vdac2_rom_path,
	                sizeof config.vdac2_rom_path);
	// [VDAC2] LineBudgetMargin: the FT812 line metrics' soft budget, percent below
	// the line period (line-budget-model.md §2); 0..50, default 10
	config.vdac2_line_budget_margin =
		static_cast<uint8_t>(std::clamp<long>(inimanager.GetLongValue(vdac2, "LineBudgetMargin", 10), 0, 50));
	// [VDAC2] CaptureFile: a debug capture of everything on the FT812's bus, for
	// replaying the chip alone (vdac2-test-corpus.md §4); empty = off
	CopyStringValue(inimanager.GetValue(vdac2, "CaptureFile", ""), config.vdac2_capture_path,
	                sizeof config.vdac2_capture_path);

	// NETWORK section (network adapters TDD §8). Card= fits a card on the
	// ZX-Bus; the runtime feature "network" can still unplug it.
	char netValue[64] = {};
	CopyStringValue(inimanager.GetValue(network, "Card", nullptr), netValue, sizeof netValue);
	{
		std::string error;
		if (!networkspec::ParseCards(netValue, config.network.card, error))
		{
			MLOGWARNING("Config: [NETWORK] Card=%s: %s - no card fitted", netValue, error.c_str());
			config.network.card = 0;
		}
	}
	config.network.hostAccess = (inimanager.GetLongValue(network, "HostAccess", 1) != 0) ? 1 : 0;
	netValue[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "DnsMode", nullptr), netValue, sizeof netValue);
	config.network.dnsPass = (StringHelper::CompareCaseInsensitive(netValue, "PASS", strlen("PASS")) == 0) ? 1 : 0;
	config.network.hosts[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "Hosts", nullptr), config.network.hosts, sizeof config.network.hosts);
	config.network.forwards[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "Forward", nullptr), config.network.forwards, sizeof config.network.forwards);
	{
		long timeout = inimanager.GetLongValue(network, "ConnectTimeoutMs", 10000);
		config.network.connectTimeoutMs = static_cast<unsigned>(std::clamp(timeout, 500L, 120000L));
	}
	config.network.comPort[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "ComPort", nullptr), config.network.comPort, sizeof config.network.comPort);
	{
		ComPortSpec spec;
		std::string error;
		if (!ComPortSpec::Parse(config.network.comPort, spec, error))
		{
			MLOGWARNING("Config: [NETWORK] ComPort=%s: %s - no COM port", config.network.comPort, error.c_str());
			config.network.comPort[0] = '\0';
		}
	}
	config.network.comModemLines = (inimanager.GetLongValue(network, "ComModemLines", 0) != 0) ? 1 : 0;
	netValue[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "EspChip", nullptr), netValue, sizeof netValue);
	{
		EspModule::Firmware firmware = EspModule::Firmware::Esp32At220;
		if (netValue[0] != '\0' && !EspModule::ParseFirmware(netValue, firmware))
			MLOGWARNING("Config: unknown [NETWORK] EspChip=%s, ESP32 used (ESP32 | ESP8266 | ESP8266-AT221 | ESP8266-AT222)", netValue);
		config.network.espChip = static_cast<uint8_t>(firmware);
	}
	config.network.zxWifi[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "ZxWifi", nullptr), config.network.zxWifi, sizeof config.network.zxWifi);
	{
		ComPortSpec spec;
		std::string error;
		if (!ComPortSpec::Parse(config.network.zxWifi, spec, error))
		{
			MLOGWARNING("Config: [NETWORK] ZxWifi=%s: %s - the card's ESP runs AT", config.network.zxWifi, error.c_str());
			config.network.zxWifi[0] = '\0';
		}
	}
	config.network.zifi[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "ZiFi", nullptr), config.network.zifi, sizeof config.network.zifi);
	if (config.network.zifi[0])
	{
		ComPortSpec spec;
		std::string error;
		if (!ComPortSpec::Parse(config.network.zifi, spec, error))
		{
			MLOGWARNING("Config: [NETWORK] ZiFi=%s: %s - no ZiFi board", config.network.zifi, error.c_str());
			config.network.zifi[0] = '\0';
		}
	}
	config.network.atm2IoEsp[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "Atm2IoEsp", nullptr), config.network.atm2IoEsp, sizeof config.network.atm2IoEsp);
	{
		ComPortSpec spec;
		std::string error;
		if (!ComPortSpec::Parse(config.network.atm2IoEsp, spec, error))
		{
			MLOGWARNING("Config: [NETWORK] Atm2IoEsp=%s: %s - the card's ESP runs AT", config.network.atm2IoEsp, error.c_str());
			config.network.atm2IoEsp[0] = '\0';
		}
	}
	{
		// The card's bus address: a multiple of 8 (CT2..CT0 pick the register)
		const long address = inimanager.GetLongValue(network, "Atm2IoEspAddress", 0xF0);
		config.network.atm2IoEspAddress = static_cast<uint8_t>(address & 0xF8);
		if (address < 0 || address > 0xFF || (address & 0x07))
			MLOGWARNING("Config: [NETWORK] Atm2IoEspAddress=%ld: a bus address 0x00..0xF8 in steps of 8 (0xF0 or 0xF8); 0x%02X used",
			            address, config.network.atm2IoEspAddress);
	}
	// The Hayes modem's phone book (network tdd §10): "5551234=bbs.example.org:23,5550000=10.0.2.2:2323"
	config.network.modemPhonebook[0] = '\0';
	CopyStringValue(inimanager.GetValue(network, "ModemPhonebook", nullptr), config.network.modemPhonebook,
	                sizeof config.network.modemPhonebook);
	{
		std::map<std::string, std::string> book;
		std::string error;
		if (!HayesModemPeer::ParsePhonebook(config.network.modemPhonebook, book, error))
		{
			MLOGWARNING("Config: [NETWORK] ModemPhonebook=%s: %s - empty phone book", config.network.modemPhonebook, error.c_str());
			config.network.modemPhonebook[0] = '\0';
		}
	}
	if (inimanager.GetValue(network, "ComFlavor", nullptr))
		MLOGWARNING("Config: [NETWORK] ComFlavor= is no longer read: the machine decides its serial port "
		            "(ZX-Evo: [EVO] Avr=); a ZX-WiFi card is Card=ZXWIFI");

	// Make sure we're emulating valid model & configuration
	if (DetermineModel(line, config.ramsize))
	{
		// Apply hardware-accurate INT timing defaults based on the selected model
		ApplyModelTimingDefaults(config);

		// TS-Conf VDAC2 build: the card sits on the IDE connector and the
		// firmware has no IDE controller (tune.v: IDE_VDAC2 instead of
		// IDE_HDD), so the machine has no IDE whatever [HDD] Scheme says
		// (vdac2-integration-design.md §3). With IDE_NONE no IDE slot exists
		if (config.mem_model == MM_TSL && config.ts_vdac == 7 && config.ide_scheme != IDE_NONE)
		{
			MLOGWARNING("Config: [HDD] Scheme=%s ignored: the VDAC2 card ([MISC] TS_VDAC2=1) occupies the IDE "
			            "connector", IdeSchemeName(config.ide_scheme));
			config.ide_scheme = IDE_NONE;
		}

		// The config is loaded and valid: the process-wide hook gets the last
		// word before any device is created from it
		if (const ConfigLoadedHook& hook = ConfigLoadedHookStorage())
			hook(config);

		result = true;
#ifndef ENABLE_VDAC2
		// A build without the FT812 library cannot fit the card: refuse the
		// machine instead of running it without its video output
		// (vdac2-integration-design.md §2)
		if (config.mem_model == MM_TSL && config.ts_vdac == 7)
		{
			MLOGERROR("Config: [MISC] TS_VDAC2=1, but this build has no VDAC2 support (CMake ENABLE_VDAC2=OFF)");
			result = false;
		}
#endif
	}
	else
	{
	    std::string message = StringHelper::Format("Unable to recognize ZX-Spectrum model selected in config. Model: %s, mem: %d", line, config.ramsize);
		MLOGERROR(message.c_str());
	}

	return result;
}

Config::ConfigLoadedHook& Config::ConfigLoadedHookStorage()
{
	static ConfigLoadedHook hook;
	return hook;
}

void Config::SetConfigLoadedHook(ConfigLoadedHook hook)
{
	ConfigLoadedHookStorage() = std::move(hook);
}

Config::ConfigLoadedHook Config::GetConfigLoadedHook()
{
	return ConfigLoadedHookStorage();
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

	// Search for model in lookup dictionary (short names and their aliases)
	if (const TMemModel* found = FindModelByShortName(model))
	{
		config.mem_model = found->Model;
		maxMemory = found->AvailRAMs;
		fullModelName = found->FullName;
		result = true;
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
	for (const ModelAlias& alias : model_aliases)
	{
		if (StringHelper::CompareCaseInsensitive(shortName.c_str(), alias.Name, strlen(alias.Name)) == 0)
			return FindModelByEnum(alias.Model);
	}
	return nullptr;
}

const TMemModel* Config::FindModelByEnum(MEM_MODEL model)
{
	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		if (mem_model[i].Model == model)
		{
			return &mem_model[i];
		}
	}
	return nullptr;
}

std::string Config::GetModelFullName(MEM_MODEL model)
{
	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		if (mem_model[i].Model == model)
		{
			return mem_model[i].FullName;
		}
	}
	return "Unknown";
}

std::string Config::GetConfigFolderForModel(MEM_MODEL model, uint32_t ramSizeKB)
{
	const TMemModel* info = nullptr;
	for (uint8_t i = 0; i < N_MM_MODELS; i++)
	{
		if (mem_model[i].Model == model)
		{
			info = &mem_model[i];
			break;
		}
	}

	uint32_t ram = ramSizeKB ? ramSizeKB : (info ? info->defaultRAM : 128);

	switch (model)
	{
		case MM_PENTAGON:    return (ram >= 512) ? "pentagon512k" : "pentagon128k";
		case MM_SPECTRUM48:  return "spectrum48";
		case MM_SPECTRUM128: return "spectrum128";
		case MM_PLUS3:       return "spectrum3";
		case MM_PLUS2:       return "spectrum2";
		case MM_PLUS2A:      return "spectrum2a";
		case MM_TSL:         return "ts-conf";
		default:
			break;
	}

	// No dedicated folder yet: derive it from the short name so the
	// LoadConfig error message tells exactly which folder is expected
	std::string folder = (info && info->ShortName) ? info->ShortName : "pentagon128k";
	std::transform(folder.begin(), folder.end(), folder.begin(),
	               [](unsigned char c) { return (char)std::tolower(c); });
	return folder;
}

bool Config::IsModelCreatable(const TMemModel& model)
{
	// Hard prerequisite: the build must know how to decode the model's ports.
	// GetPortDecoderForModel throws std::logic_error otherwise.
	if (!PortDecoder::IsModelSupported(model.Model))
		return false;

	// Second prerequisite: the model's config must be resolvable the exact
	// way Emulator::Init -> LoadConfig resolves it, so the flag reflects
	// what a create attempt would actually do.
	const std::string folder = GetConfigFolderForModel(model.Model, model.defaultRAM);
	std::string relativePath = FileHelper::PathCombine("configs", folder);
	relativePath = FileHelper::PathCombine(relativePath, GetDefaultConfig());

	for (const std::string& basePath : { FileHelper::GetExecutablePath(), FileHelper::GetResourcesPath() })
	{
		if (basePath.empty())
			continue;

		std::string configPath = FileHelper::AbsolutePath(FileHelper::PathCombine(basePath, relativePath));
		if (FileHelper::FileExists(configPath))
			return true;
	}

	return false;
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

void Config::StripProfRomQuadrantSuffix(char* path, size_t len)
{
	if (path == nullptr || len == 0)
	{
		return;
	}

	// Heritage unreal.ini files may carry a quadrant selector suffix
	// (PROFROM=<file>:<n>). Only a trailing decimal selector is stripped - a
	// drive-letter colon ("C:\\...") never qualifies because the remainder is
	// not all digits.
	char* suffix = strrchr(path, ':');
	if (suffix == nullptr || suffix == path)
	{
		return;
	}

	bool numericSuffix = true;
	for (const char* cursor = suffix + 1; *cursor != '\0'; cursor++)
	{
		if (*cursor < '0' || *cursor > '9')
		{
			numericSuffix = false;
			break;
		}
	}

	if (numericSuffix && *(suffix + 1) != '\0')
	{
		*suffix = '\0';
		MLOGWARNING("Stripped ProfROM quadrant suffix from path '%s' (quadrant selection is runtime state)", path);
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
    // Every model is calibrated on the same invariant: the distance from the INT
    // (fires at intstart+1) to the first paper pixel of the renderer's raster
    // (T 24 of the first paper line, see ScreenZX::CreateTstateLUT):
    //   Pentagon: 71635 -> 17988T, verified on "Across the Edge" (71634 = 17989T breaks it);
    //             references span 17985 (ZXMAK2) .. 17987 (MiSTer) .. 17989 (UnrealSpeccy)
    //   ZX-48K:   14340T (Xpeccy ULA.48, MiSTer ula.sv + 6T output pipeline; ZXMAK2 14336)
    //             -> 16152 - 14340 - 1 = 1811
    //   ZX-128K:  14366T (Xpeccy ULA.128, MiSTer ula.sv + 6T; ZXMAK2 14362) -> 16212 - 14366 - 1 = 1845
    //   Scorpion: 14336T (Xpeccy ULA.Scorpion, ZXMAK2 UlaScorpion; UnrealSpeccy 14344) -> 1815
    switch (config.mem_model)
    {
        case MM_PENTAGON:
            // INT fires at intstart+1 due to strict `>` check. Paper starts at T=17944.
            // Calculation: (71680 - (71635+1)) + 17944 = 17988T (demo-verified)
            // Note: INT is quantized to 4T due to HALT; the 2-pixel fine adjustment
            // is handled in ScreenZX::SetBorderColor. See: docs/timing/pentagon-border-timing.md
            config.intstart = 71635;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM48:
            config.intstart = 1811;
            config.intlen   = 32;
            break;

        case MM_SPECTRUM128:
        case MM_PLUS2:
            config.intstart = 1845;
            config.intlen   = 36;   // ZX-128K ULA has 72-HC INT = 36 T-states
            break;

        case MM_PLUS2A:
        case MM_PLUS3:
            // The gate array keeps the 128K frame and INT position; its INT is 32 T (ZXMAK2 UlaPlus3,
            // BizHawk ZX128Plus2a; ZX-M8XXX says 36)
            config.intstart = 1845;
            config.intlen   = 32;
            break;

        case MM_SCORP:
        case MM_PROFSCORP:
            // Scorpion ZS-256: Sinclair-matching 312 x 224T frame, INT at the ZX48
            // position (hardware-reference 6; Xpeccy and ZXMAK2: 14336T to paper).
            // Frame/t_line values read from another model's INI must not leak in.
            config.frame    = 69888;   // 224 * 312
            config.t_line   = 224;
            config.intstart = 1815;
            config.intlen   = 32;
            break;

        case MM_PROFI:
        case MM_PROFI3:
        {
            // Profi: the frame of the board's sync PROM ([PROFI] SyncProm=, ProfiSyncPromFrame), decoded from the
            // PROM dumps (docs/inprogress/2026-10-01-profi-v3-v5/cross-check.md section 4). The defaults:
            //   v3 (0A1DFAFD, a 3.2 board's original PROM): 69888 T, INT 12580 T before paper, 28 T (UnrealSpeccy)
            //   v5 (D2D4A7C8, Kondor 5.04 with the DD53 fix): 69888 T, INT 14368 T before paper, 28 T
            // The raster paper starts at T=16152 (line 72 * 224 + 24); INT fires at intstart+1
            const ProfiFrame f = ProfiSyncPromFrame(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model);
            config.frame    = f.frame;
            config.t_line   = f.tLine;
            config.intstart = ProfiIntStart(f);
            config.intlen   = f.intLength;
            break;
        }

        case MM_ATM450:
            // ATM Turbo 2 v4.50: 308 x 224 T = 68992 T, inferred from the system ROM's
            // frame-timing protection (it corrupts typed keys unless its frame measure
            // lands in a window that 312 lines miss). The 4 lines come out of the
            // vertical blank, so INT moves 4 lines earlier and INT-to-paper stays 14395T.
            // See docs/inprogress/2026-10-01-atm450/frame-timing-protection.md
            config.intstart = AtmGeometry::kAtm450IntStart;
            config.intlen   = 32;
            break;

        case MM_ATM710:
        case MM_ATM3:
            // ATM Turbo 2+ and ZX-Evo BaseConf: 312 x 224T frame at the base
            // clock in every video mode; the FF77.3 turbo multiplies the CPU only.
            // INT-to-first-ZX-paper distance 14395T (UnrealSpeccy
            // PRESET.ATM1_2_3.5MHz, "thanks to DDp"; Xpeccy ULA.ATM2: 14384T).
            // ZX paper starts at T=16152 (line 72 * 224 + 24), INT fires at
            // intstart+1 => 16152 - 1757 = 14395T.
            config.intstart = 1756;
            config.intlen   = 32;
            break;

        default:
            // Leave existing values for the other models
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
        // The Scorpion boards' Even M1 wait (z80.cpp) is part of the model, like its frame: only there, and only
        // with the SC15.1 logic firmware ([MISC] ScorpionTurboLogic)
        config.even_M1 = ((config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP) &&
                          config.scorpionTurboLogic == ScorpionTurboLogic::SC151) ? 1 : 0;

        switch (config.mem_model)
        {
            case MM_SPECTRUM48:
                config.frame = 69888;   // 224 * 312
                config.t_line = 224;
                config.intstart = 1811;
                config.intlen = 32;
                break;
            case MM_SPECTRUM128:
            case MM_PLUS2:
            case MM_PLUS2A:
            case MM_PLUS3:
                config.frame = 70908;   // 228 * 311
                config.t_line = 228;
                config.intstart = 1845;
                config.intlen = (config.mem_model == MM_PLUS3 || config.mem_model == MM_PLUS2A) ? 32 : 36;
                break;
            case MM_PENTAGON:
                config.frame = 71680;   // 224 * 320
                config.t_line = 224;
                break;
            case MM_SCORP:
            case MM_PROFSCORP:
                config.frame = 69888;   // 224 * 312
                config.t_line = 224;
                config.intstart = 1815;
                config.intlen = 32;
                break;
            case MM_PROFI:
            case MM_PROFI3:
            {
                const ProfiFrame f =
                    ProfiSyncPromFrame(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model);
                config.frame = f.frame;
                config.t_line = f.tLine;
                config.intstart = ProfiIntStart(f);
                config.intlen = f.intLength;
                break;
            }
            case MM_ATM450:
                // 308 lines: docs/inprogress/2026-10-01-atm450/frame-timing-protection.md
                config.frame = AtmGeometry::kAtm450Frame;   // 224 * 308
                config.t_line = 224;
                config.intstart = AtmGeometry::kAtm450IntStart;
                config.intlen = 32;
                break;
            case MM_ATM710:
            case MM_ATM3:
                config.frame = 69888;   // 224 * 312
                config.t_line = 224;
                config.intstart = 1756;
                config.intlen = 32;
                break;
            case MM_TSL:
                // TS-Conf: 320 lines x 224 T (the Pentagon raster, hardware-spec §4). INT comes from the
                // machine's interrupt source (VS_INT / HS_INT), so intstart / intlen are not used
                config.frame = 71680;   // 224 * 320
                config.t_line = 224;
                break;
            case MM_SPRINTER:
                // Sprinter: 320 lines x 224 T after reset (Sprinter hardware-reference §6.1). INT comes from
                // the mode table through the machine's interrupt source, so intstart / intlen are not used
                config.frame = 71680;   // 224 * 320
                config.t_line = 224;
                break;
            default:
                break;
        }

        // Invariant: frame_duration_us must be recomputed with config.frame
        config.frame_duration_us = CalculateFrameDurationUs(config.frame);
    }

    MLOGINFO("ApplyModelTimingDefaults: model=%d intstart=%u intlen=%u frame=%u line=%u",
             config.mem_model, config.intstart, config.intlen, config.frame, config.t_line);
}
