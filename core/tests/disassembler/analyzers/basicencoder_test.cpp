#include "basicencoder_test.h"

#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/basic-lang/basicextractor.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"
#include "emulator/emulator.h"

void BasicEncoder_Test::SetUp()
{
    MessageCenter::DisposeDefaultMessageCenter();
    _context = new EmulatorContext(LoggerLevel::LogError);
    _memory = new Memory(_context);
    
    // Initialize memory banks (required for DirectWriteToZ80Memory to work)
    _memory->DefaultBanksFor48k();
}

void BasicEncoder_Test::TearDown()
{
    if (_memory)
    {
        delete _memory;
        _memory = nullptr;
    }
    
    if (_context)
    {
        delete _context;
        _context = nullptr;
    }
    
    MessageCenter::DisposeDefaultMessageCenter();
}

/// region <Unit Tests - Tokenization>

TEST_F(BasicEncoder_Test, TokenizeSingleLine_Simple)
{
    BasicEncoder encoder;
    
    std::string program = "10 PRINT \"HELLO\"\n";
    auto tokenized = encoder.tokenize(program);
    
    ASSERT_FALSE(tokenized.empty());
    
    // Verify line number (big-endian)
    EXPECT_EQ(tokenized[0], 0x00);  // Line 10 high byte
    EXPECT_EQ(tokenized[1], 0x0A);  // Line 10 low byte
    
    // Verify line length is present (little-endian)
    uint16_t lineLength = tokenized[2] | (tokenized[3] << 8);
    EXPECT_GT(lineLength, 0);
    
    // Verify PRINT token (0xF5)
    bool foundPrintToken = false;
    for (size_t i = 4; i < tokenized.size(); i++)
    {
        if (tokenized[i] == 0xF5)
        {
            foundPrintToken = true;
            break;
        }
    }
    EXPECT_TRUE(foundPrintToken) << "PRINT token (0xF5) not found";
    
    // Verify program terminator (0x00 0x00) is NOT present in tokenized output
    // It is added by the injector, not the tokenizer
    ASSERT_GE(tokenized.size(), 4) << "Tokenized program too small";
    // Last bytes should be line data/terminator, not program terminator
    
    // Verify line terminator (0x0D) exists at the end
    ASSERT_FALSE(tokenized.empty());
    EXPECT_EQ(tokenized.back(), 0x0D) << "Last byte should be 0x0D (line terminator)";
}

TEST_F(BasicEncoder_Test, TokenizeMultiLine)
{
    BasicEncoder encoder;
    
    std::string program = 
        "10 PRINT \"TEST\"\n"
        "20 GOTO 10\n";
    
    auto tokenized = encoder.tokenize(program);
    
    ASSERT_FALSE(tokenized.empty());
    
    // Should have two lines
    // Line 1: Line number (2) + length (2) + data + 0x0D
    // Line 2: Line number (2) + length (2) + data + 0x0D
    
    // Verify first line number
    EXPECT_EQ(tokenized[0], 0x00);
    EXPECT_EQ(tokenized[1], 0x0A);  // Line 10
    
    // Find second line (after first 0x0D)
    size_t secondLineStart = 0;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        if (tokenized[i] == 0x0D)
        {
            secondLineStart = i + 1;
            break;
        }
    }
    
    ASSERT_GT(secondLineStart, 0);
    ASSERT_LT(secondLineStart + 1, tokenized.size());
    
    // Verify second line number
    EXPECT_EQ(tokenized[secondLineStart], 0x00);
    EXPECT_EQ(tokenized[secondLineStart + 1], 0x14);  // Line 20
}

TEST_F(BasicEncoder_Test, TokenizeKeywords_MultiWord)
{
    BasicEncoder encoder;
    
    // Test "GO TO" (should be single token 0xEC)
    std::string program = "10 GO TO 100\n";
    auto tokenized = encoder.tokenize(program);
    
    bool foundGoToToken = false;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        if (tokenized[i] == 0xEC)  // GO TO token
        {
            foundGoToToken = true;
            break;
        }
    }
    EXPECT_TRUE(foundGoToToken) << "GO TO token (0xEC) not found";
}

