#include "debugger/analyzers/basic-lang/basicextractor.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

class BasicExtractorTest : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
};

TEST_F(BasicExtractorTest, ExtractBasic_SimplePrint)
{
    // 10 PRINT "HELLO"
    // Line 10 -> 0x00, 0x0A
    // Length: 2 (tokens) + 7 (string) + 1 (newline) = 10 bytes -> INCORRECT.
    // Real count: PRINT(1) + "(1) + H(1) + E(1) + L(1) + L(1) + O(1) + "(1) + NL(1) = 9 bytes.
    // PRINT -> 0xF5
    // "HELLO" -> 0x22, 'H', 'E', 'L', 'L', 'O', 0x22
    // Newline -> 0x0D
    std::vector<uint8_t> data = {
        0x00, 0x0A,  // Line 10
        0x09, 0x00,  // Length 9
        0xF5,        // PRINT
        0x22, 'H',  'E', 'L', 'L', 'O', 0x22,
        0x0D  // Newline
    };

    BasicExtractor extractor;
    std::string result = extractor.extractBasic(data.data(), data.size());

    // Expected: "10 PRINT "HELLO"\n"
    // Note: Line number format adds space ("10 "). Token used to add leading space (" PRINT"),
    // but now basicextractor.cpp avoids double-spacing.
    EXPECT_EQ(result, "10 PRINT \"HELLO\"\n");
}

TEST_F(BasicExtractorTest, ExtractBasic_HiddenNumber)
{
    // 20 LET A=10
    // Line 20 -> 0x00, 0x14
    // Content: LET A=10[0x0E][HIDDEN]
    // LET -> 0xF1
    //  A= -> 0x20, 0x41, 0x3D
    // 10 -> 0x31, 0x30
    // Hidden Marker -> 0x0E
    // Hidden 5 bytes -> 0x00, 0x00, 0x00, 0x00, 0x00 (Dummy)
    // Newline -> 0x0D
    // Length: 1 (LET) + 3 ( A=) + 2 (10) + 1 (0x0E) + 5 (Hidden) + 1 (CR) = 13 bytes

    std::vector<uint8_t> data = {
        0x00, 0x14,                    // Line 20
        0x0D, 0x00,                    // Length 13 (0x0D)
        0xF1,                          // LET
        0x20, 'A',  '=',               // " A="
        '1',  '0',                     // "10"
        0x0E,                          // Marker
        0x00, 0x00, 0x00, 0x00, 0x00,  // Hidden bytes (should be skipped)
        0x0D                           // Newline
    };

    BasicExtractor extractor;
    std::string result = extractor.extractBasic(data.data(), data.size());

    // Expected: "20 LET  A=10\n"
    // "20 " + "LET " + " A=" + "10" + (skipped 0x0E+5) + "\n"
    EXPECT_EQ(result, "20 LET  A=10\n");
}

TEST_F(BasicExtractorTest, ExtractBasic_EyeAcheFile)
{
    std::string filePath = TestPathHelper::GetTestDataPath("analyzers/basic/EYEACHE2.B");

    ASSERT_TRUE(FileHelper::FileExists(filePath)) << "Test file not found: " << filePath;

    size_t fileSize = FileHelper::GetFileSize(filePath);
    std::vector<uint8_t> buffer(fileSize);
    FileHelper::ReadFileToBuffer(filePath, buffer.data(), fileSize);

    BasicExtractor extractor;
    std::string result = extractor.extractBasic(buffer.data(), buffer.size());

    // std::cout << "Extracted Result: '" << result << "'" << std::endl;

    // Based on inspection: "1 PRINT USR 0: REM !" + CHR$ + "\1"
    // The line length is 0xFFFF (Big Endian 00 01 FF FF)
    // We expect the extractor to survive and print what it can.
    // 1  PRINT USR 0: REM !"
    // Note: line number 1.
    // Content should contain "PRINT" and "USR"
    // Note: PRINT has trailing space, USR has no leading space -> " PRINT USR"
    EXPECT_NE(result.find("1 PRINT USR "), std::string::npos);
    EXPECT_NE(result.find(": REM !"), std::string::npos);
}

