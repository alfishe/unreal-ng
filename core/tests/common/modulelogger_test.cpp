#include <common/modulelogger.h>
#include "pch.h"

#include "modulelogger_test.h"

/// region <SetUp / TearDown>

void ModuleLogger_Test::SetUp()
{
    _context = new EmulatorContext();
    _moduleLogger = new ModuleLoggerCUT(_context);
}

void ModuleLogger_Test::TearDown()
{
    if (_moduleLogger)
    {
        delete _moduleLogger;
        _moduleLogger = nullptr;
    }

    if (_context)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </SetUp / TearDown>

TEST_F(ModuleLogger_Test, DumpResolveFlags)
{
    static const char* names[] =
    {
        "<1>",
        "<2>",
        "<3>",
        "<4>",
        "<5>",
        "<6>",
        "<7>"
    };

    std::vector<uint16_t> refInputs =
    {
        0xFFFF,
        0x0000,
        0x00FF,
        0x00AA
    };

    std::vector<std::string> refStrings;
    refStrings.push_back("<All>");
    refStrings.push_back("<None>");
    refStrings.push_back("  <1>: on\n  <2>: on\n  <3>: on\n  <4>: on\n  <5>: on\n  <6>: on\n  <7>: on");
    refStrings.push_back("  <1>: off\n  <2>: on\n  <3>: off\n  <4>: on\n  <5>: off\n  <6>: on\n  <7>: off");

    for (int i = 0; i < refInputs.size(); i++)
    {
        std::string result = _moduleLogger->DumpResolveFlags(refInputs[i], names, sizeof(names) / sizeof(names[0]));
        EXPECT_EQ(refStrings[i], result);
    }
}

TEST_F(ModuleLogger_Test, DumpModules)
{
    std::vector<uint32_t> refInputs =
    {
        0xFFFFFFFF,
        0x00000000,
        0x00000001,
        0x00000002,
        0x00000004,
        0x00000008,
        0x00000010,
        0x00000020,
        0x00000040,
        0x00000080,
        0x00000100,
        0x00000200,
        0x00000400,
        0x0000FFFE,
        0x0000FFFF
    };

    std::vector<std::string> refStrings =
    {
        "<All>",
        "<None>",
        "<Unknown>",
        "Core",
        "Z80",
        "Memory",
        "I/O",
        "Disk",
        "Video",
        "Sound",
        "DMA",
        "Loader",
        "Debugger",
        "Core, Z80, Memory, I/O, Disk, Video, Sound, DMA, Loader, Debugger, Disassembler",
        "<Unknown>, Core, Z80, Memory, I/O, Disk, Video, Sound, DMA, Loader, Debugger, Disassembler"
    };

    for (int i = 0; i < refInputs.size(); i++)
    {
        std::string result = _moduleLogger->DumpModules(refInputs[i]);
        EXPECT_EQ(refStrings[i], result);
    }
}

TEST_F(ModuleLogger_Test, DumpSettings)
{
    LoggerSettings settings;
    settings.modules = 0xFFFFFFFF;
    _moduleLogger->SetLoggingSettings(settings);
    
    std::string result = _moduleLogger->DumpSettings();
    std::string expected = "Module logger settings dump:\nCore: on\n<All>\nZ80: on\n<All>\nMemory: on\n<All>\nI/O: on\n<All>\nDisk: on\n<All>\nVideo: on\n<All>\nSound: on\n<All>\nDMA: on\n<All>\nLoader: on\n<All>\nDebugger: on\n<All>\nDisassembler: on\n<All>\n";
    EXPECT_EQ(expected, result);
    
    settings.modules = 0x00000002; // Core module only
    _moduleLogger->SetLoggingSettings(settings);
    
    result = _moduleLogger->DumpSettings();
    expected = "Module logger settings dump:\nCore: on\n<All>\nZ80: off\nMemory: off\nI/O: off\nDisk: off\nVideo: off\nSound: off\nDMA: off\nLoader: off\nDebugger: off\nDisassembler: off\n";
    EXPECT_EQ(expected, result);
}

TEST_F(ModuleLogger_Test, LogMessages)
{
    LoggerSettings settings;
    settings.modules = 0xFFFFFFFF; // Enable all modules
    _moduleLogger->SetLoggingSettings(settings);
    _moduleLogger->SetLoggingLevel(LoggerLevel::LogDebug); // Enable all levels
    
    // Test different log levels
    _moduleLogger->LogMessage(LoggerLevel::LogDebug, PlatformModulesEnum::MODULE_NONE, 0, "Test debug message");
    _moduleLogger->LogMessage(LoggerLevel::LogInfo, PlatformModulesEnum::MODULE_NONE, 0, "Test info message");
    _moduleLogger->LogMessage(LoggerLevel::LogWarning, PlatformModulesEnum::MODULE_NONE, 0, "Test warning message");
    _moduleLogger->LogMessage(LoggerLevel::LogError, PlatformModulesEnum::MODULE_NONE, 0, "Test error message");
    
    // Test module-specific logging
    _moduleLogger->LogMessage(LoggerLevel::LogDebug, PlatformModulesEnum::MODULE_CORE, 0, "Core module debug message");
    _moduleLogger->LogMessage(LoggerLevel::LogInfo, PlatformModulesEnum::MODULE_Z80, 0, "Z80 module info message");
    _moduleLogger->LogMessage(LoggerLevel::LogWarning, PlatformModulesEnum::MODULE_MEMORY, 0, "Memory module warning message");
    _moduleLogger->LogMessage(LoggerLevel::LogError, PlatformModulesEnum::MODULE_IO, 0, "I/O module error message");
    
    // Test logging with format arguments
    _moduleLogger->LogMessage(LoggerLevel::LogDebug, PlatformModulesEnum::MODULE_NONE, 0, "Formatted message: %d %s", 42, "test");
    _moduleLogger->LogMessage(LoggerLevel::LogInfo, PlatformModulesEnum::MODULE_CORE, 0, "Formatted module message: %d %s", 42, "test");
}