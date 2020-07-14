#include "screenzx.h"

#include "emulator/cpu/z80.h"

/// region <Constructors / Destructors>

ScreenZX::ScreenZX(EmulatorContext *context) : Screen(context)
{
    SetVideoMode(M_ZX48);
    CreateTables();
}

ScreenZX::~ScreenZX()
{

}

/// endregion </Constructors / Destructors>

/// region <Genuine ZX-Spectrum ULA specifics>

void ScreenZX::CreateTables()
{
    // Pre-calculate line offsets for ZX-Spectrum ULA screen
    int idx = 0;
    for (int p = 0; p < 4; p++)
    {
        for (int y = 0; y < 8; y++)
        {
            for (int o = 0; o < 8; o++)
            {
                // Per-line offsets for pixel rows
                _screenLineOffsets[idx] = p * 0x800 + o * 0x100 + y * 0x20;

                // Attribute mapping
                _attrLineOffsets[idx] = 0x1800 + (p * 8 + y) * 0x20;

                idx++;
            }
        }
    }

    // Pre-calculate RGBA colors from ULA format
    for (int idx = 0; idx < 0x100; idx++)
    {
        _rgbaColors[idx] = TransformZXSpectrumColorsToRGBA(idx, true);          // Normal state colors
        _rgbaFlashColors[idx] = TransformZXSpectrumColorsToRGBA(idx, false);    // Flashing state colors
    }

    // Screen mode dependent
    CreateTimingTable();
}

// Pre-calculate render position based on t-states
// Each Z80 t-state ULA renders 2 pixels
// Color information update rate:
//   - border - each 2 pixels
//   - screen area - each 8 pixels
// Pre-calculated values contain per-line information

// Genuine ZX-Spectrum frame is: (64 + 192 + 56) * 224 = 69888 T-states
// Lines: 8 (vsync) + 8 (top border invisible) + 48 (top border) + 192 (screen) + 48 (bottom border) + 8 (???)
// Each line: 16 (left border) + 128 (screen) + 16 (right border) + 48 (blank / raytrace) + 16 (hsync) T-states
void ScreenZX::CreateTimingTable()
{
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];
    const RasterState& state = _rasterState;

    RenderTypeEnum type = RT_BLANK;

    /// region <Line renderer in screen area>

    for (uint16_t i = 0; i <= 255; i++)
    {
        if (i >= state.blankLineAreaStart && i <= state.blankLineAreaEnd)
        {
            type = RT_BLANK;
        }
        else if (i >= state.leftBorderAreaStart && i <= state.leftBorderAreaEnd)
        {
            type = RT_BORDER;
        }
        else if (i >= state.screenLineAreaStart && i <= state.screenLineAreaEnd)
        {
            type = RT_SCREEN;
        }
        else if (i >= state.rightBorderAreaStart && i <= state.rightBorderAreaEnd)
        {
            type = RT_BORDER;
        }
        else
        {
            type = RT_BLANK;
        }

        _screenLineRenderers[i] = type;
    }

    /// endregion </Line renderer in screen area>


    // 0. Invisible - VSync and VBlank

    // 1. Top segment - top border only
    for (int lines = 0; lines < rasterDescriptor.screenOffsetTop; lines++)
    {

    }

    // 2. Middle segment - border on sides and screen area
    int screenBottom = rasterDescriptor.screenOffsetTop + rasterDescriptor.screenHeight;
    for (int lines = rasterDescriptor.screenOffsetTop; lines < screenBottom; lines++)
    {

    }

    // 3. Bottom segment - bottom border only
    for (int lines = screenBottom; lines < rasterDescriptor.fullFrameHeight; lines++)
    {

    }

    // Contention memory access pattern
    // See: https://worldofspectrum.org/faq/reference/48kreference.htm#Contention
    // See: https://faqwiki.zxnet.co.uk/wiki/Contended_memory
    //
};

// ZX-Spectrum 48k ULA screen addressing:
// Where: X - 5'bits [0:31] - X-coordinate in 1x8 blocks, Y - 8'bits [0:191] - Y coordinate in pixel lines
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0   Y7   Y6   Y2   Y1   Y0   Y5   Y4   Y3   X4   X3   X2   X1   X0

