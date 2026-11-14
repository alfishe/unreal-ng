#include "screenocr.h"

#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/screen.h"

#include <algorithm>

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
    return ocrScreen(emulator.get());
}

std::string ScreenOCR::ocrScreen(Emulator* emulator)
{
    if (!emulator)
        return "";

    std::string text;
    if (textLayerScreen(emulator, text))
        return text;

    std::vector<ZxLine> lines;
    int page = -1;
    if (!readZxScreen(emulator, lines, page))
        return "";

    std::string result;
    result.reserve(ROWS * (COLS + 1));  // +1 for newlines
    for (const ZxLine& line : lines)
        result += line.text + '\n';
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

    // Row by row: a match never spans two rows (the text of a row ends with its newline)
    return ocrScreen(emulator.get()).find(searchText) != std::string::npos;
}

bool ScreenOCR::copyZxScreen(Emulator* emulator, uint8_t* out6912, int& displayedPage)
{
    displayedPage = -1;
    EmulatorContext* context = emulator->GetContext();
    Memory* memory = emulator->GetMemory();
    if (!memory)
        return false;

    // The bytes the picture is read from (Screen::DescribeDisplayedMemory): the ZX layout is one span of 6,912 bytes
    // in page 5, or 7 when #7FFD bit 3 shows the shadow screen - not what the Z80 has paged in at #4000
    Screen* screen = context ? context->pScreen : nullptr;
    if (screen)
    {
        std::vector<ScreenMemorySpan> spans;
        screen->DescribeDisplayedMemory(spans);
        if (spans.size() == 1 && !spans[0].region && spans[0].bytes == 6912 && memory->RAMBase() &&
            spans[0].offset + 6912u <= MAX_RAM_SIZE)
        {
            std::copy_n(memory->RAMBase() + spans[0].offset, 6912, out6912);
            displayedPage = static_cast<int>(spans[0].offset / 0x4000u);
            return true;
        }
    }

    // No ZX layout described (no screen device, another family's mode): what the Z80 sees at #4000
    for (uint16_t i = 0; i < 6912; i++)
        out6912[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(SCREEN_BASE + i));
    return true;
}

bool ScreenOCR::readZxScreen(Emulator* emulator, std::vector<ZxLine>& lines, int& displayedPage)
{
    lines.clear();
    displayedPage = -1;
    if (!emulator)
        return false;
    std::string text;
    if (textLayerScreen(emulator, text))
        return false;

    uint8_t screen[6912];
    if (!copyZxScreen(emulator, screen, displayedPage))
        return false;

    initFontHashTable();
    lines.resize(ROWS);
    for (int row = 0; row < ROWS; row++)
    {
        ZxLine& line = lines[row];
        line.text.reserve(COLS);
        for (int col = 0; col < COLS; col++)
        {
            uint8_t bitmap[8];
            for (int pixelLine = 0; pixelLine < 8; pixelLine++)
                bitmap[pixelLine] = screen[getScreenAddr(row, col, pixelLine) - SCREEN_BASE];
            line.text += matchFont(bitmap);
            line.attrs[col] = screen[6144 + row * COLS + col];
        }
    }
    return true;
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
