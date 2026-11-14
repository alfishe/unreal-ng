#pragma once

#include <array>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include "zxspectrumfont.h"

/// ScreenOCR: Extract text from ZX Spectrum screen using ROM font matching
///
/// Uses the ROM font bitmap data to recognize characters displayed on screen.
/// ZX Spectrum screen: 32 columns × 24 rows of 8×8 pixel characters, read from the screen the picture shows
/// (page 5, or 7 when #7FFD bit 3 selects the shadow screen).
/// Text video modes (ATM Turbo 2+ ATMTX, ZX-Evo ATMTL: 80x25) are read
/// exactly from their character codes through the video mapper instead.
///
class ScreenOCR
{
public:
    /// One text row of the ZX screen with what the pixels alone do not say
    struct ZxLine
    {
        std::string text;                       ///< 32 characters ('?' where no glyph matched)
        std::array<uint8_t, 32> attrs{};        ///< attribute byte of every cell
    };

    /// The ZX screen as text rows. displayedPage is the RAM page the picture shows (5 / 7), -1 when the bytes came
    /// from the Z80's #4000 window (no screen device: a bare memory in tests).
    /// False when the current mode has a text layer or no memory (ocrScreen covers those)
    static bool readZxScreen(Emulator* emulator, std::vector<ZxLine>& lines, int& displayedPage);

    /// OCR entire screen and return all visible text
    /// @param emulatorId EmulatorManager emulator ID
    /// @return All text on screen (24 lines, newline-separated)
    static std::string ocrScreen(const std::string& emulatorId);
    /// The same for an emulator at hand (a frame watch on the emulation thread: no manager lookup)
    static std::string ocrScreen(Emulator* emulator);

    /// Fast search for text anywhere on screen (stops when found)
    /// @param emulatorId EmulatorManager emulator ID
    /// @param searchText Text to find
    /// @return true if searchText found anywhere on screen
    static bool containsText(const std::string& emulatorId, const std::string& searchText);

    /// OCR single character at given cell position
    /// @param memory Pointer to memory (for internal use)
    /// @param row Character row (0-23)
    /// @param col Character column (0-31)
    /// @return Matched character or '?' if no match
    static char ocrCell(Memory* memory, int row, int col);

    /// The current mode's text layer as lines; false in bitmap modes
    static bool textLayerScreen(Emulator* emulator, std::string& out);

private:
    /// Get screen byte address for given character cell and pixel line
    /// ZX Spectrum screen layout is interleaved, not sequential
    static uint16_t getScreenAddr(int charRow, int charCol, int pixelLine);

    /// Extract 8 bytes (8x8 bitmap) for character at given position
    static void extractCellBitmap(Memory* memory, int row, int col, uint8_t* out8bytes);

    /// Match bitmap against ROM font using hash lookup (O(1) vs O(96))
    static char matchFont(const uint8_t* bitmap8bytes);

    /// The 6,912 bytes of the displayed ZX screen; displayedPage as in readZxScreen
    static bool copyZxScreen(Emulator* emulator, uint8_t* out6912, int& displayedPage);

    /// Hash function for 8-byte bitmap (uses first 4 bytes as uint32 + last 4)
    static uint64_t hashBitmap(const uint8_t* bitmap);

    /// Initialize hash table from ROM font on first use
    static void initFontHashTable();

    /// Screen constants
    static constexpr uint16_t SCREEN_BASE = 0x4000;
    static constexpr int ROWS = 24;
    static constexpr int COLS = 32;

    /// Font lookup: bitmap hash -> character
    static std::unordered_map<uint64_t, char> _fontHashTable;
    static bool _fontHashTableInitialized;
};