// Alternative view:
// Where: C - 5'bits [0:31] - column coordinate in 1x8 blocks, L - 5'bits [0:31] - Line number, P - 3'bits [0:7] - pixel row within the line
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0   L4   L3   P2   P1   P0   L2   L1   L0   C4   C3   C2   C1   C0

/// Calculate address in Z80 address space for pixel with coordinates (x, y)
/// \param x X-coordinate
/// \param y Y-coordinate
/// \param baseAddress Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return Address for correspondent coordinates adjusted to current screen base offset
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

/// Lookup table optimized version to calculate address in Z80 address space for pixel with coordinates (x, y)
/// \param x X-coordinate
/// \param y Y-coordinate
/// \param baseAddress Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return Address for correspondent coordinates adjusted to current screen base offset
uint16_t ScreenZX::CalculateXYScreenAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    /*
    uint8_t symbolX = x >> 3;

    uint16_t result = baseAddress + _screenLineOffsets[y] + symbolX;

    return result;
     */

    return baseAddress + _screenLineOffsets[y] + (x >> 3);
}


// ZX-Spectrum 48k ULA color addressing:
// Where: L - 5'bits [0:24] - Line coordinate, C - 5'bits [0:31] - Column coordinate
// Address bits:   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
// Coord. bits:     0    1    0    1    1    0   L4   L3   L2   L1   L0   C4   C3   C2   C1   C0

/// Calculate address in Z80 address space for color atrributes corresponding to pixel with coordinates (x, y)
/// \param x X-coordinate
/// \param y Y-coordinate
/// \param baseAddress Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return Address for correspondent coordinates adjusted to current screen base offset
uint16_t ScreenZX::CalculateXYColorAttrAddress(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    uint16_t result = baseAddress + 0x1800;
    uint8_t c = x >> 3;
    uint8_t l = y >> 3;

    // Check for invalid coordinates. (C: [0:31]; L: [0:23])
    if (c < 32 && l < 24)
    {
        uint16_t hiByte = 0x18 | (l >> 3);
        uint8_t loByte = (l << 5) | c;

        result = baseAddress + (hiByte << 8) | loByte;
    }

    return result;
}

/// Lookup table optimized to calculate address in Z80 address space for color atrributes corresponding to pixel with coordinates (x, y)
/// \param x X-coordinate
/// \param y Y-coordinate
/// \param baseAddress Screen base address (0x4000 for ZX48 screen (Bank 5) and 0xC000 (Bank 7)). Default value: 0x4000
/// \return Address for correspondent coordinates adjusted to current screen base offset
uint16_t ScreenZX::CalculateXYColorAttrAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    /*
    uint8_t symbolX = x >> 3;

    // Lookup table values already have offset 0x1800 applied
    uint16_t result = baseAddress + _attrLineOffsets[y] + symbolX;

    return result;
    */

    return baseAddress + _attrLineOffsets[y] + (x >> 3);
}

// ZX-Spectrum 48k ULA color bits
// Where: P [0:7] - paper/background color; I [0:7] - ink/foreground color; B [0:1] - brightness; F [0:1] - flashing
// Color attr bits:    7    6    5    4    3    2    1    0
// Data bits:          F    B   P2   P1   P0   I2   I1   I0

// Alternative view:
// Color attr bits:    7        6         5         4         3       2       1       0
// Data bits:          F        B   Paper-G   Paper-R   Paper-B   Ink-G   Ink-R   Ink-B