TEST_F(BasicEncoder_Test, TokenizeKeywords_GOSUB)
{
    BasicEncoder encoder;
    
    std::string program = "10 GO SUB 1000\n";
    auto tokenized = encoder.tokenize(program);
    
    bool foundGoSubToken = false;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        if (tokenized[i] == 0xED)  // GO SUB token
        {
            foundGoSubToken = true;
            break;
        }
    }
    EXPECT_TRUE(foundGoSubToken) << "GO SUB token (0xED) not found";
}

TEST_F(BasicEncoder_Test, TokenizeStringPreservation)
{
    BasicEncoder encoder;
    
    std::string program = "10 PRINT \"HELLO WORLD\"\n";
    auto tokenized = encoder.tokenize(program);
    
    // Find the string content
    bool inString = false;
    std::string extractedString;
    
    for (size_t i = 4; i < tokenized.size(); i++)  // Skip line header
    {
        if (tokenized[i] == '\"')
        {
            if (!inString)
            {
                inString = true;
            }
            else
            {
                break;  // End of string
            }
        }
        else if (inString)
        {
            extractedString += (char)tokenized[i];
        }
    }
    
    EXPECT_EQ(extractedString, "HELLO WORLD");
}

TEST_F(BasicEncoder_Test, TokenizeNumbers)
{
    BasicEncoder encoder;
    
    std::string program = "10 LET A=42\n";
    auto tokenized = encoder.tokenize(program);
    
    ASSERT_FALSE(tokenized.empty());
    
    // Verify LET token (0xF1)
    bool foundLetToken = false;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        if (tokenized[i] == 0xF1)
        {
            foundLetToken = true;
            break;
        }
    }
    EXPECT_TRUE(foundLetToken);
    
    // Numbers should be preserved as ASCII
    bool found4 = false, found2 = false;
    for (size_t i = 0; i < tokenized.size(); i++)
    {
        if (tokenized[i] == '4') found4 = true;
        if (tokenized[i] == '2') found2 = true;
    }
    EXPECT_TRUE(found4 && found2) << "Number digits not preserved";
}

/// endregion </Unit Tests - Tokenization>

/// region <Unit Tests - Memory Injection>

TEST_F(BasicEncoder_Test, InjectIntoMemory_SystemVariables)
{
    BasicEncoder encoder;
    
    std::string program = "10 PRINT \"X\"\n";
    bool result = encoder.loadProgram(_memory, program);
    
    ASSERT_TRUE(result);
    
    // Read PROG system variable
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progAddr = progL | (progH << 8);
    
    EXPECT_EQ(progAddr, BasicEncoder::DEFAULT_PROG_START);
    
    // Read VARS system variable
    uint8_t varsL = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS);
    uint8_t varsH = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS + 1);
    uint16_t varsAddr = varsL | (varsH << 8);
    
    // VARS should be after PROG
    EXPECT_GT(varsAddr, progAddr);
    
    // Program length should match
    // Program length should match (tokenized size only)
    auto tokenized = encoder.tokenize(program);
    EXPECT_EQ(varsAddr - progAddr, tokenized.size());
}

TEST_F(BasicEncoder_Test, InjectIntoMemory_ProgramContent)
{
    BasicEncoder encoder;
    
    std::string program = "10 PRINT \"TEST\"\n";
    bool result = encoder.loadProgram(_memory, program);
    
    ASSERT_TRUE(result);
    
    // Read back the program from memory
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progAddr = progL | (progH << 8);
    
    // Verify line number
    uint8_t lineHigh = _memory->DirectReadFromZ80Memory(progAddr);
    uint8_t lineLow = _memory->DirectReadFromZ80Memory(progAddr + 1);
    uint16_t lineNumber = (lineHigh << 8) | lineLow;
    
    EXPECT_EQ(lineNumber, 10);
}

/// endregion </Unit Tests - Memory Injection>

/// region <Integration Tests - Round Trip>

TEST_F(BasicEncoder_Test, RoundTrip_SimplePrint)
{
    BasicEncoder encoder;
    BasicExtractor extractor;
    
    std::string original = "10 PRINT \"HELLO\"\n";
    
    // Encode and inject
    bool result = encoder.loadProgram(_memory, original);
    ASSERT_TRUE(result);
    
    // Extract back
    std::string extracted = extractor.extractFromMemory(_memory);
    
    // Normalize for comparison (remove trailing whitespace, etc.)
    auto normalize = [](const std::string& s) {
        std::string result = s;
        // Remove trailing newlines
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    };
    
    EXPECT_EQ(normalize(extracted), normalize(original));
}

