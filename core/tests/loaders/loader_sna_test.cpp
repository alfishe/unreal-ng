#include "loader_sna_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void LoaderSNA_Test::SetUp()
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

void LoaderSNA_Test::TearDown()
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

TEST_F(LoaderSNA_Test, validateSnapshotFile)
{
    static std::string testSnapshotPath = "../../../tests/loaders/sna/multifix.sna";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderSNACUT loader(_context, testSnapshotPath);

    bool result = loader.validateSnapshotFile();
    if (result != true)
    {
        std::string message = StringHelper::Format("Validation FAILED for file '%s'", absoluteSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    if (loader._fileValidated != true)
    {
        std::string message = "LoaderSNA::_fileValidated was not set during LoaderSNA::validateSnapshotFile() call";
        FAIL() << message << std::endl;
    }
}

TEST_F(LoaderSNA_Test, is48kSnapshot)
{
    static std::string test48kSnapshotPath = "../../../tests/loaders/sna/48k.sna";
    std::string absolute48kSnapshotPath = FileHelper::AbsolutePath(test48kSnapshotPath);

    static std::string test128kSnapshotPath = "../../../tests/loaders/sna/multifix.sna";
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    /// region <Positive cases>
    /// endregion </Positive cases>

    /// region <Negative cases>

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);

    FILE* file = FileHelper::OpenFile(absolute128kSnapshotPath);
    if (file != nullptr)
    {
        bool result = loader.is48kSnapshot(file);

        FileHelper::CloseFile(file);

        if (result == true)
        {
            std::string message = StringHelper::Format("Validation FAILED for file '%s'. It's 128k mode snapshot", absolute128kSnapshotPath.c_str());
            FAIL() << message << std::endl;
        }
    }
    else
    {
        std::string message = StringHelper::Format("Unable to open file '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    /// endregion </Negative cases>
}

TEST_F(LoaderSNA_Test, is128kSnapshot)
{
    static std::string test48kSnapshotPath = "../../../tests/loaders/sna/48k.sna";
    std::string absolute48kSnapshotPath = FileHelper::AbsolutePath(test48kSnapshotPath);

    static std::string test128kSnapshotPath = "../../../tests/loaders/sna/multifix.sna";
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    /// region <Positive cases>

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);

    FILE* file = FileHelper::OpenFile(absolute128kSnapshotPath);
    if (file != nullptr)
    {
        bool result = loader.is128kSnapshot(file);

        FileHelper::CloseFile(file);

        if (result != true)
        {
            std::string message = StringHelper::Format("Validation FAILED for file '%s'", absolute128kSnapshotPath.c_str());
            FAIL() << message << std::endl;
        }
    }
    else
    {
        std::string message = StringHelper::Format("Unable to open file '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    /// endregion </Positive cases>

    /// region <Negative cases>
    /// endregion </Negative cases>
}

TEST_F(LoaderSNA_Test, loadToStaging)
{

}

TEST_F(LoaderSNA_Test, load48kToStaging)
{

}

TEST_F(LoaderSNA_Test, load128kToStaging)
{
    static std::string test128kSnapshotPath = "../../../tests/loaders/sna/multifix.sna";
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);
    if (loader.validateSnapshotFile())
    {
        bool result = loader.load128kToStaging();

        if (!result)
        {
            std::string message = StringHelper::Format("Loading FAILED for 128K snapshot: '%s'", absolute128kSnapshotPath.c_str());
            FAIL() << message << std::endl;
        }

        if (!loader._stagingLoaded)
        {
            std::string message = "LoaderSNA::_stagingLoaded was not set during LoaderSNA::load128kToStaging() call";
            FAIL() << message << std::endl;
        }
    }
    else
    {
        std::string message = StringHelper::Format("Invalid 128K snapshot: '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }
}

TEST_F(LoaderSNA_Test, applySnapshotFromStaging)
{
    static std::string test128kSnapshotPath = "../../../tests/loaders/sna/multifix.sna";
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);
    //cout << absolute128kSnapshotPath << endl;

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);
    if (!loader.validateSnapshotFile())
    {
        std::string message = StringHelper::Format("Invalid 128K snapshot: '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    bool loadResult = loader.load128kToStaging();
    if (!loadResult)
    {
        std::string message = StringHelper::Format("Loading FAILED for 128K snapshot: '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    bool result = loader.applySnapshotFromStaging();
    if (!result)
    {
        std::string message = StringHelper::Format("Unable to apply loaded 128K snapshot from file: '%s'", absolute128kSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }
}