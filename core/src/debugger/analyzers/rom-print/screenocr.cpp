#include "screenocr.h"

#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"

// Static member definitions
std::unordered_map<uint64_t, char> ScreenOCR::_fontHashTable;
bool ScreenOCR::_fontHashTableInitialized = false;

/// Text modes hold character codes, not bitmaps: read them exactly through the
/// video mapper (PLAN #42 design §6). One line per text row, printable ASCII,
/// other codes as spaces
bool ScreenOCR::textLayerScreen(Emulator* emulator, std::string& out)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pScreen)
        return false;
    uint16_t columns = 0, rows = 0;
    std::vector<videomap::TextCell> cells;
    if (!videomap::VideoMapService(context).Text(0, columns, rows, cells))
    {
        // The Sprinter's text squares (DeviceState::SprinterText: the same cells /video/text shows) while
        // most of the picture is text (BIOS, DSS); 80 columns, one line per square row. Spectrum mode's
        // graphics picture stays with the ZX OCR below
        const StateNode sprinter = DeviceState::SprinterText(context);
        const StateNode* pictureIsText = sprinter.find("picture_is_text");
        const StateNode* lines = sprinter.find("lines");
        if (!pictureIsText || !pictureIsText->b || !lines)
            return false;
        out.clear();
        for (const StateNode& line : lines->items)
        {
            const StateNode* text = line.find("text");
            out += (text ? text->s : std::string()) + '\n';
        }
        return true;
    }
    out.clear();
    out.reserve(static_cast<size_t>(rows) * (columns + 1));
    for (uint16_t r = 0; r < rows; ++r)
    {
        for (uint16_t c = 0; c < columns; ++c)
        {
            const uint8_t code = cells[static_cast<size_t>(r) * columns + c].code;
            out += (code >= 0x20 && code < 0x7F) ? static_cast<char>(code) : ' ';
        }
        out += '\n';
    }
    return true;
}

std::string ScreenOCR::ocrScreen(const std::string& emulatorId)
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(emulatorId);

    if (!emulator)
        return "";

    std::string text;
    if (textLayerScreen(emulator.get(), text))
        return text;

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

    std::string text;
    if (textLayerScreen(emulator.get(), text))
        return text.find(searchText) != std::string::npos;

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
