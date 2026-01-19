#include "loader_sna_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "_helpers/test_path_helper.h"

/// region <SetUp / TearDown>

void LoaderSNA_Test::SetUp()
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

TEST_F(LoaderSNA_Test, validate)
{
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderSNACUT loader(_context, testSnapshotPath);

    bool result = loader.validate();
    if (result != true)
    {
        std::string message = StringHelper::Format("Validation FAILED for file '%s'", absoluteSnapshotPath.c_str());
        FAIL() << message << std::endl;
    }

    if (loader._fileValidated != true)
    {
        std::string message = "LoaderSNA::_fileValidated was not set during LoaderSNA::validate() call";
        FAIL() << message << std::endl;
    }
}

TEST_F(LoaderSNA_Test, is48kSnapshot)
{
    static std::string test48kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/48k.sna");
    std::string absolute48kSnapshotPath = FileHelper::AbsolutePath(test48kSnapshotPath);

    static std::string test128kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    /// region <Positive cases>
    /// endregion </Positive cases>

    /// region <Negative cases>

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);

    FILE* file = FileHelper::OpenExistingFile(absolute128kSnapshotPath);
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
    static std::string test48kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/48k.sna");
    std::string absolute48kSnapshotPath = FileHelper::AbsolutePath(test48kSnapshotPath);

    static std::string test128kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    /// region <Positive cases>

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);

    FILE* file = FileHelper::OpenExistingFile(absolute128kSnapshotPath);
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
    static std::string test128kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);
    if (loader.validate())
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
    static std::string test128kSnapshotPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    std::string absolute128kSnapshotPath = FileHelper::AbsolutePath(test128kSnapshotPath);
    //cout << absolute128kSnapshotPath << endl;

    LoaderSNACUT loader(_context, absolute128kSnapshotPath);
    if (!loader.validate())
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

/// region <Invalid File Tests>

TEST_F(LoaderSNA_Test, rejectEmptyFile)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/empty.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Empty file should be rejected";
}

TEST_F(LoaderSNA_Test, rejectTruncatedHeader)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/truncated_header.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "File with truncated header should be rejected";
}

TEST_F(LoaderSNA_Test, rejectHeaderOnly)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/header_only.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "File with header but no RAM should be rejected";
}

TEST_F(LoaderSNA_Test, rejectTruncated48K)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/truncated_48k.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Truncated 48K snapshot should be rejected";
}

TEST_F(LoaderSNA_Test, rejectTruncated128K)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/truncated_128k.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Truncated 128K snapshot should be rejected";
}

TEST_F(LoaderSNA_Test, rejectWrongFormat_PNG)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/fake_png.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "PNG file should be rejected";
}

TEST_F(LoaderSNA_Test, rejectWrongFormat_JPEG)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/fake_jpeg.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "JPEG file should be rejected";
}

TEST_F(LoaderSNA_Test, rejectTextFile)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/text_file.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Text file should be rejected";
}

TEST_F(LoaderSNA_Test, rejectAllZeros)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/all_zeros.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "All zeros file should be rejected";
}

TEST_F(LoaderSNA_Test, rejectAllFF)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/invalid/all_ff.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "All 0xFF file should be rejected";
}

/// endregion </Invalid File Tests>

/// region <Lock/State Verification Tests>

TEST_F(LoaderSNA_Test, load128KWithPreLockedPort)
{
    // Lock the paging port to simulate a pre-existing locked state
    _context->pPortDecoder->LockPaging();
    ASSERT_TRUE(_context->emulatorState.p7FFD & PORT_7FFD_LOCK) << "Port should be locked for test setup";
    
    // Load a 128K snapshot (using first available 128K SNA)
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    LoaderSNACUT loader(_context, testPath);
    
    bool loadResult = loader.load();
    ASSERT_TRUE(loadResult) << "128K SNA should load even with pre-locked port";
    
    bool applyResult = loader.applySnapshotFromStaging();
    ASSERT_TRUE(applyResult) << "128K SNA should apply even with pre-locked port";
}

TEST_F(LoaderSNA_Test, load48KAfterLocked128K)
{
    // First load a 128K snapshot and lock the port
    static std::string test128Path = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    LoaderSNACUT loader128(_context, test128Path);
    
    bool load128 = loader128.load();
    ASSERT_TRUE(load128) << "128K SNA should load successfully";
    
    bool apply128 = loader128.applySnapshotFromStaging();
    ASSERT_TRUE(apply128) << "128K SNA should apply successfully";
    
    // Lock the port explicitly
    _context->pPortDecoder->LockPaging();
    ASSERT_TRUE(_context->emulatorState.p7FFD & PORT_7FFD_LOCK) << "Port should be locked after 128K load";
    
    // Now load a 48K snapshot - it should still work
    static std::string test48Path = TestPathHelper::GetTestDataPath("loaders/sna/Timing_Tests-48k_v1.0.sna");
    LoaderSNACUT loader48(_context, test48Path);
    
    bool load48 = loader48.load();
    ASSERT_TRUE(load48) << "48K SNA should load even after locked 128K state";
    
    bool apply48 = loader48.applySnapshotFromStaging();
    ASSERT_TRUE(apply48) << "48K SNA should apply even after locked 128K state";
}

TEST_F(LoaderSNA_Test, repeatedLoadIsIdempotent)
{
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    
    // Load the same snapshot twice
    LoaderSNACUT loader1(_context, testPath);
    bool load1 = loader1.load();
    ASSERT_TRUE(load1) << "First load should succeed";
    
    bool apply1 = loader1.applySnapshotFromStaging();
    ASSERT_TRUE(apply1) << "First apply should succeed";
    
    // Lock the port
    _context->pPortDecoder->LockPaging();
    
    // Load again
    LoaderSNACUT loader2(_context, testPath);
    bool load2 = loader2.load();
    ASSERT_TRUE(load2) << "Second load should succeed";
    
    bool apply2 = loader2.applySnapshotFromStaging();
    ASSERT_TRUE(apply2) << "Second apply should succeed";
}

/// endregion </Lock/State Verification Tests>