TEST_F(BasicExtractorTest, ExtractBasic_AcrossFile)
{
    std::string filePath = TestPathHelper::GetTestDataPath("analyzers/basic/ACROSS.B");

    ASSERT_TRUE(FileHelper::FileExists(filePath)) << "Test file not found: " << filePath;

    size_t fileSize = FileHelper::GetFileSize(filePath);
    std::vector<uint8_t> buffer(fileSize);
    FileHelper::ReadFileToBuffer(filePath, buffer.data(), fileSize);

    BasicExtractor extractor;
    std::string result = extractor.extractBasic(buffer.data(), buffer.size());

    std::cout << "Extracted ACROSS.B Result:\n" << result << std::endl;

    // Line 10 BORDER VAL "7": INK VAL "7": PAPER VAL "7": CLS : CLEAR VAL "25087": RANDOMIZE USR VAL "15619": REM :
    // LOAD "ACROSSLK" CODE VAL "25088" Note: Line 10.

    // Check for key components
    EXPECT_NE(result.find("10 BORDER VAL \"7\": INK VAL \"7\""), std::string::npos);
    EXPECT_NE(result.find("RANDOMIZE USR VAL \"15619\""), std::string::npos);
    // Note: Our extractor now adds a space after a closing quote if the next byte is a token.
    // So LOAD "ACROSSLK"CODE becomes LOAD "ACROSSLK" CODE
    EXPECT_NE(result.find("LOAD \"ACROSSLK\" CODE"), std::string::npos);
}

TEST_F(BasicExtractorTest, ExtractBasic_TapProgramBlock_DizzyX)
{
    // r8 regression for the Tape Manager block popup: the FIRST data block of
    // a stock BASIC game TAP is the program body (flag $FF + body + checksum).
    // DIZZY_X_ALEX_S carries a real-world protected listing: line 0 is a huge
    // REM stuffed with machine code, line 1 holds the cracker credit string.
    std::string filePath = TestPathHelper::GetTestDataPath("loaders/tap/DIZZY_X_ALEX_S__MAX_IWAMOTO.tap");

    ASSERT_TRUE(FileHelper::FileExists(filePath)) << "Test file not found: " << filePath;

    size_t fileSize = FileHelper::GetFileSize(filePath);
    std::vector<uint8_t> buffer(fileSize);
    FileHelper::ReadFileToBuffer(filePath, buffer.data(), fileSize);
    ASSERT_GE(fileSize, 23u);

    // Walk the TAP framing to block 1: [u16 len][bytes] twice
    auto blockLengthAt = [](const std::vector<uint8_t>& bytes, size_t offset) -> size_t
    {
        return offset + 2 <= bytes.size() ? static_cast<size_t>(bytes[offset] | (bytes[offset + 1] << 8)) : 0;
    };
    const size_t block0Length = blockLengthAt(buffer, 0);
    const size_t block1Offset = 2 + block0Length;
    const size_t block1Length = blockLengthAt(buffer, block1Offset);
    ASSERT_EQ(block0Length, 19u) << "block 0 should be the 19-byte Program header";
    ASSERT_GT(block1Length, 3u) << "block 1 should be the framed program body";

    // Strip the $FF flag and the trailing checksum — exactly what the popup feeds in
    const uint8_t* body = buffer.data() + block1Offset + 2 + 1;
    const size_t bodySize = block1Length - 2;

    BasicExtractor extractor;
    std::string result = extractor.extractBasic(const_cast<uint8_t*>(body), bodySize);

    // Line 0 opens the listing (REM token 0xEA renders as " REM ")
    EXPECT_EQ(result.compare(0, 5, "0 REM"), 0) << "listing should open with the line-0 REM";
    // Line 1 survives the embedded-binary line 0 intact
    EXPECT_NE(result.find("VIKTOR VIKTOROVICH TEL. 65-00-83"), std::string::npos);
}

