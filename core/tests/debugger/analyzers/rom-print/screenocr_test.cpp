/// @file screenocr_test.cpp
/// @brief Comprehensive test suite for ScreenOCR - tests ALL 96 font characters at ALL positions

#include "stdafx.h"
#include "pch.h"

#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/sprinter/sprintervideoram.h"
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

/// OCR-04: an inverse glyph (INVERSE 1) reads as its character
TEST_F(ScreenOCR_Test, RecognizeInverseChar_SingleLetter)
{
    WriteCharToScreen(0, 0, 'A', true);
    EXPECT_EQ(ScreenOCR::ocrCell(_memory, 0, 0), 'A');
}

/// OCR-04: every inverse glyph is recognised (the ROM font has no glyph that is another one inverted)
TEST_F(ScreenOCR_Test, RecognizeInverseAllChars)
{
    for (int code = 0x21; code <= 0x7F; code++)  // Skip space
    {
        ClearScreen();
        WriteCharToScreen(0, 0, static_cast<char>(code), true);
        EXPECT_EQ(ScreenOCR::ocrCell(_memory, 0, 0), static_cast<char>(code)) << "inverse code 0x" << std::hex << code;
    }

    // A solid cell is an inverse space
    for (int line = 0; line < 8; line++)
        _memory->DirectWriteToZ80Memory(GetScreenAddr(0, 1, line), 0xFF);
    EXPECT_EQ(ScreenOCR::ocrCell(_memory, 0, 1), ' ');
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

// ============================================================================
// TEXT VIDEO MODES (PLAN #42): read exactly from the character codes
// ============================================================================

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/video/screen.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"

#include <algorithm>

/// ATM Turbo 2+ text mode: the codes are in video memory, OCR reads them through the video mapper
TEST(ScreenOCRTextMode_Test, AtmTextModeIsReadFromTheCharacterCodes)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM710", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->emulatorState.pFF77 = FF77_TX | 0x20;
    context->emulatorState.atm.aFF77 = 0x0100;
    context->pScreen->InitRaster();
    ASSERT_EQ(context->pScreen->GetVideoMode(), M_ATMTX);

    // Row 0: even columns at video page 5 + 0x1C0 + n / 2, odd columns at + 0x2000
    uint8_t* page = context->pMemory->RAMPageAddress(5);
    for (int n = 0; n < 80; n++)
        page[(n % 2 ? 0x2000 : 0) + 0x1C0 + n / 2] = ' ';
    page[0x1C0] = 'H';
    page[0x2000 + 0x1C0] = 'I';

    const std::string text = ScreenOCR::ocrScreen(emulator->GetId());
    EXPECT_EQ(text.substr(0, 2), "HI");
    EXPECT_EQ(text.find('\n'), 80u) << "80 columns";
    EXPECT_TRUE(ScreenOCR::containsText(emulator->GetId(), "HI"));
    EXPECT_FALSE(ScreenOCR::containsText(emulator->GetId(), "HIX"));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

// The Sprinter (automation audit G8): a text picture's squares are read like a text layer (DeviceState::SprinterText),
// so an agent that does not know the machine reads BIOS / DSS screens with the OCR (Spectrum mode stays a ZX screen,
// SprinterBoot_Test.Dss162_SpectrumModeTrDosReadsATrd). A machine build: ~20-100 ms
TEST(ScreenOCRTextMode_Test, SprinterTextSquaresAreRead)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "no Sprinter (data/rom/sprinter/sp2k-3.04.rom?)";
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    SprinterVideoRam& vram = decoder->GetVideoRam();
    const uint8_t page = decoder->GetPldState().rgMod & 1;
    decoder->GetPldState().allMode = 0x01;  // Sprinter mode (not the Spectrum screen)
    for (uint8_t b = 0; b < 32; b++)
    {
        for (uint8_t a = 0; a < 40; a++)
        {
            const uint32_t square = SprinterVideoRam::ModeAddress(a, b, page);
            vram.Write(square, 0x30);  // text 40: the whole picture is text
            vram.Write(square + 1, ' ');
        }
    }
    vram.Write(SprinterVideoRam::ModeAddress(0, 0, page) + 1, 'B');

    const std::string text = ScreenOCR::ocrScreen(emulator->GetId());
    EXPECT_EQ(text.substr(0, 1), "B") << text.substr(0, 200);
    EXPECT_TRUE(ScreenOCR::containsText(emulator->GetId(), "B"));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

// ============================================================================
// THE DISPLAYED SCREEN, INVERSE CELLS AND THE MENU BAR (ZF6 mastering: MCP-02, MCP-03)
// ============================================================================

namespace
{
    /// Writes text into the screen bitmap of a RAM page (the ZX layout from its first byte)
    void WriteToPage(uint8_t* page, int row, int col, const std::string& text, bool inverse = false)
    {
        for (size_t i = 0; i < text.size(); i++)
        {
            const uint8_t* glyph = ZXSpectrum::FONT_BITMAP[text[i] - 0x20];
            for (int line = 0; line < 8; line++)
            {
                const int y = row * 8 + line;
                const int offset = ((y & 0xC0) << 5) + ((y & 7) << 8) + ((y & 0x38) << 2) + col + static_cast<int>(i);
                page[offset] = inverse ? static_cast<uint8_t>(~glyph[line]) : glyph[line];
            }
        }
    }

    void ClearPage(uint8_t* page, uint8_t attr)
    {
        std::fill(page, page + 6144, 0x00);
        std::fill(page + 6144, page + 6912, attr);
    }

    void FillAttrs(uint8_t* page, int row, int from, int to, uint8_t attr)
    {
        for (int col = from; col < to; col++)
            page[6144 + row * 32 + col] = attr;
    }
}

/// OCR-03: with #7FFD bit 3 the picture shows page 7: the OCR reads it, whatever the Z80 has at #4000 (page 5)
TEST(ScreenOCRMachine_Test, ShadowScreenIsReadFromPage7)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    uint8_t* page5 = context->pMemory->RAMPageAddress(5);
    uint8_t* page7 = context->pMemory->RAMPageAddress(7);
    ClearPage(page5, 0x38);
    ClearPage(page7, 0x38);
    WriteToPage(page5, 0, 0, "BANK FIVE");
    WriteToPage(page7, 0, 0, "BANK SEVEN");

    std::vector<ScreenOCR::ZxLine> lines;
    int page = -1;
    ASSERT_TRUE(ScreenOCR::readZxScreen(emulator, lines, page));
    EXPECT_EQ(page, 5);
    EXPECT_EQ(lines[0].text.substr(0, 9), "BANK FIVE");

    context->emulatorState.p7FFD = 0x08;  // the shadow screen; page 5 stays at #4000
    ASSERT_TRUE(ScreenOCR::readZxScreen(emulator, lines, page));
    EXPECT_EQ(page, 7);
    EXPECT_EQ(lines[0].text.substr(0, 10), "BANK SEVEN");
    EXPECT_EQ(ScreenOCR::ocrScreen(emulator).substr(0, 10), "BANK SEVEN");
    EXPECT_EQ(context->pMemory->DirectReadFromZ80Memory(0x5800), 0x38) << "the Z80 still sees page 5 at #4000";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// OCR-02 / OCR-04: inverse cells are flagged; the menu bar is the panel row that differs in PAPER / BRIGHT only,
/// a title strip with another INK is not, nor a bar without text
TEST(ScreenOCRMachine_Test, InverseCellsAndHighlightedRow)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    uint8_t* page5 = emulator->GetContext()->pMemory->RAMPageAddress(5);
    ClearPage(page5, 0x38);
    // The 128 menu as the ROM draws it: title 0x47, panel 0x78, bar 0x68 (cyan PAPER, same black INK)
    FillAttrs(page5, 7, 7, 21, 0x47);
    WriteToPage(page5, 7, 7, "128");
    for (int row = 8; row <= 13; row++)
        FillAttrs(page5, row, 7, 21, 0x78);
    WriteToPage(page5, 8, 8, "Tape Loader");
    WriteToPage(page5, 9, 8, "128 BASIC");
    WriteToPage(page5, 10, 8, "Calculator");
    FillAttrs(page5, 10, 7, 21, 0x68);
    WriteToPage(page5, 20, 2, "INVERSE", true);
    FillAttrs(page5, 22, 0, 32, 0x68);  // a coloured strip without text

    std::vector<ScreenOCR::ZxLine> lines;
    int page = -1;
    ASSERT_TRUE(ScreenOCR::readZxScreen(emulator, lines, page));
    for (int row = 0; row < 24; row++)
        EXPECT_EQ(lines[row].highlighted, row == 10) << "row " << row << ": " << lines[row].text;
    EXPECT_EQ(lines[20].text.substr(2, 7), "INVERSE");
    for (int col = 0; col < 32; col++)
        EXPECT_EQ(lines[20].inverse[col], col >= 2 && col < 9) << col;
    EXPECT_FALSE(lines[8].inverse[8]);
    EXPECT_EQ(lines[10].attrs[7], 0x68);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// OCR-02 end to end: the Pentagon's 128 menu from its ROM - the bar is on the first item, Down moves it one row.
/// Boot-bound: turbo, ~150 frames
TEST(ScreenOCRMachine_Test, PentagonMenuBarFollowsDown)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    emulator->GetContext()->config.reset_rom = RM_128;
    emulator->Reset();
    emulator->EnableTurboMode();

    std::vector<ScreenOCR::ZxLine> lines;
    int page = -1;
    auto highlightedRow = [&]() {
        if (!ScreenOCR::readZxScreen(emulator, lines, page))
            return -1;
        int found = -1;
        for (int row = 0; row < static_cast<int>(lines.size()); row++)
            if (lines[row].highlighted)
                found = found < 0 ? row : -2;  // -2: more than one
        return found;
    };
    int bar = -1;
    for (int frame = 0; frame < 400 && bar < 0; frame += 10)
    {
        emulator->RunNFrames(10, true);
        bar = highlightedRow();
    }
    ASSERT_GE(bar, 0) << ScreenOCR::ocrScreen(emulator);
    EXPECT_NE(lines[bar].text.find("Tape Loader"), std::string::npos) << lines[bar].text;

    emulator->GetContext()->pDebugManager->GetKeyboardManager()->TapKey("down");
    int moved = bar;
    for (int frame = 0; frame < 100 && moved == bar; frame += 5)
    {
        emulator->RunNFrames(5, true);
        moved = highlightedRow();
    }
    EXPECT_EQ(moved, bar + 1) << ScreenOCR::ocrScreen(emulator);
    EXPECT_NE(lines[bar + 1].text.find("128 BASIC"), std::string::npos) << lines[bar + 1].text;

    EmulatorTestHelper::CleanupEmulator(emulator);
}
