#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include "zxspectrumfont.h"

/// ScreenOCR: Extract text from ZX Spectrum screen using ROM font matching
///
/// Uses the ROM font bitmap data to recognize characters displayed on screen.
/// ZX Spectrum screen: 32 columns × 24 rows of 8×8 pixel characters.
///
class ScreenOCR
{
public:
    /// OCR entire screen and return all visible text
    /// @param emulatorId EmulatorManager emulator ID
    /// @return All text on screen (24 lines, newline-separated)
    static std::string ocrScreen(const std::string& emulatorId);

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

private:
    /// Get screen byte address for given character cell and pixel line
    /// ZX Spectrum screen layout is interleaved, not sequential
    static uint16_t getScreenAddr(int charRow, int charCol, int pixelLine);

    /// Extract 8 bytes (8x8 bitmap) for character at given position
    static void extractCellBitmap(Memory* memory, int row, int col, uint8_t* out8bytes);

    /// Match bitmap against ROM font using hash lookup (O(1) vs O(96))
    static char matchFont(const uint8_t* bitmap8bytes);

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
