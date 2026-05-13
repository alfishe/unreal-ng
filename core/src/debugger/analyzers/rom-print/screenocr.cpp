#include "screenocr.h"

std::string ScreenOCR::ocrScreen(const std::string& emulatorId)
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(emulatorId);
    
    if (!emulator)
        return "";
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
        return "";
    
    std::string result;
    result.reserve(ROWS * (COLS + 1));  // +1 for newlines
    
    for (int row = 0; row < ROWS; row++)
    {
        for (int col = 0; col < COLS; col++)
        {
            result += ocrCell(memory, row, col);
        }
        result += '\n';
    }
    
    return result;
}

char ScreenOCR::ocrCell(Memory* memory, int row, int col)
{
    uint8_t bitmap[8];
    extractCellBitmap(memory, row, col, bitmap);
    return matchFont(bitmap);
}

uint16_t ScreenOCR::getScreenAddr(int charRow, int charCol, int pixelLine)
{
    // ZX Spectrum screen address formula:
    // The screen is divided into 3 "thirds" (0-7, 8-15, 16-23 rows)
    // Within each third, lines are interleaved
    //
    // Address = 0x4000 + ((y & 0xC0) << 5) + ((y & 7) << 8) + ((y & 0x38) << 2) + x
    // where y = charRow * 8 + pixelLine, x = charCol
    
    int y = charRow * 8 + pixelLine;
    return SCREEN_BASE + 
           ((y & 0xC0) << 5) +   // Third select (0, 0x800, 0x1000)
           ((y & 7) << 8) +      // Pixel line within char
           ((y & 0x38) << 2) +   // Char row within third
           charCol;
}

void ScreenOCR::extractCellBitmap(Memory* memory, int row, int col, uint8_t* out8bytes)
{
    for (int pixelLine = 0; pixelLine < 8; pixelLine++)
    {
        uint16_t addr = getScreenAddr(row, col, pixelLine);
        out8bytes[pixelLine] = memory->DirectReadFromZ80Memory(addr);
    }
}

char ScreenOCR::matchFont(const uint8_t* bitmap8bytes)
{
    // Compare against all 96 characters in ROM font (0x20-0x7F)
    for (int charCode = 0; charCode < 96; charCode++)
    {
        bool match = true;
        for (int i = 0; i < 8; i++)
        {
            if (bitmap8bytes[i] != ZXSpectrum::FONT_BITMAP[charCode][i])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return static_cast<char>(0x20 + charCode);
        }
    }
    
    // No match - check for empty cell (all zeros = space)
    bool allZero = true;
    for (int i = 0; i < 8; i++)
    {
        if (bitmap8bytes[i] != 0)
        {
            allZero = false;
            break;
        }
    }
    
    return allZero ? ' ' : '?';
}
