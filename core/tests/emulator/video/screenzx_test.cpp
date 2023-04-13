#include "stdafx.h"
#include "pch.h"

#include "screenzx_test.h"

/// region <SetUp / TearDown>

void ScreenZX_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _cpu = new CPU(_context);
    bool initialized = _cpu->Init();
    _screenzx = new ScreenZXCUT(_context);
}

void ScreenZX_Test::TearDown()
{
    if (_screenzx != nullptr)
    {
        delete _screenzx;
        _screenzx = nullptr;
    }

    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Setup / TearDown>

/// region <ZX screen coordinates tests>

TEST_F(ScreenZX_Test, CalculateXYScreenAddress)
{
    char message[256];
    uint16_t addr = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYScreenAddress(x, y, 0x4000);

#ifdef _DEBUG
            snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYScreenAddressCorrectness)
{
    char message[256];
    uint16_t addr = 0;
    uint16_t addrOptimized = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYScreenAddress(x, y, 0x4000);
            addrOptimized = _screenzx->CalculateXYScreenAddressOptimized(x, y, 0x4000);

            if (addr != addrOptimized)
            {
                //ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr, addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            //snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            //std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYColorAttrAddress)
{
    char message[256];
    uint16_t addr = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYColorAttrAddress(x, y, 0x4000);

#ifdef _DEBUG
            snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYColorAddressCorrectness)
{
    char message[256];
    uint16_t addr = 0;
    uint16_t addrOptimized = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYColorAttrAddress(x, y, 0x4000);
            addrOptimized = _screenzx->CalculateXYColorAttrAddressOptimized(x, y, 0x4000);

            if (addr != addrOptimized)
            {
                //ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr, addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            //snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            //std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, TransformZXSpectrumColorsToRGBA)
{
    { // Black on non-bright white - default screen colors
        uint32_t inkColor = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, true);
        uint32_t paperColor = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, false);

        EXPECT_EQ(inkColor, 0xFF000000);
        EXPECT_EQ(paperColor, 0xFFCACACA);
    }
}

/// endregion </ZX screen coordinates tests>

/// region <ULA tables creation tests>

TEST_F(ScreenZX_Test, CreateTimingTable)
{
    char message[256];

    /// region <ZX-Spectrum 48k>

    // Genuine ZX-Spectrum 48k
    // t-states per line: 224
    //
    _screenzx->SetVideoMode(M_ZX48);
    _screenzx->CreateTimingTable();

    // Check line duration
    if (_screenzx->_rasterState.tstatesPerLine != 224)
    {
        snprintf(message, sizeof message, "ZX-Spectrum 48k has 224 t-states per line. Found: %d", _screenzx->_rasterState.tstatesPerLine);
        FAIL() << message << std::endl;
    }

    RenderTypeEnum type = RT_BLANK;
    for (int i = 0; i <= 255; i++)
    {
        type = _screenzx->_screenLineRenderers[i];

        // hBlank + hSync
        if (i >= 0 && i <= 47)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </ZX-Spectrum 48k>

    /// region <ZX-Spectrum 128k>

    // Genuine ZX-Spectrum 128k
    // t-states per line: 228
    //
    _screenzx->SetVideoMode(M_ZX128);
    _screenzx->CreateTimingTable();

    // Check line duration
    if (_screenzx->_rasterState.tstatesPerLine != 228)
    {
        snprintf(message, sizeof message, "ZX-Spectrum 128k has 228 t-states per line. Found: %d", _screenzx->_rasterState.tstatesPerLine);
        FAIL() << message << std::endl;
    }

    type = RT_BLANK;
    for (int i = 0; i <= 255; i++)
    {
        type = _screenzx->_screenLineRenderers[i];

        // hBlank + hSync
        if (i >= 0 && i <= 47)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </ZX-Spectrum 128k>

    /// region <Pentagon>

    _screenzx->SetVideoMode(M_PMC);
    _screenzx->CreateTimingTable();

    // Check line duration
    if (_screenzx->_rasterState.tstatesPerLine != 224)
    {
        snprintf(message, sizeof message, "Pentagon has 224 t-states per line. Found: %d", _screenzx->_rasterState.tstatesPerLine);
        FAIL() << message << std::endl;
    }

    type = RT_BLANK;
    for (int i = 0; i <= 255; i++)
    {
        type = _screenzx->_screenLineRenderers[i];

        // hBlank + hSync
        if (i >= 0 && i <= 47)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i, Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </Pentagon>
}

/// endregion </ULA tables creation tests>

/// region <ULA video render tests>

TEST_F(ScreenZX_Test, GetRenderTypeByTiming)
{
    char message[256];

    /// region <Genuine ZX-Spectrum 48k>

    // Value is used by renderer for sanity checks
    _context->config.frame = 69888;

    // Genuine ZX-Spectrum
    // Max t-state = 69888
    // [0; 5375]        - Top Blank
    // [5476; 16127]    - Top Border
    // [16128; 59135]   - Screen
    // [59136; 69887]   - Bottom Border
    _screenzx->SetVideoMode(M_ZX48);

    for (uint32_t tstate = 0; tstate < 70000; tstate++)
    {
        RenderTypeEnum type = _screenzx->GetLineRenderTypeByTiming(tstate);

        if (tstate >= 0 && tstate <= 5375)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BLANK, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 5476 && tstate <= 16127)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BORDER, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 16128 && tstate <= 59135)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_SCREEN, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 59136 && tstate <= 69887)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BORDER, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 69888)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "tstate: %05d, expected type: %d, found: %d", tstate, RT_BLANK, type);
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </Genuine ZX-Spectrum 48k>

    /// region <Pentagon>

    // Value is used by renderer for sanity checks
    _context->config.frame = 71680;

    // Pentagon
    // Max t-state = 71680
    // [0; 7167]        - Top Blank
    // [7168; 17919]    - Top Border
    // [17920; 60927]   - Screen
    // [60928; 71679]   - Bottom Border
    _screenzx->SetVideoMode(M_PMC);

    for (uint32_t tstate = 0; tstate < 72000; tstate++)
    {
        RenderTypeEnum type = _screenzx->GetLineRenderTypeByTiming(tstate);

        if (tstate >= 0 && tstate <= 7167)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BLANK, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 7168 && tstate <= 17919)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BORDER, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 17920 && tstate <= 60927)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_SCREEN, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 60928 && tstate <= 71679)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "tstate: %d, expected type: %d, found: %d", tstate, RT_BORDER, type);
                FAIL() << message << std::endl;
            }
        }

        if (tstate >= 71680)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "tstate: %05d, expected type: %d, found: %d", tstate, RT_BLANK, type);
                FAIL() << message << std::endl;
            }
        }
    }


    /// endregion </Pentagon>
}

