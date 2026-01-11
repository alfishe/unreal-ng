#include "screenzx_test.h"

#include "pch.h"
#include "stdafx.h"

/// region <SetUp / TearDown>

void ScreenZX_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _cpu = new Core(_context);
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
#endif  // _DEBUG
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
                // ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr,
                         addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            // snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            // std::cout << message << std::endl;
#endif  // _DEBUG
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
#endif  // _DEBUG
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
                // ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr,
                         addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            // snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            // std::cout << message << std::endl;
#endif  // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, TransformZXSpectrumColorsToRGBA)
{
    {  // Black on non-bright white - default screen colors
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
        snprintf(message, sizeof message, "ZX-Spectrum 48k has 224 t-states per line. Found: %d",
                 _screenzx->_rasterState.tstatesPerLine);
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
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
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
        snprintf(message, sizeof message, "ZX-Spectrum 128k has 228 t-states per line. Found: %d",
                 _screenzx->_rasterState.tstatesPerLine);
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
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </ZX-Spectrum 128k>

    /// region <Pentagon>

    _screenzx->SetVideoMode(M_PENTAGON128K);
    _screenzx->CreateTimingTable();

    // Check line duration
    if (_screenzx->_rasterState.tstatesPerLine != 224)
    {
        snprintf(message, sizeof message, "Pentagon has 224 t-states per line. Found: %d",
                 _screenzx->_rasterState.tstatesPerLine);
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
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Left border
        if (i >= 48 && i <= 71)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Screen area
        if (i >= 72 && i <= 199)
        {
            if (type != RT_SCREEN)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_SCREEN).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Right border
        if (i >= 200 && i <= 223)
        {
            if (type != RT_BORDER)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BORDER).c_str(), Screen::GetRenderTypeName(type).c_str());
                FAIL() << message << std::endl;
            }
        }

        // Ensure unused part of lookup table is blank
        if (i >= 224)
        {
            if (type != RT_BLANK)
            {
                snprintf(message, sizeof message, "line offset (t-states): %d, expected type: %s, found: %s", i,
                         Screen::GetRenderTypeName(RT_BLANK).c_str(), Screen::GetRenderTypeName(type).c_str());
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
    _screenzx->SetVideoMode(M_PENTAGON128K);

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
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false,
                         coordsFound);
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
                        snprintf(
                            message, sizeof message,
                            "tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)",
                            tstate, line, column, false, coordsFound, x, y);
                        FAIL() << message << std::endl;
                    }
                }
                else
                {
                    if (!coordsFound)
                    {
                        snprintf(
                            message, sizeof message,
                            "tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)",
                            tstate, line, column, true, coordsFound, x, y);
                        FAIL() << message << std::endl;
                    }

                    if (x % 2 == 1)
                    {
                        snprintf(message, sizeof message,
                                 "tstate: %d (line %d, col: %d), (x: %d, y: %d), X cannot be odd. ULA draws 2 pixels "
                                 "per t-state",
                                 tstate, line, column, x, y);
                        FAIL() << message << std::endl;
                    }
                }
            }
            else
            {
                if (coordsFound)
                {
                    snprintf(message, sizeof message,
                             "tstate: %d (line: %d, col: %d), expected coordsFound value: %d, found: %d", tstate, line,
                             column, false, coordsFound);
                    FAIL() << message << std::endl;
                }
            }

            /// endregion </Check if position is within framebuffer>

            if (coordsFound)
            {
                if (x > rasterDescriptor.fullFrameWidth)
                {
                    snprintf(message, sizeof message,
                             "tstate: %d (line: %d, col: %d), X expected value: %d, found: %d "
                             "(rasterDescriptor.fullFrameWidth: %d)",
                             tstate, line, column, false, x, rasterDescriptor.fullFrameWidth);
                    FAIL() << message << std::endl;
                }

                if (y > rasterDescriptor.fullFrameHeight)
                {
                    snprintf(message, sizeof message,
                             "tstate: %d (line: %d, col: %d), Y expected value: %d, found: %d "
                             "(rasterDescriptor.fullFrameHeight: %d)",
                             tstate, line, column, false, y, rasterDescriptor.fullFrameHeight);
                    FAIL() << message << std::endl;
                }
            }
        }
        else
        {
            if (coordsFound)
            {
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false,
                         coordsFound);
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
            if (column >= rasterDescriptor.screenOffsetLeft &&
                column < rasterDescriptor.screenOffsetLeft + rasterDescriptor.screenWidth)
            {
                if (!coordsFound)
                {
                    snprintf(message, sizeof message,
                             "tstate: %d (line %d, col: %d), expected coordsFound value: %d, found: %d (x: %d, y: %d)",
                             tstate, line, column, true, coordsFound, x, y);
                    FAIL() << message << std::endl;
                }
            }
        }
        else
        {
            if (coordsFound)
            {
                snprintf(message, sizeof message, "tstate: %d, expected value: %d, found: %d", tstate, false,
                         coordsFound);
                FAIL() << message << std::endl;
            }
        }
    }

    /// endregion </Genuine ZX-Spectrum 48k>
}