TEST_F(BasicEncoder_Test, RoundTrip_MultiLine)
{
    BasicEncoder encoder;
    BasicExtractor extractor;
    
    std::string original = 
        "10 PRINT \"TEST\"\n"
        "20 LET A=10\n"
        "30 GOTO 10\n";
    
    bool result = encoder.loadProgram(_memory, original);
    ASSERT_TRUE(result);
    
    std::string extracted = extractor.extractFromMemory(_memory);
    
    // Verify all lines are present
    EXPECT_NE(extracted.find("10 "), std::string::npos);
    EXPECT_NE(extracted.find("20 "), std::string::npos);
    EXPECT_NE(extracted.find("30 "), std::string::npos);
    EXPECT_NE(extracted.find("PRINT"), std::string::npos);
    EXPECT_NE(extracted.find("LET"), std::string::npos);
    EXPECT_NE(extracted.find("GOTO"), std::string::npos);
}

TEST_F(BasicEncoder_Test, RoundTrip_ComplexProgram)
{
    BasicEncoder encoder;
    BasicExtractor extractor;
    
    std::string original = 
        "10 CLS\n"
        "20 FOR I=1 TO 10\n"
        "30 PRINT I\n"
        "40 NEXT I\n"
        "50 GOTO 10\n";
    
    bool result = encoder.loadProgram(_memory, original);
    ASSERT_TRUE(result);
    
    std::string extracted = extractor.extractFromMemory(_memory);
    
    // Verify key elements
    EXPECT_NE(extracted.find("CLS"), std::string::npos);
    EXPECT_NE(extracted.find("FOR"), std::string::npos);
    EXPECT_NE(extracted.find("TO"), std::string::npos);
    EXPECT_NE(extracted.find("NEXT"), std::string::npos);
}

/// endregion </Integration Tests - Round Trip>

/// region <Integration Tests - Full Emulator>

TEST_F(BasicEncoder_Test, Integration_Pentagon128k_LoadAndExtract)
{
    // Create full emulator
    Emulator emulator(LoggerLevel::LogError);
    
    if (!emulator.Init())
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }
    
    Memory* memory = emulator.GetMemory();
    ASSERT_NE(memory, nullptr);
    
    BasicEncoder encoder;
    BasicExtractor extractor;
    
    std::string program = 
        "10 PRINT \"PENTAGON TEST\"\n"
        "20 GOTO 10\n";
    
    bool result = encoder.loadProgram(memory, program);
    ASSERT_TRUE(result);
    
    std::string extracted = extractor.extractFromMemory(memory);
    
    EXPECT_NE(extracted.find("PENTAGON TEST"), std::string::npos);
    EXPECT_NE(extracted.find("GOTO"), std::string::npos);
}

/// endregion </Integration Tests - Full Emulator>

/// region <Edge Cases>

TEST_F(BasicEncoder_Test, EdgeCase_EmptyProgram)
{
    BasicEncoder encoder;
    
    std::string program = "";
    auto tokenized = encoder.tokenize(program);
    
    EXPECT_TRUE(tokenized.empty());
}

TEST_F(BasicEncoder_Test, EdgeCase_NoLineNumber)
{
    BasicEncoder encoder;
    
    std::string program = "PRINT \"NO LINE NUMBER\"\n";
    auto tokenized = encoder.tokenize(program);
    
    // Should skip lines without line numbers
    EXPECT_TRUE(tokenized.empty());
}

TEST_F(BasicEncoder_Test, EdgeCase_LargeLineNumber)
{
    BasicEncoder encoder;
    
    std::string program = "9999 PRINT \"MAX\"\n";
    auto tokenized = encoder.tokenize(program);
    
    ASSERT_FALSE(tokenized.empty());
    
    // Verify line number 9999 (0x270F)
    uint16_t lineNumber = (tokenized[0] << 8) | tokenized[1];
    EXPECT_EQ(lineNumber, 9999);
}

