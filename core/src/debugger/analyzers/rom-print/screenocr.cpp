#include "screenocr.h"

#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/screen.h"

#include <algorithm>
#include <cctype>

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
            bool inverse = false;
            line.text += matchFontOrInverse(bitmap, inverse);
            line.inverse[col] = inverse;
            line.attrs[col] = screen[6144 + row * COLS + col];
        }
    }
    markHighlightedRows(lines);
    return true;
}

void ScreenOCR::markHighlightedRows(std::vector<ZxLine>& lines)
{
    // Attributes compared without FLASH
    auto key = [](uint8_t attr) { return static_cast<uint8_t>(attr & 0x7F); };
    auto mostCommon = [](const std::array<int, 128>& counts, int& count) {
        int best = 0;
        for (int a = 1; a < 128; a++)
            if (counts[a] > counts[best])
                best = a;
        count = counts[best];
        return static_cast<uint8_t>(best);
    };

    // The background: the attribute most cells have
    std::array<int, 128> counts{};
    for (const ZxLine& line : lines)
        for (uint8_t attr : line.attrs)
            counts[key(attr)]++;
    int count = 0;
    const uint8_t background = mostCommon(counts, count);

    // Each row's panel: the attribute most of its other cells have (at least 4 of them, a bar not a dot)
    static constexpr int kMinPanelCells = 4;
    std::vector<int> panel(lines.size(), -1);
    std::array<int, 128> rowsPerPanel{};
    for (size_t r = 0; r < lines.size(); r++)
    {
        std::array<int, 128> row{};
        for (uint8_t attr : lines[r].attrs)
            if (key(attr) != background)
                row[key(attr)]++;
        const uint8_t attr = mostCommon(row, count);
        if (count >= kMinPanelCells)
        {
            panel[r] = attr;
            rowsPerPanel[attr]++;
        }
    }

    // The panel most rows share (a menu box); with none shared by two rows the background is the panel (a menu
    // drawn on the whole screen)
    uint8_t common = mostCommon(rowsPerPanel, count);
    if (count < 2)
        common = background;

    for (size_t r = 0; r < lines.size(); r++)
    {
        ZxLine& line = lines[r];
        const bool hasText = std::any_of(line.text.begin(), line.text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
        line.highlighted = panel[r] >= 0 && panel[r] != common && (panel[r] & 0x07) == (common & 0x07) && hasText;
    }
}

char ScreenOCR::ocrCell(Memory* memory, int row, int col)
{
    initFontHashTable();  // Ensure initialized for direct ocrCell calls
    uint8_t bitmap[8];
    extractCellBitmap(memory, row, col, bitmap);
    bool inverse = false;
    return matchFontOrInverse(bitmap, inverse);
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

char ScreenOCR::matchFontOrInverse(const uint8_t* bitmap8bytes, bool& inverse)
{
    inverse = false;
    const char direct = matchFont(bitmap8bytes);
    if (direct != '?')
        return direct;

    // INVERSE 1 (or a menu that draws its bar with set pixels): the glyph is the paper. A solid cell is an inverse
    // space and stays unflagged
    uint8_t inverted[8];
    for (int i = 0; i < 8; i++)
        inverted[i] = static_cast<uint8_t>(~bitmap8bytes[i]);
    const char glyph = matchFont(inverted);
    inverse = glyph != '?' && glyph != ' ';
    return glyph;
}
