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

/// region <Compression Tests>

TEST_F(LoaderZ80_Test, compressPageBasic)
{
    // Test basic RLE compression: sequence of identical bytes
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    // Create source with 10 zeros (should compress to ED ED 0A 00)
    uint8_t src[PAGE_SIZE];
    memset(src, 0x00, PAGE_SIZE);
    
    uint8_t compressed[PAGE_SIZE + 1024];  // Extra space for worst-case expansion
    memset(compressed, 0xFF, sizeof(compressed));
    
    loader.compressPage(src, PAGE_SIZE, compressed, sizeof(compressed));
    
    // Verify compression produced valid RLE sequence
    // First 4 bytes should be ED ED <count> 00 for initial zeros
    EXPECT_EQ(compressed[0], 0xED);
    EXPECT_EQ(compressed[1], 0xED);
    EXPECT_GT(compressed[2], 4);  // Count should be > 4 for compression to activate
    EXPECT_EQ(compressed[3], 0x00);  // Value being repeated
}

TEST_F(LoaderZ80_Test, compressPageEDHandling)
{
    // Test special ED sequence handling: even 2 EDs must encode as ED ED 02 ED
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    // Create source with ED ED pattern
    uint8_t src[16];
    memset(src, 0x00, sizeof(src));
    src[0] = 0xED;
    src[1] = 0xED;
    
    uint8_t compressed[64];
    memset(compressed, 0xFF, sizeof(compressed));
    
    loader.compressPage(src, sizeof(src), compressed, sizeof(compressed));
    
    // First 4 bytes should be ED ED 02 ED (encoding the two ED bytes)
    EXPECT_EQ(compressed[0], 0xED);
    EXPECT_EQ(compressed[1], 0xED);
    EXPECT_EQ(compressed[2], 0x02);
    EXPECT_EQ(compressed[3], 0xED);
}

TEST_F(LoaderZ80_Test, compressDecompressRoundtrip)
{
    // CRITICAL: Compress then decompress must produce IDENTICAL data
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    // Create realistic test data with mixed patterns
    uint8_t original[PAGE_SIZE];
    for (size_t i = 0; i < PAGE_SIZE; i++)
    {
        if (i < 1000)
            original[i] = 0x00;  // Initial zeros (compressible)
        else if (i < 2000)
            original[i] = static_cast<uint8_t>(i & 0xFF);  // Random-ish (not compressible)
        else if (i < 3000)
            original[i] = 0xFF;  // More repeated bytes
        else if (i < 3010)
            original[i] = 0xED;  // ED sequences (special case)
        else
            original[i] = static_cast<uint8_t>((i * 7) & 0xFF);  // More varied data
    }
    
    // Compress
    uint8_t compressed[PAGE_SIZE * 2];  // Worst case: no compression + overhead
    memset(compressed, 0xAA, sizeof(compressed));
    loader.compressPage(original, PAGE_SIZE, compressed, sizeof(compressed));
    
    // Decompress
    uint8_t decompressed[PAGE_SIZE];
    memset(decompressed, 0xBB, sizeof(decompressed));
    loader.decompressPage(compressed, sizeof(compressed), decompressed, PAGE_SIZE);
    
    // VERIFY: Must be byte-for-byte identical
    EXPECT_EQ(memcmp(original, decompressed, PAGE_SIZE), 0) 
        << "Roundtrip failed: decompressed data does not match original";
}

TEST_F(LoaderZ80_Test, compressDecompressRoundtripLargeData)
{
    // Test roundtrip with full page size of realistic data patterns
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    // Generate realistic game-like data patterns
    uint8_t original[PAGE_SIZE];
    
    // Simulate typical ZX Spectrum memory: screen area (mostly zeros/attrs) + code + data
    for (size_t i = 0; i < PAGE_SIZE; i++)
    {
        if (i < 6144)
        {
            // Screen bitmap area - often has patterns
            original[i] = static_cast<uint8_t>((i / 32) % 256);
        }
        else if (i < 6912)
        {
            // Attribute area - typically lots of repeated colors
            original[i] = 0x38;  // White on black - common default
        }
        else
        {
            // Code/data area - mix of everything including ED opcodes
            original[i] = static_cast<uint8_t>((i * 13 + 7) % 256);
        }
    }
    
    // Inject some ED sequences (Z80 prefixes) to test special handling
    original[7000] = 0xED;
    original[7001] = 0xED;
    original[7002] = 0xED;
    
    // Compress
    uint8_t compressed[PAGE_SIZE * 2];
    loader.compressPage(original, PAGE_SIZE, compressed, sizeof(compressed));
    
    // Decompress  
    uint8_t decompressed[PAGE_SIZE];
    loader.decompressPage(compressed, sizeof(compressed), decompressed, PAGE_SIZE);
    
    // Verify identical
    EXPECT_EQ(memcmp(original, decompressed, PAGE_SIZE), 0)
        << "Large data roundtrip failed";
}

