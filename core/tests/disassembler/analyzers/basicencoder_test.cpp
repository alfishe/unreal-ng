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
