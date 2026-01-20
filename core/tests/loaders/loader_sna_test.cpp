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

/// region <Save Tests>

TEST_F(LoaderSNA_Test, determineOutputFormat_48kMode)
{
    // Set up 48K mode by locking paging (bit 5 of port 7FFD)
    _context->emulatorState.p7FFD = 0x20;  // Lock bit set
    _context->pPortDecoder->LockPaging();
    
    std::string tempPath = "/tmp/test_determine_48k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    SNA_MODE mode = loader.determineOutputFormat();
    EXPECT_EQ(mode, SNA_48) << "Locked paging should result in 48K format";
}

TEST_F(LoaderSNA_Test, determineOutputFormat_128kMode)
{
    // Set up 128K mode (unlocked paging)
    _context->emulatorState.p7FFD = 0x00;  // No lock bit
    _context->pPortDecoder->UnlockPaging();
    
    std::string tempPath = "/tmp/test_determine_128k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    SNA_MODE mode = loader.determineOutputFormat();
    EXPECT_EQ(mode, SNA_128) << "Unlocked paging should result in 128K format";
}

TEST_F(LoaderSNA_Test, isPageEmpty_AllZeros)
{
    // Explicitly zero out page 0 for this test
    Memory& memory = *_context->pMemory;
    memset(memory.RAMPageAddress(0), 0, PAGE_SIZE);
    
    std::string tempPath = "/tmp/test_empty_page.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    // Page 0 should now be empty after explicit zeroing
    bool isEmpty = loader.isPageEmpty(0);
    EXPECT_TRUE(isEmpty) << "Page 0 should be empty after zeroing";
}

TEST_F(LoaderSNA_Test, isPageEmpty_Randomized)
{
    std::string tempPath = "/tmp/test_randomized_page.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    // Page 5 should NOT be empty (randomized during init)
    bool isEmpty = loader.isPageEmpty(5);
    EXPECT_FALSE(isEmpty) << "Page 5 should not be empty (randomized)";
}

// NOTE: Save tests require full EmulatorContext initialization (pCore, pMemory).
// The test fixture initializes Core but not Screen - save handles Screen being null.

TEST_F(LoaderSNA_Test, save48kBasic)
{
    // Set up 48K locked mode
    _context->emulatorState.p7FFD = 0x20;
    _context->pPortDecoder->LockPaging();
    
    std::string tempPath = "/tmp/test_save_48k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    bool result = loader.save();
    ASSERT_TRUE(result) << "save() should succeed for 48K snapshot";
    
    // Verify file exists and has correct size
    FILE* file = fopen(tempPath.c_str(), "rb");
    ASSERT_NE(file, nullptr) << "Saved file should exist";
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);
    
    EXPECT_EQ(fileSize, 49179) << "48K SNA should be exactly 49179 bytes";
    
    // Clean up
    remove(tempPath.c_str());
}

TEST_F(LoaderSNA_Test, save128kBasic)
{
    // Set up 128K unlocked mode
    _context->emulatorState.p7FFD = 0x00;
    _context->pPortDecoder->UnlockPaging();
    
    std::string tempPath = "/tmp/test_save_128k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    bool result = loader.save();
    ASSERT_TRUE(result) << "save() should succeed for 128K snapshot";
    
    // Verify file exists and has correct minimum size
    FILE* file = fopen(tempPath.c_str(), "rb");
    ASSERT_NE(file, nullptr) << "Saved file should exist";
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);
    
    // 128K SNA: header(27) + 3 pages(49152) + ext header(4) + 5 remaining pages(81920) = 131103
    EXPECT_EQ(fileSize, 131103) << "128K SNA should be exactly 131103 bytes";
    
    // Clean up
    remove(tempPath.c_str());
}

TEST_F(LoaderSNA_Test, saveAndLoadRoundtrip48k)
{
    // Set up 48K mode with specific register values
    _context->emulatorState.p7FFD = 0x20;
    _context->pPortDecoder->LockPaging();
    
    Z80& z80 = *_context->pCore->GetZ80();
    z80.a = 0xAB;
    z80.bc = 0x1234;
    z80.de = 0x5678;
    z80.hl = 0x9ABC;
    z80.pc = 0x8000;
    z80.sp = 0xFF00;
    z80.im = 1;
    
    std::string tempPath = "/tmp/test_roundtrip_48k.sna";
    
    // Save
    {
        LoaderSNACUT saver(_context, tempPath);
        bool saveResult = saver.save();
        ASSERT_TRUE(saveResult) << "Save should succeed";
    }
    
    // Reset registers to different values
    z80.a = 0x00;
    z80.bc = 0x0000;
    z80.de = 0x0000;
    z80.hl = 0x0000;
    
    // Load back
    {
        LoaderSNACUT loader(_context, tempPath);
        bool loadResult = loader.load();
        ASSERT_TRUE(loadResult) << "Load should succeed";
    }
    
    // Verify registers restored (note: 48K PC comes from stack)
    EXPECT_EQ(z80.a, 0xAB) << "Register A should be restored";
    EXPECT_EQ(z80.bc, 0x1234) << "Register BC should be restored";
    EXPECT_EQ(z80.de, 0x5678) << "Register DE should be restored";
    EXPECT_EQ(z80.hl, 0x9ABC) << "Register HL should be restored";
    
    // Clean up
    remove(tempPath.c_str());
}

// File size sanity tests - prevent oversized snapshots (e.g. 4MB extended memory)
TEST_F(LoaderSNA_Test, save48kFileSizeExact)
{
    // Set up 48K locked mode
    _context->emulatorState.p7FFD = 0x20;
    _context->pPortDecoder->LockPaging();
    
    std::string tempPath = "/tmp/test_size_48k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    bool result = loader.save();
    ASSERT_TRUE(result);
    
    FILE* file = fopen(tempPath.c_str(), "rb");
    ASSERT_NE(file, nullptr);
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);
    
    // 48K SNA: 27 (header) + 49152 (3 pages) = 49179 bytes exactly
    EXPECT_EQ(fileSize, 49179) << "48K SNA must be exactly 49179 bytes";
    EXPECT_LT(fileSize, 100000) << "48K SNA should never exceed 100KB (sanity check)";
    
    remove(tempPath.c_str());
}

TEST_F(LoaderSNA_Test, save128kFileSizeExact)
{
    // Set up 128K unlocked mode
    _context->emulatorState.p7FFD = 0x00;
    _context->pPortDecoder->UnlockPaging();
    
    std::string tempPath = "/tmp/test_size_128k.sna";
    LoaderSNACUT loader(_context, tempPath);
    
    bool result = loader.save();
    ASSERT_TRUE(result);
    
    FILE* file = fopen(tempPath.c_str(), "rb");
    ASSERT_NE(file, nullptr);
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);
    
    // 128K SNA: 27 (header) + 49152 (3 pages) + 4 (ext header) + 81920 (5 pages) = 131103 bytes exactly
    EXPECT_EQ(fileSize, 131103) << "128K SNA must be exactly 131103 bytes";
    EXPECT_LT(fileSize, 200000) << "128K SNA should never exceed 200KB (sanity check)";
    
    remove(tempPath.c_str());
}

/// endregion </Save Tests>