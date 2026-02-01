# Screen OCR Implementation

## Status: ✅ Implemented

## Overview
Static method to extract text from ZX Spectrum screen using ROM font pattern matching.

## API

```cpp
#include "debugger/analyzers/rom-print/screenocr.h"

// Extract all text from screen (24 lines × 32 chars)
std::string text = ScreenOCR::ocrScreen(emulatorId);
```

## Files

- `core/src/debugger/analyzers/rom-print/screenocr.h` - Header
- `core/src/debugger/analyzers/rom-print/screenocr.cpp` - Implementation

## How It Works

1. **Get memory** via `EmulatorManager::GetEmulator(emulatorId)`
2. **For each 8×8 cell** (32 cols × 24 rows):
   - Extract 8 bytes using ZX Spectrum interleaved screen address formula
   - Match against `ZXSpectrum::FONT_BITMAP[96][8]` (ROM font)
3. **Return text** as 24-line string with newlines

### Screen Address Formula
```cpp
// ZX Spectrum screen is interleaved, not sequential
uint16_t addr = 0x4000 + 
    ((y & 0xC0) << 5) +   // Third (0, 0x800, 0x1000)
    ((y & 7) << 8) +      // Pixel line within char
    ((y & 0x38) << 2) +   // Char row within third
    charCol;
```

## Usage Example
```cpp
// Get screen text from emulator "0"
std::string screenText = ScreenOCR::ocrScreen("0");

// Check for specific message
if (screenText.find("1982 Sinclair") != std::string::npos) {
    // Copyright message visible
}
```