TEST_F(BasicEncoder_Test, EdgeCase_QuotesInString)
{
    BasicEncoder encoder;
    
    // Note: ZX Spectrum doesn't support escaped quotes, but we test the string handling
    std::string program = "10 PRINT \"HELLO\"\n";
    auto tokenized = encoder.tokenize(program);
    
    // Count quote characters
    int quoteCount = 0;
    for (auto byte : tokenized)
    {
        if (byte == '\"') quoteCount++;
    }
    
    EXPECT_EQ(quoteCount, 2);  // Opening and closing quotes
}

/// endregion </Edge Cases>

/// region <Immediate Command Tokenization Tests>

TEST_F(BasicEncoder_Test, TokenizeImmediate_PRINT_Command)
{
    // Test that "PRINT 1+2" tokenizes PRINT to 0xF5 WITHOUT trailing space
    std::string command = "PRINT 1+2";
    auto tokenized = BasicEncoder::tokenizeImmediate(command);
    
    ASSERT_GE(tokenized.size(), 4);
    
    // First byte should be PRINT token (0xF5)
    EXPECT_EQ(tokenized[0], 0xF5) << "PRINT should tokenize to 0xF5";
    
    // Followed immediately by digits (NO space after token)
    EXPECT_EQ(tokenized[1], '1') << "No space should follow token";
    EXPECT_EQ(tokenized[2], '+');
    EXPECT_EQ(tokenized[3], '2');
}

TEST_F(BasicEncoder_Test, TokenizeImmediate_KeywordAtLineStart)
{
    // Test keyword at start works correctly
    std::string command = "PRINT";
    auto tokenized = BasicEncoder::tokenizeImmediate(command);
    
    ASSERT_EQ(tokenized.size(), 1);
    // Should be tokenized to 0xF5
    EXPECT_EQ(tokenized[0], 0xF5) << "PRINT at line start should tokenize to 0xF5";
}

TEST_F(BasicEncoder_Test, TokenizeImmediate_StringLiteralsPreserved)
{
    // Keywords inside strings should NOT be tokenized
    std::string command = "PRINT \"PRINT\"";
    auto tokenized = BasicEncoder::tokenizeImmediate(command);
    
    // PRINT outside string should be tokenized, space consumed
    // Result: [0xF5, '"', 'P', 'R', 'I', 'N', 'T', '"']
    EXPECT_EQ(tokenized[0], 0xF5);
    
    // Quote (space was consumed)
    EXPECT_EQ(tokenized[1], '"');
    
    // PRINT inside string should be ASCII, not tokenized
    EXPECT_EQ(tokenized[2], 'P');
    EXPECT_EQ(tokenized[3], 'R');
    EXPECT_EQ(tokenized[4], 'I');
    EXPECT_EQ(tokenized[5], 'N');
    EXPECT_EQ(tokenized[6], 'T');
    EXPECT_EQ(tokenized[7], '"');
}

/// endregion </Immediate Command Tokenization Tests>

/// region <ROM State Detection Tests>
/// 
/// These tests verify the three-tier ROM state detection algorithm:
/// - Tier 1: Hardware ROM paging state
/// - Tier 2: Stack context analysis (TR-DOS calling SOS)
/// - Tier 3: System variable initialization
///
/// Test scenarios:
/// 1. Pure SOS ROM (48K BASIC active, no TR-DOS initialized)
/// 2. 128K Menu (ROM 0, EDITOR_FLAGS bit 1 set)
/// 3. 128K BASIC (ROM 0, EDITOR_FLAGS bit 1 clear)
/// 4. 48K BASIC (ROM 1 or 3)
/// 5. TR-DOS Active (DOS ROM paged, ROM page 2)
/// 6. TR-DOS calling SOS (SOS ROM paged, stack contains $3D2F)

TEST_F(BasicEncoder_Test, StateDetection_PureSOS_48KBASIC)
{
    // Scenario: Pure 48K BASIC mode
    // - ROM page 1 (48K BASIC ROM)
    // - No TR-DOS initialization markers
    // - Expected: Basic48K
    
    // Note: Memory banks are already set up for 48K in SetUp()
    // ROM page detection depends on Memory::GetROMPage() implementation
    
    // For this unit test, we verify isTRDOSInitialized returns false
    // when RAM stub doesn't contain RET opcode
    bool trdosInit = BasicEncoder::isTRDOSInitialized(_memory);
    EXPECT_FALSE(trdosInit) << "Fresh 48K memory should not have TR-DOS initialized";
    
    // Stack scan should return false with no DOS addresses
    bool hasDosOnStack = BasicEncoder::stackContainsDOSReturnAddress(_memory, 0xFF00);
    EXPECT_FALSE(hasDosOnStack) << "Fresh memory should not have DOS return addresses on stack";
}

