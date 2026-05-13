#include "loader_tzx_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "_helpers/test_path_helper.h"

/// region <SetUp / TearDown>

void LoaderTZX_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new Core(_context);
    if (_cpu->Init())
    {

        // Use Spectrum48K / Pentagon memory layout
        _cpu->GetMemory()->DefaultBanksFor48k();
    }
    else
    {
        throw std::runtime_error("Unable to SetUp LoaderSNA test(s)");
    }
}

void LoaderTZX_Test::TearDown()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(LoaderTZX_Test, parseHardware)
{
    static std::string testTapePath = TestPathHelper::GetTestDataPath("loaders/tap/action.tap");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);

    LoaderTZXCUT loader(_context, testTapePath);
    uint8_t data[] = { 0x1, };

    loader.parseHardware(data);
}