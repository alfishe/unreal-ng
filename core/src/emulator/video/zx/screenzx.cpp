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

/// Calculate address in Z80 address space for pixel with coordinates (x, y)
/// \param x - X-coordinate
/// \param y - Y-coordinate
/// \param baseAddress - Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return  -
uint16_t ScreenZX::CalculateXYScreenAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000)
{
    uint16_t result = baseAddress;

    uint16_t x0x1x2x3x4 = x & 0b00011111;
    uint8_t y0y1y2 = y & 0b00000111;
    uint8_t y3y4y5 = y & 0b00111000;
    uint8_t y6y7 = y & 0b11000000;

    uint16_t loByte = (y3y4y5 << 2) | x0x1x2x3x4;
    uint8_t hiByte = (y6y7 >> 3) | y0y1y2;

    result = baseAddress + (hiByte << 8) + loByte;

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