/// endregion </ULA video render tests>

/// region <T-state Coordinate LUT tests>

/// @brief Test that LUT is properly initialized after mode change
TEST_F(ScreenZX_Test, TstateLUT_InitializedOnModeChange)
{
    // Set video mode - this should trigger CreateTstateLUT
    _screenzx->SetVideoMode(M_ZX48);

    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;

    // Verify at least some entries are initialized
    bool hasBlank = false;
    bool hasBorder = false;
    bool hasScreen = false;

    for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
    {
        const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];
        if (lut.renderType == RT_BLANK)
            hasBlank = true;
        if (lut.renderType == RT_BORDER)
            hasBorder = true;
        if (lut.renderType == RT_SCREEN)
            hasScreen = true;
    }

    EXPECT_TRUE(hasBlank) << "LUT should contain BLANK entries";
    EXPECT_TRUE(hasBorder) << "LUT should contain BORDER entries";
    EXPECT_TRUE(hasScreen) << "LUT should contain SCREEN entries";
}

/// @brief Test that LUT entries match original TransformTstateToFramebufferCoords results
TEST_F(ScreenZX_Test, TstateLUT_MatchesTransformTstateToFramebufferCoords)
{
    _screenzx->SetVideoMode(M_ZX48);

    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;

    for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
    {
        const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];

        uint16_t origX, origY;
        bool origFound = _screenzx->TransformTstateToFramebufferCoords(t, &origX, &origY);

        if (origFound)
        {
            EXPECT_NE(lut.framebufferX, UINT16_MAX) << "t-state " << t << ": LUT should have valid framebuffer X";
            EXPECT_EQ(lut.framebufferX, origX) << "t-state " << t << ": LUT framebufferX mismatch";
            EXPECT_EQ(lut.framebufferY, origY) << "t-state " << t << ": LUT framebufferY mismatch";
        }
        else
        {
            EXPECT_EQ(lut.framebufferX, UINT16_MAX) << "t-state " << t << ": LUT should mark invisible with UINT16_MAX";
            EXPECT_EQ(lut.renderType, RT_BLANK) << "t-state " << t << ": Invisible should be RT_BLANK";
        }
    }
}

/// @brief Test that LUT entries match original TransformTstateToZXCoords results
TEST_F(ScreenZX_Test, TstateLUT_MatchesTransformTstateToZXCoords)
{
    _screenzx->SetVideoMode(M_ZX48);

    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;

    for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
    {
        const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];

        uint16_t origZxX, origZxY;
        bool origFound = _screenzx->TransformTstateToZXCoords(t, &origZxX, &origZxY);

        if (origFound)
        {
            EXPECT_EQ(lut.renderType, RT_SCREEN) << "t-state " << t << ": Should be RT_SCREEN when ZX coords valid";
            EXPECT_EQ(lut.zxX, origZxX) << "t-state " << t << ": LUT zxX mismatch";
            EXPECT_EQ(lut.zxY, origZxY) << "t-state " << t << ": LUT zxY mismatch";

            // Verify pre-computed symbolX and pixelXBit
            EXPECT_EQ(lut.symbolX, origZxX / 8) << "t-state " << t << ": LUT symbolX mismatch";
            EXPECT_EQ(lut.pixelXBit, origZxX % 8) << "t-state " << t << ": LUT pixelXBit mismatch";
        }
        else if (lut.renderType != RT_BLANK)
        {
            // Border case - no specific check needed for zxX/zxY values
            // The important thing is renderType is correctly set
            EXPECT_EQ(lut.renderType, RT_BORDER) << "t-state " << t << ": Non-screen should be BORDER";
        }
    }
}