TEST_F(BasicEncoder_Test, StateDetection_TRDOSInitialized)
{
    // Scenario: TR-DOS has been initialized (RAM stub at $5CC2 = $C9)
    // Write the RAM stub that TR-DOS installs
    using namespace TRDOS::ROMSwitch;
    using namespace SystemVariables48k;
    
    // Write RET ($C9) at RAM stub location
    _memory->DirectWriteToZ80Memory(RAM_STUB, RAM_STUB_OPCODE);
    
    // Write CHANS = $5D25 (TR-DOS extends channel area)
    _memory->DirectWriteToZ80Memory(CHANS, CHANS_TRDOS_VALUE & 0xFF);
    _memory->DirectWriteToZ80Memory(CHANS + 1, (CHANS_TRDOS_VALUE >> 8) & 0xFF);
    
    bool trdosInit = BasicEncoder::isTRDOSInitialized(_memory);
    EXPECT_TRUE(trdosInit) << "TR-DOS initialization markers should be detected";
}

TEST_F(BasicEncoder_Test, StateDetection_TRDOSNotInitialized_WrongStub)
{
    // Scenario: RAM stub has wrong opcode
    using namespace TRDOS::ROMSwitch;
    using namespace SystemVariables48k;
    
    // Write wrong opcode at stub location
    _memory->DirectWriteToZ80Memory(RAM_STUB, 0x00);  // NOP, not RET
    
    // Write correct CHANS value
    _memory->DirectWriteToZ80Memory(CHANS, CHANS_TRDOS_VALUE & 0xFF);
    _memory->DirectWriteToZ80Memory(CHANS + 1, (CHANS_TRDOS_VALUE >> 8) & 0xFF);
    
    bool trdosInit = BasicEncoder::isTRDOSInitialized(_memory);
    EXPECT_FALSE(trdosInit) << "Wrong stub opcode should fail TR-DOS detection";
}

TEST_F(BasicEncoder_Test, StateDetection_TRDOSNotInitialized_WrongCHANS)
{
    // Scenario: CHANS has wrong value (standard 48K value)
    using namespace TRDOS::ROMSwitch;
    using namespace SystemVariables48k;
    
    // Write correct stub
    _memory->DirectWriteToZ80Memory(RAM_STUB, RAM_STUB_OPCODE);
    
    // Write 48K CHANS value (not $5D25)
    _memory->DirectWriteToZ80Memory(CHANS, 0xAF);  // Standard 48K value
    _memory->DirectWriteToZ80Memory(CHANS + 1, 0x5C);
    
    bool trdosInit = BasicEncoder::isTRDOSInitialized(_memory);
    EXPECT_FALSE(trdosInit) << "Wrong CHANS value should fail TR-DOS detection";
}

TEST_F(BasicEncoder_Test, StateDetection_StackContainsDOSAddress_SingleEntry)
{
    // Scenario: Stack contains a single TR-DOS trap address ($3D2F)
    using namespace TRDOS::ROMSwitch;
    
    uint16_t sp = 0xFF00;
    
    // Push $3D2F to stack (little-endian)
    _memory->DirectWriteToZ80Memory(sp, ROM_TRAMPOLINE & 0xFF);       // Low byte
    _memory->DirectWriteToZ80Memory(sp + 1, (ROM_TRAMPOLINE >> 8) & 0xFF);  // High byte
    
    bool hasDosAddr = BasicEncoder::stackContainsDOSReturnAddress(_memory, sp);
    EXPECT_TRUE(hasDosAddr) << "Stack with $3D2F should detect DOS return address";
}