TEST_F(BasicExtractorTest, ExtractBasicLines_StructureAndVarsArea)
{
    // r9 structured walk: two program lines plus a 6-byte variables tail.
    // The vars area starts with a var-name byte (single-letter vars are
    // 0x80+code, e.g. 0xC1 = A), which the walk sees as a "line number" far
    // above the 9999 editor limit — that marks the end of the program proper.
    std::vector<uint8_t> data = {
        0x00, 0x0A, 0x09, 0x00,                     // line 10, length 9
        0xF5, 0x22, 'H', 'E', 'L', 'L', 'O', 0x22, 0x0D,
        0x00, 0x14, 0x0D, 0x00,                     // line 20, length 13
        0xF1, 0x20, 'A', '=', '1', '0', 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
        0xC1, 'B', 0x00, 0x00, 0x00, 0x00           // variables area (var A...)
    };

    BasicExtractor extractor;
    const BasicListing listing = extractor.extractBasicLines(data.data(), data.size());

    ASSERT_EQ(listing.lines.size(), 3u);

    // Program lines keep their header offsets and the token spacing rules
    EXPECT_EQ(listing.lines[0].lineNumber, 10u);
    EXPECT_EQ(listing.lines[0].startOffset, 0u);
    EXPECT_EQ(listing.lines[0].endOffset, 13u);
    EXPECT_FALSE(listing.lines[0].leadingSpace);  // PRINT token absorbs the separator
    EXPECT_FALSE(listing.lines[0].variablesArea);
    EXPECT_EQ(listing.lines[0].text, " PRINT \"HELLO\"");

    EXPECT_EQ(listing.lines[1].lineNumber, 20u);
    EXPECT_EQ(listing.lines[1].startOffset, 13u);
    EXPECT_EQ(listing.lines[1].endOffset, 30u);
    EXPECT_EQ(listing.lines[1].text, " LET  A=10");
    EXPECT_FALSE(listing.lines[1].variablesArea);

    // The vars pseudo-line is flagged and delimits the program
    EXPECT_EQ(listing.lines[2].lineNumber, 0xC142u);
    EXPECT_EQ(listing.lines[2].startOffset, 30u);
    EXPECT_TRUE(listing.lines[2].variablesArea);
    EXPECT_EQ(listing.programEndOffset, 30u);
    EXPECT_EQ(listing.variablesBytes, 6u);

    // Legacy output is exactly the join of the structured lines
    EXPECT_EQ(extractor.extractBasic(data.data(), data.size()),
              "10 PRINT \"HELLO\"\n20 LET  A=10\n49474 \n");
}