/// @brief Test that Draw and DrawOriginal produce the same framebuffer output
TEST_F(ScreenZX_Test, TstateLUT_DrawProducesSameOutputAsDrawOriginal)
{
    // Initialize with memory
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->SetVideoMode(M_ZX48);
    _screenzx->InitFrame();

    // Fill screen memory with test pattern
    Memory& memory = *_context->pMemory;
    for (uint16_t addr = 0x4000; addr < 0x5B00; addr++)
    {
        memory.DirectWriteToZ80Memory(addr, static_cast<uint8_t>(addr & 0xFF));
    }

    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;

    // Get framebuffer info
    uint32_t* fb1 = nullptr;
    size_t fbSize = 0;
    _screenzx->GetFramebufferData(&fb1, &fbSize);
    size_t fbPixels = fbSize / sizeof(uint32_t);

    // Allocate second buffer for comparison
    std::vector<uint32_t> fb2(fbPixels, 0);

    // Draw using original method
    for (uint32_t t = 0; t < maxTstates; t++)
    {
        _screenzx->DrawOriginal(t);
    }

    // Copy framebuffer content
    memcpy(fb2.data(), fb1, fbSize);

    // Clear framebuffer
    memset(fb1, 0, fbSize);

    // Draw using LUT method
    for (uint32_t t = 0; t < maxTstates; t++)
    {
        _screenzx->Draw(t);
    }

    // Compare framebuffers
    int differences = 0;
    for (size_t i = 0; i < fbPixels && differences < 10; i++)
    {
        if (fb1[i] != fb2[i])
        {
            differences++;
            GTEST_LOG_(ERROR) << "Pixel " << i << ": LUT=" << std::hex << fb1[i] << " Original=" << fb2[i] << std::dec;
        }
    }

    EXPECT_EQ(differences, 0) << "Draw and DrawOriginal should produce identical framebuffer output";
}

/// @brief Test LUT initialization for multiple video modes
TEST_F(ScreenZX_Test, TstateLUT_InitializedForAllModes)
{
    VideoModeEnum modes[] = {M_ZX48, M_ZX128, M_PENTAGON128K};
    const char* modeNames[] = {"M_ZX48", "M_ZX128", "M_PENTAGON128K"};

    for (int i = 0; i < 3; i++)
    {
        _screenzx->SetVideoMode(modes[i]);

        const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;

        // Count render types
        int blankCount = 0, borderCount = 0, screenCount = 0;

        for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
        {
            const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];
            switch (lut.renderType)
            {
                case RT_BLANK:
                    blankCount++;
                    break;
                case RT_BORDER:
                    borderCount++;
                    break;
                case RT_SCREEN:
                    screenCount++;
                    break;
            }
        }

        EXPECT_GT(blankCount, 0) << modeNames[i] << ": Should have BLANK entries";
        EXPECT_GT(borderCount, 0) << modeNames[i] << ": Should have BORDER entries";
        EXPECT_GT(screenCount, 0) << modeNames[i] << ": Should have SCREEN entries";

        GTEST_LOG_(INFO) << modeNames[i] << ": BLANK=" << blankCount << " BORDER=" << borderCount
                         << " SCREEN=" << screenCount;
    }
}

/// endregion </T-state Coordinate LUT tests>

/// region <Batch 8-Pixel Tests - Phase 4-5>

