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
    };

    std::vector<std::string> refStrings =
    {
        { "<All>" },
        { "<None>" },
        { "<1> | <2> | <3> | <4> | <5> | <6> | <7>" },
        { "<1> | <3> | <5> | <7>" },
    };

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
        0x0000FFFF,
        0x0000FFFE
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
        "Debugger",
        "<Unknown>, Core, Z80, Memory, I/O, Disk, Video, Sound, DMA, Debugger",
        "Core, Z80, Memory, I/O, Disk, Video, Sound, DMA, Debugger"
    };

    for (int i = 0; i < refInputs.size(); i++)
    {
        std::string result = _moduleLogger->DumpModules(refInputs[i]);
        EXPECT_EQ(refStrings[i], result);
    }
}

TEST_F(ModuleLogger_Test, DumpSettings)
{
    LoggerSettings& loggerSettings = _moduleLogger->_settings;
    loggerSettings.modules = 0xFFFFFFFF;

    // TODO: Not implemented yet
}