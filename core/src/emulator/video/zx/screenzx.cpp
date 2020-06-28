#include "screenzx.h"

#include "emulator/cpu/z80.h"

ScreenZX::ScreenZX(EmulatorContext *context) : Screen(context)
{
}

ScreenZX::~ScreenZX()
{

}

/// region <Genuine ZX-Spectrum ULA specifics>

void ScreenZX::CreateTables()
{
    // ZX-Spectrum screen address helper

}

// ZX-Spectrum 48k ULA screen addressing:
// Where: X - 5'bits [0:31] - X-coordinate in 1x8 blocks, Y - 8'bits [0:191] - Y coordinate in pixel lines
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0   Y7   Y6   Y2   Y1   Y0   Y5   Y4   Y3   X4   X3   X2   X1   X0

// Alternative view:
// Where: C - 5'bits [0:31] - column coordinate in 1x8 blocks, L - 5'bits [0:31] - Line number, P - 3'bits [0:7] - pixel row within the line
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0   L4   L3   P2   P1   P0   L2   L1   L0   C4   C3   C2   C1   C0

/// Calculate address in Z80 address space for pixel with coordinates (x, y)
/// \param x - X-coordinate
/// \param y - Y-coordinate
/// \param baseAddress - Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return  - Address for correspondent coordinates adjusted to current screen base offset
uint16_t ScreenZX::CalculateXYScreenAddress(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    uint16_t result = baseAddress;
    uint8_t symbolX = x >> 3;

    // Check for invalid coordinates. (X: [0;31] => 32 * 8 pixel bits in line; Y: [0:191])
    if (symbolX < 32 && y < 192)
    {
        uint16_t x0x1x2x3x4 = symbolX & 0b00011111;
        uint8_t y0y1y2 = y & 0b00000111;
        uint8_t y3y4y5 = y & 0b00111000;
        uint8_t y6y7 = y & 0b11000000;

        uint8_t loByte = (y3y4y5 << 2) | x0x1x2x3x4;
        uint16_t hiByte = (y6y7 >> 3) | y0y1y2;

        result = baseAddress + (hiByte << 8) + loByte;
    }

    return result;
}


// ZX-Spectrum 48k ULA color addressing:
// Where: L - 5'bits [0:24] - Line coordinate, C - 5'bits [0:31] - Column coordinate
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0    1    1    0   L4   L3   L2   L1   L0   C4   C3   C2   C1   C0

/// Calculate address in Z80 address space for color atrributes corresponding to pixel with coordinates (x, y)
/// \param x - X-coordinate
/// \param y - Y-coordinate
/// \param baseAddress - Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return  - Address for correspondent coordinates adjusted to current screen base offset
uint16_t ScreenZX::CalculateXYColorAttrAddress(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    uint16_t result = baseAddress + 0x1800;
    uint8_t c = x >> 3;
    uint8_t l = y >> 3;

    // Check for invalid coordinates. (C: [0:31]; L: [0:23])
    if (c < 32 && l < 24)
    {
        uint16_t hiByte = 0b01011000 | (l >> 3);
        uint8_t loByte = (l << 5) | c;

        result += (hiByte << 8) + loByte;
    }

    return result;
}

// ZX-Spectrum 48k ULA color bits
// Where: P [0:7] - paper/background color; I [0:7] - ink/foreground color; B [0:1] - brightness; F [0:1] - flashing
// Color attr bits:    7    6    5    4    3    2    1    0
// Data bits:          F    B   P2   P1   P0   I2   I1   I0

// Alternative view:
// Color attr bits:    7        6         5         4         3       2       1       0
// Data bits:          F        B   Paper-G   Paper-R   Paper-B   Ink-G   Ink-R   Ink-B

/// Transforms ZX-Spectrum color to RGBA using palette information
/// \param color - ZX-Spectrum ULA color byte information
/// \param isPixelSet - determines whether pixel has paper/background or ink/foreground color
/// \return RGbA color for the pixel
uint32_t ScreenZX::TransformZXSpectrumColorsToRGBA(uint8_t color, bool isPixelSet)
{
    static uint32_t palette[2][8] =
    {
        //     Black,       Blue,        Red,    Magenta,      Green,       Cyan,     Yellow,      White
        { 0x00000000, 0x0022C700, 0xD6281600, 0xD433C700, 0x00C52500, 0x00C7C900, 0xCCC82A00, 0xCACACA00 },  // Brightness = 0
        { 0x00000000, 0x002BFB00, 0xFF331C00, 0xFF40FC00, 0x00F92F00, 0x00FBFE00, 0xFFFC3600, 0xFFFFFFFF }   // Brightness = 1
    };

    uint32_t result = 0;

    uint8_t paper = (color & 0b00111000) >> 3;
    uint8_t ink = color & -0b00000111;
    bool brightness = (color & 0b01000000) > 0;
    bool flash = (color & 0b10000000) > 0;

    // Set resulting pixel color based on ZX-Spectrum pixel information (not set => paper color, set => ink color)
    uint8_t paletteIndex = isPixelSet ? ink : paper;
    result = palette[brightness][paletteIndex];

    return result;
}


/// endregion </Genuine ZX-Spectrum ULA specifics>

/// region <Screen class methods override>

void ScreenZX::UpdateScreen()
{

}

///
/// Convert ZX-Spectrum screen (content area only) to RGBA framebuffer
///
void ScreenZX::RenderOnlyMainScreen()
{
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];

    // Validate required mode(s) set and framebuffer allocated
    if (rasterDescriptor.screenWidth == 0 || rasterDescriptor.screenHeight == 0 || _framebuffer.memoryBuffer == nullptr || _framebuffer.memoryBufferSize == 0)
        return;

    for (int y = 0; y < rasterDescriptor.screenHeight; y++)
    {
        for (int x = 0; x < rasterDescriptor.screenWidth / 8; x++)
        {
            /*
            byte pix = *(src + scrtab[y] + x);
            byte ink = colortab[*(src + atrtab[y] + x)];
            byte paper = ink >> 4;
            ink &= 0x0f;
            for(int b = 0; b < 8; ++b)
            {
                *dst++ = ((pix << b) & 0x80) ? ink : paper;
            }
             */
        }
    }
}

/// endregion </Screen class methods override>