/// @brief Test that DrawBatch8_Scalar produces correct output for known pixel patterns
TEST_F(ScreenZX_Test, Batch8_ScalarProducesCorrectOutput)
{
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->SetVideoMode(M_ZX48);
    _screenzx->InitFrame();

    // Set a known pixel pattern at (0,0): alternating pixels
    Memory& memory = *_context->pMemory;
    memory.DirectWriteToZ80Memory(0x4000, 0xAA);  // 10101010 - alternating pixels
    memory.DirectWriteToZ80Memory(0x5800, 0x38);  // Default attribute (black on white)

    // Get destination buffer
    uint32_t pixels[8] = {0};
    _screenzx->DrawBatch8_Scalar(0, 0, pixels);

    // Check alternating pattern
    uint32_t ink = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, true);
    uint32_t paper = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, false);

    EXPECT_EQ(pixels[0], ink) << "Pixel 0 should be ink (bit 7 = 1)";
    EXPECT_EQ(pixels[1], paper) << "Pixel 1 should be paper (bit 6 = 0)";
    EXPECT_EQ(pixels[2], ink) << "Pixel 2 should be ink (bit 5 = 1)";
    EXPECT_EQ(pixels[3], paper) << "Pixel 3 should be paper (bit 4 = 0)";
    EXPECT_EQ(pixels[4], ink) << "Pixel 4 should be ink (bit 3 = 1)";
    EXPECT_EQ(pixels[5], paper) << "Pixel 5 should be paper (bit 2 = 0)";
    EXPECT_EQ(pixels[6], ink) << "Pixel 6 should be ink (bit 1 = 1)";
    EXPECT_EQ(pixels[7], paper) << "Pixel 7 should be paper (bit 0 = 0)";
}

#ifdef __ARM_NEON
/// @brief Test that DrawBatch8_NEON produces same output as scalar version
TEST_F(ScreenZX_Test, Batch8_NEONMatchesScalar)
{
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->SetVideoMode(M_ZX48);
    _screenzx->InitFrame();

    // Fill screen with random-ish pattern
    Memory& memory = *_context->pMemory;
    for (uint16_t addr = 0x4000; addr < 0x5B00; addr++)
    {
        memory.DirectWriteToZ80Memory(addr, static_cast<uint8_t>(addr * 37));  // Pseudo-random
    }

    // Compare scalar vs NEON for multiple positions
    for (uint8_t y = 0; y < 192; y += 47)  // Sample a few lines
    {
        for (uint8_t x = 0; x < 32; x += 7)  // Sample a few columns
        {
            uint32_t scalarPixels[8] = {0};
            uint32_t neonPixels[8] = {0};

            _screenzx->DrawBatch8_Scalar(y, x, scalarPixels);
            _screenzx->DrawBatch8_NEON(y, x, neonPixels);

            for (int i = 0; i < 8; i++)
            {
                EXPECT_EQ(scalarPixels[i], neonPixels[i])
                    << "Mismatch at y=" << (int)y << " x=" << (int)x << " pixel=" << i;
            }
        }
    }
}
#endif

/// @brief Test that RenderScreen_Batch8 produces same output as RenderOnlyMainScreen
TEST_F(ScreenZX_Test, Batch8_RenderScreenMatchesPerPixel)
{
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->SetVideoMode(M_ZX48);
    _screenzx->InitFrame();

    // Fill screen with test pattern
    Memory& memory = *_context->pMemory;
    for (uint16_t addr = 0x4000; addr < 0x5B00; addr++)
    {
        memory.DirectWriteToZ80Memory(addr, static_cast<uint8_t>(addr & 0xFF));
    }

    // Get framebuffer info
    uint32_t* fb = nullptr;
    size_t fbSize = 0;
    _screenzx->GetFramebufferData(&fb, &fbSize);

    // Render using per-pixel method
    _screenzx->RenderOnlyMainScreen();

    // Copy framebuffer
    std::vector<uint32_t> perPixelFb(fbSize / sizeof(uint32_t));
    memcpy(perPixelFb.data(), fb, fbSize);

    // Clear framebuffer
    memset(fb, 0, fbSize);

    // Render using batch method
    _screenzx->RenderScreen_Batch8();

    // Compare screen area only (not borders)
    const RasterDescriptor& rd = _screenzx->rasterDescriptors[_screenzx->_mode];
    int differences = 0;

    for (uint16_t y = 0; y < 192 && differences < 10; y++)
    {
        for (uint16_t x = 0; x < 256 && differences < 10; x++)
        {
            size_t offset = (rd.screenOffsetTop + y) * rd.fullFrameWidth + rd.screenOffsetLeft + x;
            if (fb[offset] != perPixelFb[offset])
            {
                differences++;
                GTEST_LOG_(ERROR) << "Pixel (" << x << "," << y << "): Batch8=" << std::hex << fb[offset]
                                  << " PerPixel=" << perPixelFb[offset] << std::dec;
            }
        }
    }

    EXPECT_EQ(differences, 0) << "RenderScreen_Batch8 should match RenderOnlyMainScreen output";
}

/// endregion </Batch 8-Pixel Tests>
