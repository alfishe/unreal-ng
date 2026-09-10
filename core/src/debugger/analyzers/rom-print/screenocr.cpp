#include "screenocr.h"

// Static member definitions
std::unordered_map<uint64_t, char> ScreenOCR::_fontHashTable;
bool ScreenOCR::_fontHashTableInitialized = false;

std::string ScreenOCR::ocrScreen(const std::string& emulatorId)
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(emulatorId);

    if (!emulator)
        return "";

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return "";

    // Ensure hash table is ready
    initFontHashTable();

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

bool ScreenOCR::containsText(const std::string& emulatorId, const std::string& searchText)
{
    if (searchText.empty())
        return true;

    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(emulatorId);
    if (!emulator)
        return false;

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return false;

    initFontHashTable();

    // Search each row for the text using a sliding window
    size_t searchLen = searchText.length();

    for (int row = 0; row < ROWS; row++)
    {
        // Build this row's text on-demand
        for (int startCol = 0; startCol <= COLS - static_cast<int>(searchLen); startCol++)
        {
            bool match = true;
            for (size_t i = 0; i < searchLen && match; i++)
            {
                char c = ocrCell(memory, row, startCol + static_cast<int>(i));
                if (c != searchText[i])
                    match = false;
            }
            if (match)
                return true;
        }
    }

    return false;
}

char ScreenOCR::ocrCell(Memory* memory, int row, int col)
{
    initFontHashTable();  // Ensure initialized for direct ocrCell calls
    uint8_t bitmap[8];
    extractCellBitmap(memory, row, col, bitmap);
    return matchFont(bitmap);
}

uint16_t ScreenOCR::getScreenAddr(int charRow, int charCol, int pixelLine)
{
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

uint64_t ScreenOCR::hashBitmap(const uint8_t* bitmap)
{
    // Pack 8 bytes into a 64-bit value for fast lookup
    return (static_cast<uint64_t>(bitmap[0]) << 56) |
           (static_cast<uint64_t>(bitmap[1]) << 48) |
           (static_cast<uint64_t>(bitmap[2]) << 40) |
           (static_cast<uint64_t>(bitmap[3]) << 32) |
           (static_cast<uint64_t>(bitmap[4]) << 24) |
           (static_cast<uint64_t>(bitmap[5]) << 16) |
           (static_cast<uint64_t>(bitmap[6]) << 8) |
           static_cast<uint64_t>(bitmap[7]);
}

void ScreenOCR::initFontHashTable()
{
    if (_fontHashTableInitialized)
        return;

    _fontHashTable.reserve(128);  // 96 characters + some slack

    // Add all ROM font characters to hash table
    for (int charCode = 0; charCode < 96; charCode++)
    {
        uint64_t hash = hashBitmap(ZXSpectrum::FONT_BITMAP[charCode]);
        _fontHashTable[hash] = static_cast<char>(0x20 + charCode);
    }

    // Add empty cell (all zeros) -> space
    _fontHashTable[0] = ' ';

    _fontHashTableInitialized = true;
}

char ScreenOCR::matchFont(const uint8_t* bitmap8bytes)
{
    // O(1) hash lookup instead of O(96) linear search
    uint64_t hash = hashBitmap(bitmap8bytes);
    auto it = _fontHashTable.find(hash);
    if (it != _fontHashTable.end())
        return it->second;

    return '?';  // No match
}
