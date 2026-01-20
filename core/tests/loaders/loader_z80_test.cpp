#include "loader_z80_test.h"


#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "_helpers/test_path_helper.h"
#include "loader_z80_fuzzing_test.h"  // Includes LoaderZ80CUT

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

/// region <Additional Version-Specific Tests>

TEST_F(LoaderZ80_Test, validateV3Snapshot)
{
    // dizzyx.z80 is a v3 format file (extendedHeaderLen = 54)
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);

    bool result = loader.validate();
    EXPECT_TRUE(result) << "Failed to validate v3 snapshot: " << absoluteSnapshotPath;
    EXPECT_TRUE(loader._fileValidated);
}

TEST_F(LoaderZ80_Test, loadV3Snapshot)
{
    // dizzyx.z80 is a v3 format file
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.load();
    EXPECT_TRUE(result) << "Unable to load v3 snapshot: " << absoluteSnapshotPath;
}

TEST_F(LoaderZ80_Test, load128KSnapshot)
{
    // BBG128.z80 is a 128K mode snapshot
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testSnapshotPath);

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.load();
    EXPECT_TRUE(result) << "Unable to load 128K snapshot: " << absoluteSnapshotPath;
}

/// endregion </Additional Version-Specific Tests>

/// region <Invalid File Handling Tests>

TEST_F(LoaderZ80_Test, rejectEmptyFile)
{
    // Empty file should be rejected without crashing
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/empty.z80");

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Empty file should be rejected";
    EXPECT_FALSE(loader._fileValidated);
}

TEST_F(LoaderZ80_Test, rejectTruncatedHeader)
{
    // File smaller than 30-byte header should be rejected
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/truncated_header.z80");

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Truncated header file should be rejected";
    EXPECT_FALSE(loader._fileValidated);
}

TEST_F(LoaderZ80_Test, rejectInvalidExtendedHeaderLen)
{
    // File with invalid extended header length should be rejected
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_extlen.z80");

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Invalid extended header length should be rejected";
}

TEST_F(LoaderZ80_Test, handleNonExistentFile)
{
    // Non-existent file should be rejected gracefully
    static std::string nonExistentPath = TestPathHelper::GetTestDataPath("loaders/z80/this_file_does_not_exist.z80");

    LoaderZ80CUT loader(_context, nonExistentPath);
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Non-existent file should be rejected";
}

TEST_F(LoaderZ80_Test, handleTruncatedV1Snapshot)
{
    // Truncated v1 file should still validate (header is valid)
    // but load should handle gracefully - the v1 loader zeros remaining bytes
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/truncated_v1.z80");

    LoaderZ80CUT loader(_context, testSnapshotPath);
    bool validateResult = loader.validate();
    EXPECT_TRUE(validateResult) << "Truncated v1 with valid header should validate";
    
    // Load should succeed but with zeroed pages for missing data
    bool loadResult = loader.load();
    EXPECT_TRUE(loadResult) << "Truncated v1 should load (with zeroed missing data)";
}

TEST_F(LoaderZ80_Test, handleTruncatedV2Snapshot)
{
    // Truncated v2 snapshot - should validate but handle missing pages gracefully
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/truncated_v2.z80");

    LoaderZ80CUT loader(_context, testSnapshotPath);
    // Validation may or may not pass depending on header completeness
    // The important thing is no crash
    bool validateResult = loader.validate();
    
    if (validateResult)
    {
        // If validation passes, load should handle truncated data gracefully
        bool loadResult = loader.load();
        // Load may fail but should not crash
        (void)loadResult;  // Just verify no crash
    }
}

TEST_F(LoaderZ80_Test, rejectMarkdownFile)
{
    // Synthetic markdown file disguised as .z80
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_markdown.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Markdown file should be rejected";
}

TEST_F(LoaderZ80_Test, rejectTextFile)
{
    // Synthetic text file disguised as .z80
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_text.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "Text file should be rejected";
}

TEST_F(LoaderZ80_Test, rejectPngFile)
{
    // Synthetic PNG file disguised as .z80
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_png.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "PNG file should be rejected";
}

TEST_F(LoaderZ80_Test, rejectJpegFile)
{
    // Synthetic JPEG file disguised as .z80
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_jpeg.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "JPEG file should be rejected";
}

TEST_F(LoaderZ80_Test, rejectGifFile)
{
    // Synthetic GIF file disguised as .z80  
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_gif.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "GIF file should be rejected";
}

TEST_F(LoaderZ80_Test, rejectInvalidIFFFlags)
{
    // File with correct size but invalid IFF flags (> 1)
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/invalid/invalid_header_size.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    bool result = loader.validate();
    EXPECT_FALSE(result) << "File with invalid IFF flags should be rejected";
}

/// endregion </Invalid File Handling Tests>
/// region <State-Independent Loading Tests>