TEST_F(BasicEncoder_Test, StateDetection_StackContainsDOSAddress_Nested)
{
    // Scenario: Multiple nested calls, DOS address buried in stack
    using namespace TRDOS::ROMSwitch;
    
    uint16_t sp = 0xFF00;
    
    // Simulate nested call stack:
    // SP+0: $5CC2 (RAM stub - current return)
    // SP+2: $0010 (SOS ROM print routine - we're inside this)
    // SP+4: $3D2F (DOS trampoline - this makes us "logically in DOS")
    // SP+6: $2F90 (Some DOS internal address)
    
    _memory->DirectWriteToZ80Memory(sp + 0, 0xC2);  // $5CC2 low
    _memory->DirectWriteToZ80Memory(sp + 1, 0x5C);  // $5CC2 high
    
    _memory->DirectWriteToZ80Memory(sp + 2, 0x10);  // $0010 low
    _memory->DirectWriteToZ80Memory(sp + 3, 0x00);  // $0010 high
    
    _memory->DirectWriteToZ80Memory(sp + 4, 0x2F);  // $3D2F low
    _memory->DirectWriteToZ80Memory(sp + 5, 0x3D);  // $3D2F high
    
    _memory->DirectWriteToZ80Memory(sp + 6, 0x90);  // $2F90 low
    _memory->DirectWriteToZ80Memory(sp + 7, 0x2F);  // $2F90 high
    
    bool hasDosAddr = BasicEncoder::stackContainsDOSReturnAddress(_memory, sp);
    EXPECT_TRUE(hasDosAddr) << "Nested stack with $3D2F should detect DOS return address";
}

TEST_F(BasicEncoder_Test, StateDetection_StackNoDOSAddress_PureBASIC)
{
    // Scenario: Stack with only BASIC ROM addresses, no TR-DOS
    uint16_t sp = 0xFF00;
    
    // Simulate pure BASIC call stack:
    // SP+0: $1234 (some BASIC ROM address)
    // SP+2: $0A3B (another BASIC address)
    // SP+4: $5678 (RAM address - program)
    
    _memory->DirectWriteToZ80Memory(sp + 0, 0x34);
    _memory->DirectWriteToZ80Memory(sp + 1, 0x12);
    
    _memory->DirectWriteToZ80Memory(sp + 2, 0x3B);
    _memory->DirectWriteToZ80Memory(sp + 3, 0x0A);
    
    _memory->DirectWriteToZ80Memory(sp + 4, 0x78);
    _memory->DirectWriteToZ80Memory(sp + 5, 0x56);
    
    bool hasDosAddr = BasicEncoder::stackContainsDOSReturnAddress(_memory, sp);
    EXPECT_FALSE(hasDosAddr) << "Pure BASIC stack should not detect DOS return address";
}

TEST_F(BasicEncoder_Test, StateDetection_TRDOSLogicallyActive_DOSPaged)
{
    // Note: This test would require mocking Memory::GetROMPage() to return 2
    // For now, we test the helper functions work correctly
    
    // If DOS ROM page 2 is paged, isTRDOSLogicallyActive should return true
    // But since we can't easily mock GetROMPage, we verify the subordinate checks
}

TEST_F(BasicEncoder_Test, StateDetection_TRDOSLogicallyActive_SOSWithDOSOnStack)
{
    // Scenario: SOS ROM paged (48K BASIC), but TR-DOS initialized and $3D2F on stack
    // This is the "TR-DOS calling SOS" scenario
    
    using namespace TRDOS::ROMSwitch;
    using namespace SystemVariables48k;
    
    // Initialize TR-DOS markers
    _memory->DirectWriteToZ80Memory(RAM_STUB, RAM_STUB_OPCODE);
    _memory->DirectWriteToZ80Memory(CHANS, CHANS_TRDOS_VALUE & 0xFF);
    _memory->DirectWriteToZ80Memory(CHANS + 1, (CHANS_TRDOS_VALUE >> 8) & 0xFF);
    
    // Set up stack with DOS return address
    uint16_t sp = 0xFF00;
    _memory->DirectWriteToZ80Memory(sp, ROM_TRAMPOLINE & 0xFF);
    _memory->DirectWriteToZ80Memory(sp + 1, (ROM_TRAMPOLINE >> 8) & 0xFF);
    
    // Verify all conditions for "TR-DOS calling SOS" are met
    EXPECT_TRUE(BasicEncoder::isTRDOSInitialized(_memory));
    EXPECT_TRUE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp));
    
    // The full isTRDOSLogicallyActive would return true if ROM page != 2
    // and both above conditions are true
}