TEST_F(BasicExtractorTest, ExtractBasicLines_TapProgramBlock_DizzyX)
{
    // r9 real-file companion of the r8 legacy regression: the protected
    // listing (line-0 REM + line-1 credit) must parse structurally, the
    // variables tail must be delimited, and the legacy text output must
    // stay exactly the join of the structured lines
    std::string filePath = TestPathHelper::GetTestDataPath("loaders/tap/DIZZY_X_ALEX_S__MAX_IWAMOTO.tap");

    ASSERT_TRUE(FileHelper::FileExists(filePath)) << "Test file not found: " << filePath;

    size_t fileSize = FileHelper::GetFileSize(filePath);
    std::vector<uint8_t> buffer(fileSize);
    FileHelper::ReadFileToBuffer(filePath, buffer.data(), fileSize);
    ASSERT_GE(fileSize, 23u);

    auto blockLengthAt = [](const std::vector<uint8_t>& bytes, size_t offset) -> size_t
    {
        return offset + 2 <= bytes.size() ? static_cast<size_t>(bytes[offset] | (bytes[offset + 1] << 8)) : 0;
    };
    const size_t block0Length = blockLengthAt(buffer, 0);
    const size_t block1Offset = 2 + block0Length;
    const size_t block1Length = blockLengthAt(buffer, block1Offset);
    ASSERT_EQ(block0Length, 19u) << "block 0 should be the 19-byte Program header";
    ASSERT_GT(block1Length, 3u) << "block 1 should be the framed program body";

    const uint8_t* body = buffer.data() + block1Offset + 2 + 1;
    const size_t bodySize = block1Length - 2;

    BasicExtractor extractor;
    const BasicListing listing = extractor.extractBasicLines(const_cast<uint8_t*>(body), bodySize);

    ASSERT_GE(listing.lines.size(), 3u) << "line 0, line 1 and the vars tail must parse";

    // Line 0: the machine-code-carrying REM opens the listing
    EXPECT_EQ(listing.lines[0].lineNumber, 0u);
    EXPECT_FALSE(listing.lines[0].variablesArea);
    EXPECT_EQ(listing.lines[0].text.compare(0, 4, " REM"), 0);

    // Line 1: the credit line survives intact
    EXPECT_EQ(listing.lines[1].lineNumber, 1u);
    EXPECT_FALSE(listing.lines[1].variablesArea);
    EXPECT_NE(listing.lines[1].text.find("VIKTOR VIKTOROVICH TEL. 65-00-83"), std::string::npos);

    // The first variables-area pseudo-line directly follows the program
    ASSERT_TRUE(listing.lines[2].variablesArea) << "the vars tail starts at line index 2";
    EXPECT_GT(listing.lines[2].lineNumber, BasicExtractor::MaxLineNumber);
    EXPECT_EQ(listing.programEndOffset, listing.lines[2].startOffset);
    EXPECT_GE(listing.variablesBytes, 4u);
    EXPECT_EQ(listing.programEndOffset + listing.variablesBytes, bodySize);

    // Invariant on the real file: legacy output == join of the structured walk
    std::string joined;
    for (const BasicLine& line : listing.lines)
    {
        joined += std::to_string(line.lineNumber);
        if (line.leadingSpace)
        {
            joined += " ";
        }
        joined += line.text;
        joined += "\n";
    }
    EXPECT_EQ(joined, extractor.extractBasic(const_cast<uint8_t*>(body), bodySize));
}

TEST_F(BasicExtractorTest, ExtractBasic_FromMemory)
{
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Memory* memory = new Memory(context);

    // Set up default memory config (48k)
    memory->DefaultBanksFor48k();

    // BASIC Program: 10 PRINT "HI"
    // 00 0A (Line 10)
    // 06 00 (Len 6)
    // F6 (PRINT)
    // 22 (")
    // 48 49 (HI)
    // 22 (")
    // 0D (Enter)
    std::vector<uint8_t> program = { 0x00, 0x0A, 0x06, 0x00, 0xF5, 0x22, 0x48, 0x49, 0x22, 0x0D };

    uint16_t progStart = 0x5CCB;  // Standard start
    uint16_t varsStart = progStart + program.size();

    // Write PROG system variable
    memory->DirectWriteToZ80Memory(SystemVariables48k::PROG, progStart & 0xFF);
    memory->DirectWriteToZ80Memory(SystemVariables48k::PROG + 1, (progStart >> 8) & 0xFF);

    // Write VARS system variable
    // VARS points to the start of variables, which is immediately after program
    memory->DirectWriteToZ80Memory(SystemVariables48k::VARS, varsStart & 0xFF);
    memory->DirectWriteToZ80Memory(SystemVariables48k::VARS + 1, (varsStart >> 8) & 0xFF);

    // Write Program to memory
    for (size_t i = 0; i < program.size(); ++i)
    {
        memory->DirectWriteToZ80Memory(progStart + i, program[i]);
    }

    // Verify memory writes
    ASSERT_EQ(memory->DirectReadFromZ80Memory(SystemVariables48k::PROG), progStart & 0xFF);
    ASSERT_EQ(memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1), (progStart >> 8) & 0xFF);

    BasicExtractor extractor;
    std::string result = extractor.extractFromMemory(memory);

    std::cout << "FromMemory Result: '" << result << "'" << std::endl;

    EXPECT_NE(result.find("10 PRINT \"HI\""), std::string::npos);

    delete memory;
    delete context;
}