TEST_F(LoaderZ80_Test, load128KAfterLockedPort)
{
    // Test loading 128K snapshot when port 7FFD is pre-locked via LockPaging
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    
    // Lock using new API
    PortDecoder& ports = *_context->pPortDecoder;
    ports.LockPaging();
    
    // Verify locked
    ASSERT_TRUE(_context->emulatorState.p7FFD & PORT_7FFD_LOCK) << "Port should be locked";
    
    LoaderZ80CUT loader(_context, testSnapshotPath);
    EXPECT_TRUE(loader.load()) << "128K should load when port locked";
    
    Memory& memory = *_context->pMemory;
    uint16_t bank3 = memory.GetRAMPageForBank3();
    // BBG128 has RAM bank 0 in bank 3 (port 7FFD bits 0-2 = 0)
    EXPECT_EQ(bank3, 0) << "Bank 3 should be RAM page 0 from BBG128"; 
}

TEST_F(LoaderZ80_Test, load48KAfter128K)
{
    // Test state doesn't leak between snapshots
    static std::string test128 = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    static std::string test48 = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    
    LoaderZ80CUT loader128(_context, test128);
    EXPECT_TRUE(loader128.load());
    
    LoaderZ80CUT loader48(_context, test48);
    EXPECT_TRUE(loader48.load());
    
    EXPECT_EQ(_context->pMemory->GetRAMPageForBank3(), 0);
}

/// endregion </State-Independent Loading Tests>

/// region <Unlock Verification Tests>

TEST_F(LoaderZ80_Test, load128KWithPreLockedPort)
{
    // Verify unlock mechanism: pre-lock port, load snapshot, verify it unlocks and configures
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    
    // Pre-lock port 7FFD to prevent bank changes
    // NOTE: Directly set emulatorState since port decoder doesn't update it in test context
    uint8_t lockedValue = PORT_7FFD_RAM_BANK_0 | PORT_7FFD_SCREEN_NORMAL | PORT_7FFD_ROM_BANK_1 | PORT_7FFD_LOCK;
    _context->emulatorState.p7FFD = lockedValue;
    
    // Verify port is locked
    ASSERT_TRUE(_context->emulatorState.p7FFD & PORT_7FFD_LOCK) << "Port should be locked before load";
    
    // Load snapshot - should unlock and configure correctly
    LoaderZ80CUT loader(_context, testPath);
    ASSERT_TRUE(loader.load()) << "Should load despite locked port";
    
    // Verify snapshot configuration was applied (BBG128 uses RAM bank 0 in bank 3)
    Memory& memory = *_context->pMemory;
    uint16_t bank3 = memory.GetRAMPageForBank3();
    EXPECT_EQ(bank3, 0) << "Bank 3 should be RAM page 0 from BBG128 snapshot";
}

TEST_F(LoaderZ80_Test, load48KAfterLocked128K)
{
    // Verify state reset: load locked 128K, then load 48K  
    static std::string test128 = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    static std::string test48 = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    
    // Load 128K (which has locked port in snapshot)
    LoaderZ80CUT loader128(_context, test128);
    ASSERT_TRUE(loader128.load());
    
    // Verify port is locked after 128K load (BBG128 has lock bit set)
    EXPECT_TRUE(_context->emulatorState.p7FFD & PORT_7FFD_LOCK) << "BBG128 should lock port";
    
    // Load 48K snapshot - should unlock and configure to 48K
    LoaderZ80CUT loader48(_context, test48);
    ASSERT_TRUE(loader48.load()) << "48K should load after locked 128K";
    
    // Verify 48K configuration
    Memory& memory = *_context->pMemory;
    EXPECT_EQ(memory.GetRAMPageForBank3(), 0) << "48K uses RAM page 0 in bank 3";
    EXPECT_TRUE(memory.IsBank0ROM()) << "Bank 0 should be ROM";
}

TEST_F(LoaderZ80_Test, repeatedLockedLoads)
{
    // Verify repeated loads with different lock states work
    static std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    
    for (int i = 0; i < 3; i++)
    {
        // Alternate between locked and unlocked states
        if (i % 2 == 0)
        {
            uint8_t lockedPort = PORT_7FFD_RAM_BANK_7 | PORT_7FFD_SCREEN_SHADOW | PORT_7FFD_LOCK;
            _context->emulatorState.p7FFD = lockedPort;
        }
        
        LoaderZ80CUT loader(_context, testPath);
        ASSERT_TRUE(loader.load()) << "Load " << i << " should succeed";
        
        // Verify consistent configuration each time
        Memory& memory = *_context->pMemory;
        EXPECT_EQ(memory.GetRAMPageForBank3(), 0) << "Iteration " << i << " bank 3 incorrect";
    }
}

/// endregion </Unlock Verification Tests>
