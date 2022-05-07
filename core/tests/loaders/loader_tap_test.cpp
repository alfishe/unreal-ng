#include "pch.h"

#include "loader_tap_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void LoaderTAP_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new CPU(_context);
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

void LoaderTAP_Test::TearDown()
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

TEST_F(LoaderTAP_Test, read)
{
    static std::string testTapePath = "../../../tests/loaders/tap/action.tap";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);

    LoaderTAPCUT loader(_context, testTapePath);
    bool result = loader.read();
    EXPECT_EQ(result, true);
}

TEST_F(LoaderTAP_Test, parseHardware)
{
    static std::string testTapePath = "../../../tests/loaders/tap/action.tap";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);

    LoaderTAPCUT loader(_context, testTapePath);
    uint8_t data[] = { 0x1, };

    loader.parseHardware(data);
}
