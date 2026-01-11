#include "loader_z80_test.h"


#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "_helpers/test_path_helper.h"

/// region <SetUp / TearDown>

void LoaderZ80_Test::SetUp()
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

void LoaderZ80_Test::TearDown()
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

TEST_F(LoaderZ80_Test, validateSnapshotFile)
{
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);

    bool result = loader.validate();
    if (result != true)
    {
        std::string message = StringHelper::Format("Validation FAILED for file '%s'", absoluteSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    if (loader._fileValidated != true)
    {
        std::string message = "LoaderSNA::_fileValidated was not set during LoaderZ80::validate() call";
        FAIL() << message << std::endl;
    }
}

TEST_F(LoaderZ80_Test, stageLoad)
{
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.validate();
    EXPECT_EQ(result, true) << "Invalid '" << absoluteSnapshotPath << "' snapshot";

    result = loader.stageLoad();
    EXPECT_EQ(result, true) << "Unable to load '" << absoluteSnapshotPath << "' snapshot";
}

TEST_F(LoaderZ80_Test, load)
{
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.load();
    EXPECT_EQ(result, true) << "Unable to load '" << absoluteSnapshotPath << "' snapshot";
}