TEST_F(LoaderZ80_Test, compressPageNoCompression)
{
    // Data with no repeats should pass through (possibly slightly larger)
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    LoaderZ80CUT loader(_context, testSnapshotPath);
    
    // Create non-repeating data (except ED must still be escaped)
    uint8_t src[256];
    for (size_t i = 0; i < sizeof(src); i++)
    {
        src[i] = static_cast<uint8_t>(i);  // 0x00-0xFF, no 5+ repeats
    }
    
    uint8_t compressed[512];
    memset(compressed, 0xFF, sizeof(compressed));
    loader.compressPage(src, sizeof(src), compressed, sizeof(compressed));
    
    // Decompress and verify
    uint8_t decompressed[256];
    loader.decompressPage(compressed, sizeof(compressed), decompressed, sizeof(decompressed));
    
    EXPECT_EQ(memcmp(src, decompressed, sizeof(src)), 0)
        << "Non-repeating data roundtrip failed";
}

/// endregion </Compression Tests>

/// region <Save Tests>

TEST_F(LoaderZ80_Test, saveBasic)
{
    // Load a snapshot, then save it to a new file
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    std::string savePath = TestPathHelper::GetTestDataPath("loaders/z80/test_save_output.z80");
    
    // Load original snapshot
    LoaderZ80CUT loader(_context, testSnapshotPath);
    ASSERT_TRUE(loader.load()) << "Failed to load test snapshot";
    
    // Save to new file
    LoaderZ80CUT saver(_context, savePath);
    bool saveResult = saver.save();
    EXPECT_TRUE(saveResult) << "Save failed";
    
    // Verify file was created
    FILE* f = fopen(savePath.c_str(), "rb");
    EXPECT_NE(f, nullptr) << "Saved file was not created";
    if (f) fclose(f);
    
    // Cleanup
    remove(savePath.c_str());
}

TEST_F(LoaderZ80_Test, saveAndLoadRoundtrip)
{
    // CRITICAL: Save then load must preserve all state
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    std::string savePath = TestPathHelper::GetTestDataPath("loaders/z80/test_roundtrip.z80");
    
    // Load original snapshot
    LoaderZ80CUT loader1(_context, testSnapshotPath);
    ASSERT_TRUE(loader1.load()) << "Failed to load original snapshot";
    
    // Capture original state
    Z80* z80 = _context->pCore->GetZ80();
    uint16_t orig_pc = z80->pc;
    uint16_t orig_sp = z80->sp;
    uint16_t orig_af = z80->af;
    uint16_t orig_bc = z80->bc;
    uint16_t orig_de = z80->de;
    uint16_t orig_hl = z80->hl;
    
    // Save to new file
    LoaderZ80CUT saver(_context, savePath);
    ASSERT_TRUE(saver.save()) << "Save failed";
    
    // Reset emulator state
    _cpu->Reset();
    
    // Load the saved file
    LoaderZ80CUT loader2(_context, savePath);
    ASSERT_TRUE(loader2.load()) << "Failed to load saved snapshot";
    
    // Verify registers match
    EXPECT_EQ(z80->pc, orig_pc) << "PC mismatch after roundtrip";
    EXPECT_EQ(z80->sp, orig_sp) << "SP mismatch after roundtrip";
    EXPECT_EQ(z80->af, orig_af) << "AF mismatch after roundtrip";
    EXPECT_EQ(z80->bc, orig_bc) << "BC mismatch after roundtrip";
    EXPECT_EQ(z80->de, orig_de) << "DE mismatch after roundtrip";
    EXPECT_EQ(z80->hl, orig_hl) << "HL mismatch after roundtrip";
    
    // Cleanup
    remove(savePath.c_str());
}

TEST_F(LoaderZ80_Test, savedFileIsValidZ80)
{
    // Verify saved file can be validated as a proper Z80 format
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    std::string savePath = TestPathHelper::GetTestDataPath("loaders/z80/test_validity.z80");
    
    // Load 128K snapshot
    LoaderZ80CUT loader1(_context, testSnapshotPath);
    ASSERT_TRUE(loader1.load()) << "Failed to load 128K snapshot";
    
    // Save it
    LoaderZ80CUT saver(_context, savePath);
    ASSERT_TRUE(saver.save()) << "Save failed";
    
    // Validate the saved file
    LoaderZ80CUT validator(_context, savePath);
    EXPECT_TRUE(validator.validate()) << "Saved file failed validation";
    
    // Cleanup
    remove(savePath.c_str());
}

/// endregion </Save Tests>


