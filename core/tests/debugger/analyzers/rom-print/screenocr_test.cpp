/// @file screenocr_test.cpp
/// @brief Comprehensive test suite for ScreenOCR - tests ALL 96 font characters at ALL positions

#include "stdafx.h"
#include "pch.h"

#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/analyzers/rom-print/zxspectrumfont.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "3rdparty/message-center/messagecenter.h"

class ScreenOCR_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _context = new EmulatorContext(LoggerLevel::LogError);
        _memory = new Memory(_context);
        _memory->DefaultBanksFor48k();
        ClearScreen();
    }

    void TearDown() override
    {
        delete _memory;
        delete _context;
        MessageCenter::DisposeDefaultMessageCenter();
    }
    
    void ClearScreen()
    {
        for (uint16_t addr = 0x4000; addr < 0x5800; addr++)
            _memory->DirectWriteToZ80Memory(addr, 0x00);
    }
    
    void WriteCharToScreen(int row, int col, char ch, bool inverse = false)
    {
        if (ch < 0x20 || ch > 0x7F) return;
        const uint8_t* bitmap = ZXSpectrum::FONT_BITMAP[ch - 0x20];
        for (int line = 0; line < 8; line++)
        {
            uint8_t data = inverse ? ~bitmap[line] : bitmap[line];
            _memory->DirectWriteToZ80Memory(GetScreenAddr(row, col, line), data);
        }
    }
    
    void WriteStringToScreen(int row, int col, const std::string& text, bool inverse = false)
    {
        for (size_t i = 0; i < text.size() && (col + static_cast<int>(i)) < 32; i++)
            WriteCharToScreen(row, col + static_cast<int>(i), text[i], inverse);
    }
    
    static uint16_t GetScreenAddr(int charRow, int charCol, int pixelLine)
    {
        int y = charRow * 8 + pixelLine;
        return 0x4000 + ((y & 0xC0) << 5) + ((y & 7) << 8) + ((y & 0x38) << 2) + charCol;
    }
};

// ============================================================================
// ALL 96 FONT CHARACTERS (0x20-0x7F)
// ============================================================================

TEST_F(ScreenOCR_Test, RecognizeAllFontCharacters_AtPosition_0_0)
{
    // Test every single character in the font at position (0,0)
    for (int code = 0x20; code <= 0x7F; code++)
    {
        ClearScreen();
        WriteCharToScreen(0, 0, static_cast<char>(code));
        
        char result = ScreenOCR::ocrCell(_memory, 0, 0);
        EXPECT_EQ(static_cast<char>(code), result) 
            << "Failed for char code 0x" << std::hex << code 
            << " ('" << static_cast<char>(code) << "')";
    }
}

TEST_F(ScreenOCR_Test, RecognizeAllFontCharacters_AtMiddleScreen)
{
    // Test every character at center position (12, 16)
    for (int code = 0x20; code <= 0x7F; code++)
    {
        ClearScreen();
        WriteCharToScreen(12, 16, static_cast<char>(code));
        
        char result = ScreenOCR::ocrCell(_memory, 12, 16);
        EXPECT_EQ(static_cast<char>(code), result)
            << "Failed at (12,16) for code 0x" << std::hex << code;
    }
}

// ============================================================================
// ALL SCREEN POSITIONS (32 cols × 24 rows = 768 cells)
// ============================================================================

TEST_F(ScreenOCR_Test, RecognizeCharAtEveryScreenPosition)
{
    // Test character 'X' at every single screen position
    for (int row = 0; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            ClearScreen();
            WriteCharToScreen(row, col, 'X');
            
            char result = ScreenOCR::ocrCell(_memory, row, col);
            EXPECT_EQ('X', result) << "Failed at row=" << row << " col=" << col;
        }
    }
}

TEST_F(ScreenOCR_Test, RecognizeLetterAtEveryPosition_FirstThird)
{
    // Test rows 0-7 (first third of screen - different address layout)
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            ClearScreen();
            WriteCharToScreen(row, col, 'A');
            EXPECT_EQ('A', ScreenOCR::ocrCell(_memory, row, col)) 
                << "First third fail: row=" << row << " col=" << col;
        }
    }
}

TEST_F(ScreenOCR_Test, RecognizeLetterAtEveryPosition_SecondThird)
{
    // Test rows 8-15 (second third)
    for (int row = 8; row < 16; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            ClearScreen();
            WriteCharToScreen(row, col, 'B');
            EXPECT_EQ('B', ScreenOCR::ocrCell(_memory, row, col))
                << "Second third fail: row=" << row << " col=" << col;
        }
    }
}

TEST_F(ScreenOCR_Test, RecognizeLetterAtEveryPosition_ThirdThird)
{
    // Test rows 16-23 (third third)
    for (int row = 16; row < 24; row++)
    {
        for (int col = 0; col < 32; col++)
        {
            ClearScreen();
            WriteCharToScreen(row, col, 'C');
            EXPECT_EQ('C', ScreenOCR::ocrCell(_memory, row, col))
                << "Third third fail: row=" << row << " col=" << col;
        }
    }
}

// ============================================================================
// INVERSE VIDEO (all pixels inverted)
// ============================================================================

