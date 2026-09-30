#include "loader_z80_test.h"

#include <fstream>


#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "_helpers/testpathhelper.h"
#include "loader_z80_fuzzing_test.h"  // Includes LoaderZ80CUT
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

namespace
{
    /// Deletes the file at the given path when it goes out of scope, on every
    /// exit path (assertion failure included: ASSERT_* returns out of the
    /// TEST_F body, which still unwinds locals normally - only an uncaught
    /// exception or a whole-process crash skips this, same as any destructor).
    /// A bare remove() call at the end of the test - the previous pattern -
    /// never ran once an earlier ASSERT_TRUE failed, leaving scratch files
    /// behind.
    class ScopedTestFile
    {
    public:
        explicit ScopedTestFile(std::string path) : _path(std::move(path)) {}
        ~ScopedTestFile() { std::remove(_path.c_str()); }

        ScopedTestFile(const ScopedTestFile&) = delete;
        ScopedTestFile& operator=(const ScopedTestFile&) = delete;

        const std::string& path() const { return _path; }
        const char* c_str() const { return _path.c_str(); }
        operator const std::string&() const { return _path; }

        friend bool operator==(const ScopedTestFile& a, const std::string& b) { return a._path == b; }
        friend bool operator==(const std::string& a, const ScopedTestFile& b) { return a == b._path; }
        friend bool operator!=(const ScopedTestFile& a, const std::string& b) { return !(a == b); }
        friend bool operator!=(const std::string& a, const ScopedTestFile& b) { return !(a == b); }

    private:
        std::string _path;
    };
}

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
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_save_output.z80"));

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
}

TEST_F(LoaderZ80_Test, saveAndLoadRoundtrip)
{
    // CRITICAL: Save then load must preserve all state
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_roundtrip.z80"));

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
}

/// R in a .z80: byte 11 holds bits 0-6, bit 7 of R is flags bit 0. The core keeps bit 7 in r_hi; r_low's own
/// bit 7 is not part of R and must not reach the file
TEST_F(LoaderZ80_Test, saveWritesRBit7OnlyThroughTheFlags)
{
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_r_bit7.z80"));

    LoaderZ80CUT loader(_context, testSnapshotPath);
    ASSERT_TRUE(loader.load());
    Z80* z80 = _context->pCore->GetZ80();

    for (uint8_t rHi : { uint8_t(0x00), uint8_t(0x80) })
    {
        z80->r_low = 0x85;  // counter 05h, a stale bit 7 in r_low
        z80->r_hi = rHi;

        LoaderZ80CUT saver(_context, savePath);
        ASSERT_TRUE(saver.save());
        std::ifstream file(savePath.path(), std::ios::binary);
        uint8_t header[13] = {};
        file.read(reinterpret_cast<char*>(header), sizeof header);
        EXPECT_EQ(header[11], 0x05) << "byte 11: bits 0-6 only (r_hi " << int(rHi) << ")";
        EXPECT_EQ(header[12] & 0x01, rHi >> 7) << "flags bit 0 = R bit 7";

        LoaderZ80CUT reloader(_context, savePath);
        ASSERT_TRUE(reloader.load());
        EXPECT_EQ((z80->r_low & 0x7F) | (z80->r_hi & 0x80), 0x05 | rHi) << "R after the round trip";
    }
}

TEST_F(LoaderZ80_Test, savedFileIsValidZ80)
{
    // Verify saved file can be validated as a proper Z80 format
    static std::string testSnapshotPath = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_validity.z80"));

    // Load 128K snapshot
    LoaderZ80CUT loader1(_context, testSnapshotPath);
    ASSERT_TRUE(loader1.load()) << "Failed to load 128K snapshot";

    // Save it
    LoaderZ80CUT saver(_context, savePath);
    ASSERT_TRUE(saver.save()) << "Save failed";

    // Validate the saved file
    LoaderZ80CUT validator(_context, savePath);
    EXPECT_TRUE(validator.validate()) << "Saved file failed validation";
}

/// endregion </Save Tests>

/// region <AY state>

namespace
{
    /// Header bytes of a v2 / v3 .z80: 37 = flags 2 (bit 2 "AY sound in use, even on 48K machines"),
    /// 38 = selected AY register, 39-54 = the 16 AY registers
    constexpr size_t Z80_FLAGS2_OFFSET = 37;
    constexpr size_t Z80_AY_SELECT_OFFSET = 38;
    constexpr size_t Z80_AY_REGISTERS_OFFSET = 39;
    constexpr uint8_t Z80_FLAGS2_AY_IN_USE = 0x04;