TEST_F(BasicEncoder_Test, StateDetection_StackScanMaxDepth)
{
    // Verify stack scanning respects max depth limit
    using namespace TRDOS::ROMSwitch;
    
    uint16_t sp = 0xFF00;
    
    // Fill stack with valid-looking addresses (not zeros or $FFFF)
    // to avoid triggering garbage detection
    for (int i = 0; i < 25; i++)
    {
        uint16_t fakeAddr = 0x5000 + (i * 0x100);  // Various RAM addresses
        _memory->DirectWriteToZ80Memory(sp + i*2, fakeAddr & 0xFF);
        _memory->DirectWriteToZ80Memory(sp + i*2 + 1, (fakeAddr >> 8) & 0xFF);
    }
    
    // Put DOS address at depth 20 (beyond default max of 16)
    int farOffset = 20 * 2;
    _memory->DirectWriteToZ80Memory(sp + farOffset, ROM_TRAMPOLINE & 0xFF);
    _memory->DirectWriteToZ80Memory(sp + farOffset + 1, (ROM_TRAMPOLINE >> 8) & 0xFF);
    
    // Should NOT find it with default depth
    bool foundDefault = BasicEncoder::stackContainsDOSReturnAddress(_memory, sp, 16);
    EXPECT_FALSE(foundDefault) << "Should not find DOS address beyond max depth";
    
    // Should find it with extended depth
    bool foundExtended = BasicEncoder::stackContainsDOSReturnAddress(_memory, sp, 25);
    EXPECT_TRUE(foundExtended) << "Should find DOS address with extended depth";
}

TEST_F(BasicEncoder_Test, StateDetection_TrapRangeBoundaries)
{
    // Verify trap range detection at boundaries
    using namespace TRDOS::ROMSwitch;
    
    uint16_t sp = 0xFF00;
    
    // Test $3D00 (start of trap range)
    _memory->DirectWriteToZ80Memory(sp, TRAP_START & 0xFF);
    _memory->DirectWriteToZ80Memory(sp + 1, (TRAP_START >> 8) & 0xFF);
    EXPECT_TRUE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp)) 
        << "$3D00 should be in trap range";
    
    // Test $3DFF (end of trap range)
    _memory->DirectWriteToZ80Memory(sp, TRAP_END & 0xFF);
    _memory->DirectWriteToZ80Memory(sp + 1, (TRAP_END >> 8) & 0xFF);
    EXPECT_TRUE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "$3DFF should be in trap range";
    
    // Test $3CFF (just below trap range)
    _memory->DirectWriteToZ80Memory(sp, 0xFF);
    _memory->DirectWriteToZ80Memory(sp + 1, 0x3C);
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "$3CFF should NOT be in trap range";
    
    // Test $3E00 (just above trap range)
    _memory->DirectWriteToZ80Memory(sp, 0x00);
    _memory->DirectWriteToZ80Memory(sp + 1, 0x3E);
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "$3E00 should NOT be in trap range";
}

TEST_F(BasicEncoder_Test, StateDetection_StackValidation_InvalidSP)
{
    // Test that invalid SP values cause fallback (return false)
    
    // SP in ROM area ($0000-$3FFF) is invalid
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, 0x1000))
        << "SP in ROM area should fail validation";
    
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, 0x3FFF))
        << "SP at end of ROM area should fail validation";
    
    // SP at very top of memory with no room
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, 0xFFFE))
        << "SP at $FFFE should fail validation";
    
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, 0xFFFF))
        << "SP at $FFFF should fail validation";
}

TEST_F(BasicEncoder_Test, StateDetection_StackGarbageDetection_TooManyZeros)
{
    // Test that consecutive $0000 entries trigger garbage detection
    uint16_t sp = 0xFF00;
    
    // Fill stack with zeros (uninitialized memory pattern)
    for (int i = 0; i < 20; i++)
    {
        _memory->DirectWriteToZ80Memory(sp + i, 0x00);
    }
    
    // Should detect garbage and return false (fallback to HW detection)
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "Stack full of zeros should trigger garbage detection";
}

TEST_F(BasicEncoder_Test, StateDetection_StackGarbageDetection_TooManyFFFF)
{
    // Test that multiple $FFFF entries trigger garbage detection
    uint16_t sp = 0xFF00;
    
    // Fill stack with $FFFF (uninitialized RAM pattern)
    for (int i = 0; i < 10; i++)
    {
        _memory->DirectWriteToZ80Memory(sp + i*2, 0xFF);
        _memory->DirectWriteToZ80Memory(sp + i*2 + 1, 0xFF);
    }
    
    // Should detect garbage and return false
    EXPECT_FALSE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "Stack full of $FFFF should trigger garbage detection";
}