/// Transforms ZX-Spectrum color to RGBA using palette information
/// \param attribute ZX-Spectrum ULA color byte information
/// \param isPixelSet determines whether pixel has paper/background or ink/foreground color
/// \return RGbA color for the pixel
uint32_t ScreenZX::TransformZXSpectrumColorsToRGBA(uint8_t attribute, bool isPixelSet)
{
    // ABGR32 (Little-endian) or RGBA32 (Bit-endian)
    // Alpha - #FF - opaque, #00 - transparent
    static uint32_t palette[2][8] =
    { // LSB ABGR encoded colors
        //     Black,       Blue,        Red,    Magenta,      Green,       Cyan,     Yellow,      White
        { 0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF0025C5, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA },  // Brightness = 0
        { 0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF }   // Brightness = 1
    };
    //    { // MSB RGBA encoded colors
    //        //     Black,       Blue,        Red,    Magenta,      Green,       Cyan,     Yellow,      White
    //        { 0x000000FF, 0x0022C7FF, 0xD62816FF, 0xD433C7FF, 0x00C525FF, 0x00C7C9FF, 0xCCC82AFF, 0xCACACAFF },  // Brightness = 0
    //        { 0x000000FF, 0x002BFBFF, 0xFF331CFF, 0xFF40FCFF, 0x00F92FFF, 0x00FBFEFF, 0xFFFC36FF, 0xFFFFFFFF }   // Brightness = 1
    //    };

    uint32_t result = 0;

    uint8_t paper = (attribute & 0b00111000) >> 3;
    uint8_t ink = attribute & 0b00000111;
    bool brightness = (attribute & 0b01000000) > 0;
    bool flash = (attribute & 0b10000000) > 0;

    // Set resulting pixel color based on ZX-Spectrum pixel information (not set => paper color, set => ink color)
    uint8_t paletteIndex = isPixelSet ? ink : paper;
    result = palette[brightness][paletteIndex];

    return result;
}

/// Determine RGBA color for pixel with x,y coordinates on ZX-Spectrum screen
/// \param x X pixel coordinate
/// \param y Y pixel coordinate
/// \param baseAddress screen offset
/// \return pixel RGBA color
uint32_t ScreenZX::GetZXSpectrumPixel(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    static Memory& memory = *_context->pMemory;

    uint32_t result = 0x00000000;

    // Pixel bit index is in lowest 3 bits of address [0:2]
    uint8_t pixelIndex = baseAddress & 0b0000000000000111;
    uint16_t pixelAddress = CalculateXYScreenAddress(x, y, baseAddress);
    uint16_t colorAddress = CalculateXYColorAttrAddress(x, y, baseAddress);

    // Fetch pixel and color data from Z80 memory
    uint8_t pixelByteValue = memory.ReadFromMappedMemoryAddress(pixelAddress);
    uint8_t colorByteValue = memory.ReadFromMappedMemoryAddress(colorAddress);

    // Masking only pixel specified (from 1x8 byte block)
    bool isPixelSet = pixelByteValue & (1 << pixelIndex);
    result = TransformZXSpectrumColorsToRGBA(colorByteValue, isPixelSet);

    return result;
}

/// Optimized version to determine RGBA color for pixel with x,y coordinates on ZX-Spectrum screen
/// \param x X pixel coordinate
/// \param y Y pixel coordinate
/// \param baseAddress screen offset
/// \return pixel RGBA color
uint32_t ScreenZX::GetZXSpectrumPixelOptimized(uint8_t x, uint8_t y, uint16_t baseAddress)
{
    static Memory& memory = *_context->pMemory;

    uint32_t result = 0x00000000;

    // Pixel bit index is in lowest 3 bits of address [0:2]
    uint8_t pixelIndex = baseAddress & 0b0000000000000111;
    uint16_t pixelAddress = CalculateXYScreenAddressOptimized(x, y, baseAddress);
    uint16_t colorAddress = CalculateXYColorAttrAddressOptimized(x, y, baseAddress);

    // Fetch pixel and color data from Z80 memory
    uint8_t pixelByteValue = memory.ReadFromMappedMemoryAddress(pixelAddress);
    uint8_t colorByteValue = memory.ReadFromMappedMemoryAddress(colorAddress);

    // Masking only pixel specified (from 1x8 byte block)
    bool isPixelSet = pixelByteValue & (1 << pixelIndex);
    result = TransformZXSpectrumColorsToRGBA(colorByteValue, isPixelSet);

    return result;
}