    std::vector<uint8_t> ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    void WriteWholeFile(const std::string& path, const std::vector<uint8_t>& data)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

/// A 128K-class snapshot carries the AY registers and the selected register; loading must put them into the
/// chip (before the fix the loader restored only the selection and left the reset chip silent)
TEST_F(LoaderZ80_Test, loadRestoresAYRegistersFrom128KSnapshot)
{
    // dizzyx.z80: v3, Pentagon, AY in use, non-zero AY registers
    const std::string path = TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80");
    const std::vector<uint8_t> file = ReadWholeFile(path);
    ASSERT_GE(file.size(), Z80_AY_REGISTERS_OFFSET + 16u);

    LoaderZ80CUT loader(_context, path);
    ASSERT_TRUE(loader.load());

    SoundChip_AY8910* psg = _context->pSoundManager->getAYChip(0);
    ASSERT_NE(psg, nullptr);
    for (uint8_t reg = 0; reg < 16; reg++)
    {
        EXPECT_EQ(psg->readRegister(reg), file[Z80_AY_REGISTERS_OFFSET + reg]) << "AY register " << int(reg);
    }
    EXPECT_EQ(psg->getCurrentRegister(), file[Z80_AY_SELECT_OFFSET] & 0x0F) << "selected AY register";
}

/// Save then load keeps every AY register and the selection
TEST_F(LoaderZ80_Test, saveAndLoadRoundtripKeepsAYRegisters)
{
    const std::string path = TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_ay_roundtrip.z80"));

    LoaderZ80CUT loader(_context, path);
    ASSERT_TRUE(loader.load());

    SoundChip_AY8910* psg = _context->pSoundManager->getAYChip(0);
    ASSERT_NE(psg, nullptr);
    uint8_t expected[16];
    for (uint8_t reg = 0; reg < 16; reg++)
    {
        expected[reg] = static_cast<uint8_t>(0x11 * reg + 3);
        psg->writeRegister(reg, expected[reg]);
    }
    _context->emulatorState.pFFFD = 0x07;
    psg->setRegister(0x07);

    LoaderZ80CUT saver(_context, savePath);
    ASSERT_TRUE(saver.save());

    _cpu->Reset();
    LoaderZ80CUT reloader(_context, savePath);
    ASSERT_TRUE(reloader.load());

    psg = _context->pSoundManager->getAYChip(0);
    for (uint8_t reg = 0; reg < 16; reg++)
    {
        EXPECT_EQ(psg->readRegister(reg), expected[reg]) << "AY register " << int(reg);
    }
    EXPECT_EQ(psg->getCurrentRegister(), 0x07);
}

/// In a 48K snapshot the AY bytes are state only when flags 2 bit 2 says the AY was in use
TEST_F(LoaderZ80_Test, load48KSnapshotAppliesAYOnlyWhenMarkedInUse)
{
    const std::string source = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_ay_48k.z80"));

    // A 48K-mode v3 file written by the saver
    _context->config.mem_model = MM_SPECTRUM48;
    LoaderZ80CUT loader(_context, source);
    ASSERT_TRUE(loader.load());
    LoaderZ80CUT saver(_context, savePath);
    ASSERT_TRUE(saver.save());

    std::vector<uint8_t> file = ReadWholeFile(savePath);
    ASSERT_GE(file.size(), Z80_AY_REGISTERS_OFFSET + 16u);
    for (uint8_t reg = 0; reg < 16; reg++)
    {
        file[Z80_AY_REGISTERS_OFFSET + reg] = static_cast<uint8_t>(0x20 + reg);
    }

    for (bool inUse : { false, true })
    {
        file[Z80_FLAGS2_OFFSET] = inUse ? Z80_FLAGS2_AY_IN_USE : 0x00;
        WriteWholeFile(savePath, file);

        _cpu->Reset();
        SoundChip_AY8910* psg = _context->pSoundManager->getAYChip(0);
        ASSERT_NE(psg, nullptr);
        uint8_t afterReset[16];
        for (uint8_t reg = 0; reg < 16; reg++)
            afterReset[reg] = psg->readRegister(reg);

        LoaderZ80CUT reloader(_context, savePath);
        ASSERT_TRUE(reloader.load());

        psg = _context->pSoundManager->getAYChip(0);
        for (uint8_t reg = 0; reg < 16; reg++)
        {
            const uint8_t expected = inUse ? static_cast<uint8_t>(0x20 + reg) : afterReset[reg];
            EXPECT_EQ(psg->readRegister(reg), expected) << "AY register " << int(reg) << (inUse ? " (in use)" : " (not in use)");
        }
    }
}

/// The saver marks the AY as in use in a 48K-mode snapshot only when the machine has an AY
TEST_F(LoaderZ80_Test, saveMarksAYInUseFor48KModeOnMachinesWithAY)
{
    const std::string source = TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80");
    ScopedTestFile savePath(TestPathHelper::GetUniqueTestScratchPath("test_ay_flag.z80"));

    LoaderZ80CUT loader(_context, source);
    ASSERT_TRUE(loader.load());

    for (MEM_MODEL model : { MM_SPECTRUM48, MM_PENTAGON })
    {
        _context->config.mem_model = model;
        LoaderZ80CUT saver(_context, savePath);
        ASSERT_TRUE(saver.save());

        const std::vector<uint8_t> file = ReadWholeFile(savePath);
        ASSERT_GT(file.size(), Z80_FLAGS2_OFFSET);
        const bool marked = (file[Z80_FLAGS2_OFFSET] & Z80_FLAGS2_AY_IN_USE) != 0;
        EXPECT_EQ(marked, model != MM_SPECTRUM48) << "model " << int(model);
    }
}

/// endregion </AY state>




/// region <Models: libspectrum files and saving by model>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "_helpers/soundcardscope.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace
{
    struct Z80ModelCase
    {
        const char* file;   ///< testdata/loaders/z80/libspectrum/, written by libspectrum (tools/verification/szx)
        const char* model;
        uint32_t ramKb;
        uint8_t modelCode;  ///< the v3 model byte our save writes
        int pages;          ///< RAM pages in the file
    };

    class LoaderZ80Models_Test : public ::testing::TestWithParam<Z80ModelCase>
    {
    };
}  // namespace

/// libspectrum's .z80 files (known values: #7FFD = #13, page n filled with
/// n * 16 + offset / 1024, #1FFD = #04 on +2A / +3) load on their model with
/// every page and #1FFD, and our save writes the model's own code, #1FFD in a
/// 55-byte header where the model has it, and every page
TEST_P(LoaderZ80Models_Test, LoadsLibspectrumFileAndSavesTheModel)
{
    const Z80ModelCase& param = GetParam();
    EmulatorManager* manager = EmulatorManager::GetInstance();
    SoundCardScope sound(TestSound::TurboSound);  // the AY registers travel too
    auto emulator = manager->CreateEmulatorWithModelAndRAM("z80-model", param.model, param.ramKb, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    const std::string path =
        (TestPathHelper::FindProjectRoot() / "testdata/loaders/z80/libspectrum" / param.file).string();
    ASSERT_TRUE(emulator->LoadSnapshot(path));

    Memory& memory = *context->pMemory;
    Z80& cpu = *context->pCore->GetZ80();
    EXPECT_EQ(cpu.a, 0x11);
    EXPECT_EQ(cpu.pc, 0x8000);
    EXPECT_EQ(cpu.t, LoaderSZX::FramePositionFromIntCount(context, 12345)) << "v3 bytes 55-57: the frame position";
    EXPECT_EQ(context->emulatorState.p7FFD, 0x13);
    EXPECT_EQ(memory.GetRAMPageForBank3(), 3);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xC000), 3 * 16);
    for (int page = 0; page < param.pages; page++)
        EXPECT_EQ(memory.RAMPageAddress(static_cast<uint16_t>(page))[0], page * 16) << "RAM page " << page;
    const bool has1FFD = param.modelCode == 7 || param.modelCode == 13 || param.modelCode == 10;
    if (param.modelCode == 7 || param.modelCode == 13)
        EXPECT_EQ(context->emulatorState.p1FFD, 0x04);