TEST_F(ScreenZX_Test, TransformTstateToFramebufferCoords)
{
    char message[256];

    /// region <Genuine ZX-Spectrum 48k>

    // Value is used by renderer for sanity checks
    _context->config.frame = 69888;

    // Genuine ZX-Spectrum
    // Max t-state = 69888
    // [0; 5375]        - Top Blank
    // [5476; 16127]    - Top Border
    // [16128; 59135]   - Screen
    // [59136; 69887]   - Bottom Border
    _screenzx->SetVideoMode(M_ZX48);
    RasterDescriptor rasterDescriptor = _screenzx->rasterDescriptors[_screenzx->_mode];
    RasterState& rasterState = _screenzx->_rasterState;

    bool coordsFound;
    uint16_t x;
    uint16_t y;

    for (uint32_t tstate = 0; tstate < 70000; tstate++)
    {
        const uint16_t line = tstate / rasterState.tstatesPerLine;
        const uint16_t column = (tstate % rasterState.tstatesPerLine) * rasterState.pixelsPerTState;

        coordsFound = _screenzx->TransformTstateToFramebufferCoords(tstate, &x, &y);

        if (tstate >= 0 && tstate <= 5375)
        {
            if (coordsFound)
            {
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false, coordsFound);
                FAIL() << message << std::endl;
            }
        }
        else if (tstate >= 5376 && tstate <= 69887)
        {
            /// region <Check if position is within framebuffer>

            if (line >= 24 && line < 312)
            {
                if (column >= rasterDescriptor.fullFrameWidth)
                {
                    if (coordsFound)
                    {
                        snprintf(message, sizeof message, "tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)", tstate, line, column, false, coordsFound, x, y);
                        FAIL() << message << std::endl;
                    }
                }
                else
                {
                    if (!coordsFound)
                    {
                        snprintf(message, sizeof message, "tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)", tstate, line, column, true, coordsFound, x, y);
                        FAIL() << message << std::endl;
                    }

                    if (x % 2 == 1)
                    {
                        snprintf(message, sizeof message, "tstate: %d (line %d, col: %d), (x: %d, y: %d), X cannot be odd. ULA draws 2 pixels per t-state", tstate, line, column, x, y);
                        FAIL() << message << std::endl;
                    }
                }
            }
            else
            {
                if (coordsFound)
                {
                    snprintf(message, sizeof message, "tstate: %d (line: %d, col: %d), expected coordsFound value: %d, found: %d", tstate, line, column, false, coordsFound);
                    FAIL() << message << std::endl;
                }
            }

            /// endregion </Check if position is within framebuffer>


            if (coordsFound)
            {
                if (x > rasterDescriptor.fullFrameWidth)
                {
                    snprintf(message, sizeof message, "tstate: %d (line: %d, col: %d), X expected value: %d, found: %d (rasterDescriptor.fullFrameWidth: %d)", tstate, line, column, false, x, rasterDescriptor.fullFrameWidth);
                    FAIL() << message << std::endl;
                }

                if (y > rasterDescriptor.fullFrameHeight)
                {
                    snprintf(message, sizeof message, "tstate: %d (line: %d, col: %d), Y expected value: %d, found: %d (rasterDescriptor.fullFrameHeight: %d)", tstate, line, column, false, y, rasterDescriptor.fullFrameHeight);
                    FAIL() << message << std::endl;
                }
            }
        }
        else
        {
            if (coordsFound)
            {
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false, coordsFound);
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </Genuine ZX-Spectrum 48k>
}

TEST_F(ScreenZX_Test, TransformTstateToZXCoords)
{
    char message[256];

    /// region <Genuine ZX-Spectrum 48k>

    // Value is used by renderer for sanity checks
    _context->config.frame = 69888;

    // Genuine ZX-Spectrum
    // Max t-state = 69888
    // [0; 5375]        - Top Blank
    // [5476; 16127]    - Top Border
    // [16128; 59135]   - Screen
    // [59136; 69887]   - Bottom Border
    _screenzx->SetVideoMode(M_ZX48);
    RasterDescriptor rasterDescriptor = _screenzx->rasterDescriptors[_screenzx->_mode];
    RasterState& rasterState = _screenzx->_rasterState;

    bool coordsFound;
    uint16_t x;
    uint16_t y;

    for (uint32_t tstate = 0; tstate < 70000; tstate++)
    {
        const uint16_t line = tstate / rasterState.tstatesPerLine;
        const uint16_t column = (tstate % rasterState.tstatesPerLine) * rasterState.pixelsPerTState;

        coordsFound = _screenzx->TransformTstateToZXCoords(tstate, &x, &y);

        if (tstate >= rasterState.screenAreaStart && tstate <= rasterState.screenAreaEnd)
        {
            if (column >= rasterDescriptor.screenOffsetLeft && column < rasterDescriptor.screenOffsetLeft + rasterDescriptor.screenWidth)
            {
                if (!coordsFound)
                {
                    snprintf(message, sizeof message,"tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)", tstate, line, column, true, coordsFound, x, y);
                    FAIL() << message << std::endl;
                }
            }
        }
        else
        {
            if (coordsFound)
            {
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false, coordsFound);
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </Genuine ZX-Spectrum 48k>
}

/// endregion </ULA video render tests>