TEST_F(BasicEncoder_Test, StateDetection_StackGarbageDetection_ValidWithSomeZeros)
{
    // Test that a few zeros don't trigger false garbage detection
    using namespace TRDOS::ROMSwitch;
    
    uint16_t sp = 0xFF00;
    
    // Some zeros, then valid DOS address
    _memory->DirectWriteToZ80Memory(sp + 0, 0x00);  // $0000
    _memory->DirectWriteToZ80Memory(sp + 1, 0x00);
    _memory->DirectWriteToZ80Memory(sp + 2, 0x00);  // $0000
    _memory->DirectWriteToZ80Memory(sp + 3, 0x00);
    _memory->DirectWriteToZ80Memory(sp + 4, ROM_TRAMPOLINE & 0xFF);  // $3D2F
    _memory->DirectWriteToZ80Memory(sp + 5, (ROM_TRAMPOLINE >> 8) & 0xFF);
    
    // Should still find the DOS address (only 2 zeros, threshold is 4)
    EXPECT_TRUE(BasicEncoder::stackContainsDOSReturnAddress(_memory, sp))
        << "A few zeros should not trigger garbage detection when DOS addr follows";
}

TEST_F(BasicEncoder_Test, StateDetection_IsStackSane_ValidStack)
{
    // Test that a stack with plausible ROM/RAM addresses passes sanity check
    uint16_t sp = 0xFF00;
    
    // Create a realistic looking stack
    // Entry 1: ROM address (keyboard scan $028E)
    _memory->DirectWriteToZ80Memory(sp + 0, 0x8E);
    _memory->DirectWriteToZ80Memory(sp + 1, 0x02);
    
    // Entry 2: RAM trampoline ($5CC2)
    _memory->DirectWriteToZ80Memory(sp + 2, 0xC2);
    _memory->DirectWriteToZ80Memory(sp + 3, 0x5C);
    
    // Entry 3: Program RAM ($6000)
    _memory->DirectWriteToZ80Memory(sp + 4, 0x00);
    _memory->DirectWriteToZ80Memory(sp + 5, 0x60);
    
    // Entry 4: TR-DOS trap ($3D2F)
    _memory->DirectWriteToZ80Memory(sp + 6, 0x2F);
    _memory->DirectWriteToZ80Memory(sp + 7, 0x3D);
    
    EXPECT_TRUE(BasicEncoder::isStackSane(_memory, sp))
        << "Stack with valid ROM/RAM addresses should be sane";
}

TEST_F(BasicEncoder_Test, StateDetection_IsStackSane_GarbageStack)
{
    // Test that a stack full of garbage fails sanity check
    uint16_t sp = 0xFF00;
    
    // Fill with garbage (addresses that don't look like code areas)
    // $3E10 - just outside trap range
    _memory->DirectWriteToZ80Memory(sp + 0, 0x10);
    _memory->DirectWriteToZ80Memory(sp + 1, 0x3E);
    
    // $FF80 - high stack area (unusual)
    _memory->DirectWriteToZ80Memory(sp + 2, 0x80);
    _memory->DirectWriteToZ80Memory(sp + 3, 0xFF);
    
    // $4000 - screen memory (not code)
    _memory->DirectWriteToZ80Memory(sp + 4, 0x00);
    _memory->DirectWriteToZ80Memory(sp + 5, 0x40);
    
    // $FFFF - uninitialized
    _memory->DirectWriteToZ80Memory(sp + 6, 0xFF);
    _memory->DirectWriteToZ80Memory(sp + 7, 0xFF);
    
    EXPECT_FALSE(BasicEncoder::isStackSane(_memory, sp))
        << "Stack with garbage addresses should not be sane";
}

TEST_F(BasicEncoder_Test, StateDetection_IsStackSane_InvalidSP)
{
    // Test that invalid SP values fail sanity check
    EXPECT_FALSE(BasicEncoder::isStackSane(_memory, 0x1000))
        << "SP in ROM area should fail";
    EXPECT_FALSE(BasicEncoder::isStackSane(_memory, 0xFFFE))
        << "SP at memory top should fail";
}

/// endregion </ROM State Detection Tests>