    const std::string saved = TestPathHelper::GetUniqueTestScratchPath("z80-model.z80");
    ASSERT_TRUE(emulator->SaveSnapshot(saved));
    // tools/verification/szx/check-interop.sh: libspectrum reads our file too
    if (const char* folder = std::getenv("UNREALNG_SZX_EXPORT_DIR"); folder && *folder)
        std::filesystem::copy_file(saved, std::filesystem::path(folder) / param.file,
                                   std::filesystem::copy_options::overwrite_existing);
    std::ifstream file(saved, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_GT(bytes.size(), 87u);
    const uint16_t extended = static_cast<uint16_t>(bytes[30] | (bytes[31] << 8));
    EXPECT_EQ(bytes[34], param.modelCode) << "model byte";
    EXPECT_EQ(extended, has1FFD ? 55 : 54);
    if (has1FFD)
        EXPECT_EQ(bytes[86], context->emulatorState.p1FFD);
    // Page blocks after the header: count them
    int pages = 0;
    for (size_t at = 32 + extended; at + 3 <= bytes.size(); pages++)
    {
        const uint16_t length = static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        at += 3 + (length == 0xFFFF ? 16384 : length);
    }
    EXPECT_EQ(pages, param.pages);

    // And it loads back with the same pages
    auto again = manager->CreateEmulatorWithModelAndRAM("z80-model-2", param.model, param.ramKb, LoggerLevel::LogError);
    ASSERT_TRUE(again);
    ASSERT_TRUE(again->LoadSnapshot(saved));
    for (int page = 0; page < param.pages; page++)
        EXPECT_EQ(again->GetContext()->pMemory->RAMPageAddress(static_cast<uint16_t>(page))[1023], page * 16) << page;
    EXPECT_EQ(again->GetContext()->emulatorState.p1FFD, context->emulatorState.p1FFD);
    EXPECT_EQ(again->GetContext()->pCore->GetZ80()->t, cpu.t) << "the frame position round-trips";
    std::remove(saved.c_str());
    manager->RemoveEmulator(again->GetId());
    manager->RemoveEmulator(emulator->GetId());
}

INSTANTIATE_TEST_SUITE_P(Models, LoaderZ80Models_Test,
                         ::testing::Values(Z80ModelCase{"synth-128.z80", "128k", 128, 4, 8},
                                           Z80ModelCase{"synth-plus2a.z80", "PLUS2A", 128, 13, 8},
                                           Z80ModelCase{"synth-plus3.z80", "PLUS3", 128, 7, 8},
                                           Z80ModelCase{"synth-pentagon.z80", "PENTAGON", 128, 9, 8},
                                           Z80ModelCase{"synth-scorpion.z80", "SCORPION", 256, 10, 16}),
                         [](const ::testing::TestParamInfo<Z80ModelCase>& info) {
                             std::string name = info.param.file;
                             name = name.substr(6, name.size() - 10);  // "synth-" ... ".z80"
                             return name;
                         });

/// +3 all-RAM mode (#1FFD bit 0) survives a save and a load
TEST(LoaderZ80Plus3_Test, SpecialPagingRoundTrips)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("z80-p3", "PLUS3", 128, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t page = 0; page < 8; page++)
        std::memset(context->pMemory->RAMPageAddress(page), page * 16, 16384);
    context->pPortDecoder->DecodePortOut(0x1FFD, 0x03, 0);  // special: pages 4, 5, 6, 7
    ASSERT_EQ(context->pMemory->DirectReadFromZ80Memory(0x0000), 4 * 16);
    const std::string saved = TestPathHelper::GetUniqueTestScratchPath("z80-p3.z80");
    ASSERT_TRUE(emulator->SaveSnapshot(saved));

    auto again = manager->CreateEmulatorWithModelAndRAM("z80-p3-2", "PLUS3", 128, LoggerLevel::LogError);
    ASSERT_TRUE(again);
    ASSERT_TRUE(again->LoadSnapshot(saved));
    Memory& memory = *again->GetContext()->pMemory;
    EXPECT_EQ(again->GetContext()->emulatorState.p1FFD, 0x03);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x0000), 4 * 16) << "all-RAM: page 4 at #0000";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x4000), 5 * 16);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xC000), 7 * 16);
    std::remove(saved.c_str());
    manager->RemoveEmulator(again->GetId());
    manager->RemoveEmulator(emulator->GetId());
}

/// endregion </Models: libspectrum files and saving by model>