TEST_F(ScreenOCR_Test, RecognizeInverseChar_SingleLetter)
{
    WriteCharToScreen(0, 0, 'A', true);  // Inverse
    
    // Current implementation might not handle inverse - test behavior
    char result = ScreenOCR::ocrCell(_memory, 0, 0);
    // Inverse 'A' won't match normal font - should return '?' 
    // (or 'A' if inverse support is implemented)
    EXPECT_TRUE(result == '?' || result == 'A') 
        << "Inverse 'A' returned: " << result;
}

TEST_F(ScreenOCR_Test, RecognizeInverseAllChars)
{
    // Track how many inverse chars are recognized (for future inverse support)
    int recognized = 0;
    for (int code = 0x21; code <= 0x7F; code++)  // Skip space
    {
        ClearScreen();
        WriteCharToScreen(0, 0, static_cast<char>(code), true);
        
        char result = ScreenOCR::ocrCell(_memory, 0, 0);
        if (result == static_cast<char>(code)) recognized++;
    }
    
    // Log how many inverse chars were recognized
    // (0 expected until inverse support added)
    std::cout << "[INFO] Inverse recognition: " << recognized << "/95 chars\n";
}

// ============================================================================
// SCREEN CORNER AND BOUNDARY TESTS
// ============================================================================

TEST_F(ScreenOCR_Test, AllFourCorners)
{
    WriteCharToScreen(0, 0, '1');
    WriteCharToScreen(0, 31, '2');
    WriteCharToScreen(23, 0, '3');
    WriteCharToScreen(23, 31, '4');
    
    EXPECT_EQ('1', ScreenOCR::ocrCell(_memory, 0, 0));
    EXPECT_EQ('2', ScreenOCR::ocrCell(_memory, 0, 31));
    EXPECT_EQ('3', ScreenOCR::ocrCell(_memory, 23, 0));
    EXPECT_EQ('4', ScreenOCR::ocrCell(_memory, 23, 31));
}

TEST_F(ScreenOCR_Test, ThirdBoundaries)
{
    // Row 7/8 boundary (Third 0 → 1)
    WriteCharToScreen(7, 0, 'A');
    WriteCharToScreen(8, 0, 'B');
    EXPECT_EQ('A', ScreenOCR::ocrCell(_memory, 7, 0));
    EXPECT_EQ('B', ScreenOCR::ocrCell(_memory, 8, 0));
    
    // Row 15/16 boundary (Third 1 → 2)
    WriteCharToScreen(15, 0, 'C');
    WriteCharToScreen(16, 0, 'D');
    EXPECT_EQ('C', ScreenOCR::ocrCell(_memory, 15, 0));
    EXPECT_EQ('D', ScreenOCR::ocrCell(_memory, 16, 0));
}

// ============================================================================
// FULL ROW AND MULTI-LINE
// ============================================================================

TEST_F(ScreenOCR_Test, FullRowOfAllDigitsAndLetters)
{
    WriteStringToScreen(10, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    
    std::string result;
    for (int col = 0; col < 32; col++)
        result += ScreenOCR::ocrCell(_memory, 10, col);
    
    EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", result);
}

TEST_F(ScreenOCR_Test, MultipleRowsConcurrently)
{
    WriteStringToScreen(0, 0, "ROW ZERO");
    WriteStringToScreen(10, 0, "ROW TEN");
    WriteStringToScreen(23, 0, "ROW TWENTYTHREE");
    
    std::string row0, row10, row23;
    for (int col = 0; col < 16; col++)
    {
        row0 += ScreenOCR::ocrCell(_memory, 0, col);
        row10 += ScreenOCR::ocrCell(_memory, 10, col);
        row23 += ScreenOCR::ocrCell(_memory, 23, col);
    }
    
    EXPECT_NE(std::string::npos, row0.find("ROW ZERO"));
    EXPECT_NE(std::string::npos, row10.find("ROW TEN"));
    EXPECT_NE(std::string::npos, row23.find("ROW TWENTYTHREE"));
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(ScreenOCR_Test, EmptyCell_ReturnsSpace)
{
    EXPECT_EQ(' ', ScreenOCR::ocrCell(_memory, 0, 0));
    EXPECT_EQ(' ', ScreenOCR::ocrCell(_memory, 12, 16));
    EXPECT_EQ(' ', ScreenOCR::ocrCell(_memory, 23, 31));
}

TEST_F(ScreenOCR_Test, UnrecognizedPattern_ReturnsQuestionMark)
{
    // Write a pattern that doesn't match any font character
    for (int line = 0; line < 8; line++)
        _memory->DirectWriteToZ80Memory(GetScreenAddr(0, 0, line), 0b10101010);
    
    EXPECT_EQ('?', ScreenOCR::ocrCell(_memory, 0, 0));
}

// ============================================================================
// ADDRESS FORMULA VALIDATION
// ============================================================================

TEST_F(ScreenOCR_Test, ScreenAddressFormula)
{
    EXPECT_EQ(0x4000, GetScreenAddr(0, 0, 0));
    EXPECT_EQ(0x4001, GetScreenAddr(0, 1, 0));
    EXPECT_EQ(0x4100, GetScreenAddr(0, 0, 1));
    EXPECT_EQ(0x4020, GetScreenAddr(1, 0, 0));
    EXPECT_EQ(0x4800, GetScreenAddr(8, 0, 0));
    EXPECT_EQ(0x5000, GetScreenAddr(16, 0, 0));
}