RenderTypeEnum ScreenZX::GetLineRenderTypeByTiming(uint32_t tstate)
{
    RenderTypeEnum result = RT_BLANK;

    if (tstate >= _rasterState.blankAreaStart && tstate <= _rasterState.blankAreaEnd)
    {
        result = RT_BLANK;
    }
    else if (tstate >= _rasterState.topBorderAreaStart && tstate <= _rasterState.topBorderAreaEnd)
    {
        result = RT_BORDER;
    }
    else if (tstate >= _rasterState.screenAreaStart && tstate <= _rasterState.screenAreaEnd)
    {
        result = RT_SCREEN;
    }
    else if (tstate >= _rasterState.bottomBorderAreaStart && tstate <= _rasterState.bottomBorderAreaEnd)
    {
        result = RT_BORDER;
    }
    else
    {
        result = RT_BLANK;

        LOGDEBUG("GetRenderTypeByTiming: t-state %d is outside of acceptable frame timings [0; %d]", tstate, _rasterState.maxFrameTiming - 1);
    }

    return result;
}

bool ScreenZX::IsOnScreenByTiming(uint32_t tstate)
{
    bool result = false;

    if (tstate < _rasterState.maxFrameTiming)
    {
        if (tstate >= _rasterState.screenAreaStart && tstate <= _rasterState.screenAreaEnd)
        {
            uint8_t col = tstate % _rasterState.tstatesPerLine;

            if (col >= _rasterState.screenLineAreaStart && col <= _rasterState.screenLineAreaEnd)
            {
                result = true;
            }
        }
    }

    return result;
}

/// endregion </Genuine ZX-Spectrum ULA specifics>

/// region <Screen class methods override>

void ScreenZX::Draw(uint32_t tstate)
{
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];
    const uint16_t tstatesPerLine = rasterDescriptor.pixelsPerLine / 2;
    const uint32_t maxFrameTiming = tstatesPerLine * (rasterDescriptor.vSyncLines + rasterDescriptor.vBlankLines + rasterDescriptor.fullFrameHeight);

    if (tstate < maxFrameTiming)
    {
        const uint8_t line = tstate / tstatesPerLine;
        const uint8_t column = tstate % tstatesPerLine;

        RenderTypeEnum type = GetLineRenderTypeByTiming(tstate);

        switch (type)
        {
            case RT_BLANK:
                break;
            case RT_BORDER:
                break;
            case RT_SCREEN:
                break;
        }
    }
}

void ScreenZX::UpdateScreen()
{
}

///
/// Convert ZX-Spectrum screen (content area only) to RGBA framebuffer
///
void ScreenZX::RenderOnlyMainScreen()
{
    static Memory& memory = *_context->pMemory;
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];

    // Validate required mode(s) set and framebuffer allocated
    if (rasterDescriptor.screenWidth == 0 || rasterDescriptor.screenHeight == 0 || _framebuffer.memoryBuffer == nullptr || _framebuffer.memoryBufferSize == 0)
        return;

    // Get host memory address for main ZX-Spectrum screen (Bank 5, #4000)
    uint8_t* zxScreen = memory.RemapAddressToCurrentBank(0x4000);

    // Get Framebuffer
    uint32_t* framebuffer;
    size_t size;
    GetFramebufferData(&framebuffer, &size);
    int offset = 0;

    // Render ZX-Spectrum screen to framebuffer
    if (framebuffer && size > 0)
    {
        for (int y = 0; y < rasterDescriptor.screenHeight; y++)
        {
            for (int x = 0; x < rasterDescriptor.screenWidth / 8; x++)
            {
                uint8_t pixels = *(zxScreen + _screenLineOffsets[y] + x);
                uint8_t attributes = *(zxScreen + _attrLineOffsets[y] + x);
                uint32_t colorInk = _rgbaColors[attributes];
                uint32_t colorPaper = _rgbaFlashColors[attributes];

                for (int destX = 0; destX < 8; destX++)
                {
                    offset = (rasterDescriptor.screenOffsetTop + y) * rasterDescriptor.fullFrameWidth + (rasterDescriptor.screenOffsetLeft + x * 8 + destX);
                    if (offset < size / sizeof(uint32_t))
                    {
                        // Write RGBA pixel to framebuffer with x,y coordinates and calculated color
                        *(framebuffer + offset) = ((pixels << destX) & 0b10000000) ? colorInk : colorPaper;
                    }
                    else
                    {
                        LOGWARNING("RenderOnlyMainScreen: offset calculated is out of range for the framebuffer. FB: %lx, size: %d, offset: %d", framebuffer, size, offset);
                    }
                }
            }
        }
    }
}

/// endregion </Screen class methods override>