#include <algorithm>
#include <set>
#include <vector>
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
    // Per-line render table in the renderer's line origin: left border, paper,
    // right border, then horizontal blank/sync closing the line. It must agree
    // with the per-T LUT the renderer draws with on a paper line.
    struct Case { VideoModeEnum mode; uint16_t tstatesPerLine; };
    for (const Case c : {Case{M_ZX48, 224}, Case{M_ZX128, 228}, Case{M_PENTAGON128K, 224}})
    {
        SCOPED_TRACE(Screen::GetVideoModeName(c.mode));
        _screenzx->SetVideoMode(c.mode);
        _screenzx->CreateTimingTable();
        ASSERT_EQ(_screenzx->_rasterState.tstatesPerLine, c.tstatesPerLine);

        for (int i = 0; i <= 255; i++)
        {
            RenderTypeEnum expected = RT_BLANK;
            if (i <= 23 || (i >= 152 && i <= 175))
                expected = RT_BORDER;
            else if (i <= 151)
                expected = RT_SCREEN;
            EXPECT_EQ(_screenzx->_screenLineRenderers[i], expected) << "line offset (t-states): " << i;

            if (i < c.tstatesPerLine)
            {
                const uint32_t t = _screenzx->_rasterState.screenAreaStart + i;
                EXPECT_EQ(_screenzx->_tstateLUT[t].renderType, expected) << "LUT disagrees at line offset " << i;
            }
        }
    }
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
    // InitFrame() re-detects the video mode from config (base-mode mapping in
    // InitRaster), so the machine must be configured as ZX48 - the bare test
    // context defaults to mem_model 0 (Pentagon) with frame 0, which fails
    // the SetVideoMode frame-duration sanity check
    _context->config.mem_model = MM_SPECTRUM48;
    _context->config.frame = 69888;
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
    // InitFrame() re-detects the video mode from config (base-mode mapping in
    // InitRaster), so the machine must be configured as ZX48 - the bare test
    // context defaults to mem_model 0 (Pentagon) with frame 0, which fails
    // the SetVideoMode frame-duration sanity check
    _context->config.mem_model = MM_SPECTRUM48;
    _context->config.frame = 69888;
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
    // InitFrame() re-detects the video mode from config (base-mode mapping in
    // InitRaster), so the machine must be configured as ZX48 - the bare test
    // context defaults to mem_model 0 (Pentagon) with frame 0, which fails
    // the SetVideoMode frame-duration sanity check
    _context->config.mem_model = MM_SPECTRUM48;
    _context->config.frame = 69888;
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
    // InitFrame() re-detects the video mode from config (base-mode mapping in
    // InitRaster), so the machine must be configured as ZX48 - the bare test
    // context defaults to mem_model 0 (Pentagon) with frame 0, which fails
    // the SetVideoMode frame-duration sanity check
    _context->config.mem_model = MM_SPECTRUM48;
    _context->config.frame = 69888;
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

/// @brief Pentagon default (non-overscan) framing: the paper sits in the middle of the 352x288
/// framebuffer with equal 48 px borders on the left and right and 48 lines top and bottom.
/// Guards the UI framing - the device screen shows the framebuffer as-is in this mode.
TEST_F(ScreenZX_Test, Pentagon_DefaultFraming_IsSymmetric)
{
    _context->config.frame = 71680;
    _screenzx->SetVideoMode(M_PENTAGON128K);

    const RasterDescriptor& rd = _screenzx->rasterDescriptors[_screenzx->_mode];
    ASSERT_EQ(rd.fullFrameWidth, 352);
    ASSERT_EQ(rd.fullFrameHeight, 288);

    uint16_t minX = UINT16_MAX, maxX = 0, minY = UINT16_MAX, maxY = 0;
    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;
    for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
    {
        const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];
        if (lut.renderType != RT_SCREEN)
            continue;
        // Each t-state covers 2 pixels; the LUT stores the first one
        minX = std::min<uint16_t>(minX, lut.framebufferX);
        maxX = std::max<uint16_t>(maxX, lut.framebufferX + 1);
        minY = std::min<uint16_t>(minY, lut.framebufferY);
        maxY = std::max<uint16_t>(maxY, lut.framebufferY);
    }

    const int leftBorder = minX;
    const int rightBorder = rd.fullFrameWidth - 1 - maxX;
    const int topBorder = minY;
    const int bottomBorder = rd.fullFrameHeight - 1 - maxY;

    EXPECT_EQ(maxX - minX + 1, 256) << "Paper width";
    EXPECT_EQ(maxY - minY + 1, 192) << "Paper height";
    EXPECT_EQ(leftBorder, 48);
    EXPECT_EQ(rightBorder, leftBorder) << "Left/right borders must be symmetric by default";
    EXPECT_EQ(topBorder, 48);
    EXPECT_EQ(bottomBorder, topBorder) << "Top/bottom borders must be symmetric by default";
}

/// @brief Pentagon overscan (384x304) keeps the paper at (48, 64); the SYMMETRIC_HORIZONTAL
/// viewport preset must therefore yield equal left/right borders after cropping.
TEST_F(ScreenZX_Test, Pentagon_OverscanSymmetricViewport_HasEqualSideBorders)
{
    _context->config.frame = 71680;
    _screenzx->SetVideoMode(M_P384);

    const RasterDescriptor& rd = _screenzx->rasterDescriptors[_screenzx->_mode];
    ASSERT_EQ(rd.fullFrameWidth, 384);

    uint16_t minX = UINT16_MAX, maxX = 0;
    const uint32_t maxTstates = _screenzx->_rasterState.maxFrameTiming;
    for (uint32_t t = 0; t < maxTstates && t < ScreenZX::MAX_FRAME_TSTATES; t++)
    {
        const ScreenZX::TstateCoordLUT& lut = _screenzx->_tstateLUT[t];
        if (lut.renderType != RT_SCREEN)
            continue;
        minX = std::min<uint16_t>(minX, lut.framebufferX);
        maxX = std::max<uint16_t>(maxX, lut.framebufferX + 1);
    }

    const DisplayViewport& vp = ViewportPresets::SYMMETRIC_HORIZONTAL;
    const int leftBorder = minX - vp.cropLeft;
    const int rightBorder = (rd.fullFrameWidth - vp.cropRight) - 1 - maxX;
    EXPECT_EQ(leftBorder, 48);
    EXPECT_EQ(rightBorder, leftBorder) << "SYMMETRIC_HORIZONTAL must crop the overscan to equal side borders";
}

/// @brief Beam -> framebuffer mapping follows where each mode stores its window:
/// ZX keeps borders 1:1, ATM stores only the 320/640 x 200 window (plus top and
/// bottom border rows), Profi hires puts 512 px at 4 px/T between 48 px borders.
TEST_F(ScreenZX_Test, TransformTstateToFramebufferCoords_ModeWindows)
{
    auto T = [](uint32_t line, uint32_t tInLine) { return line * 224 + tInLine; };
    uint16_t x = 0, y = 0;

    _context->config.frame = 69888;
    _context->config.mem_model = MM_SPECTRUM48;
    _screenzx->SetVideoMode(M_ZX48);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(72, 24), &x, &y));
    EXPECT_EQ(x, 48);
    EXPECT_EQ(y, 48);

    _context->config.mem_model = MM_ATM710;
    _screenzx->SetVideoMode(M_ATM16);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(68, 8), &x, &y));
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 44);
    EXPECT_FALSE(_screenzx->TransformTstateToFramebufferCoords(T(68, 7), &x, &y)) << "no side border stored";
    EXPECT_FALSE(_screenzx->TransformTstateToFramebufferCoords(T(68, 168), &x, &y));

    _screenzx->SetVideoMode(M_ATMHR);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(68, 9), &x, &y));
    EXPECT_EQ(x, 4) << "4 px per T in hires";

    // Pentagon overscan stores 16 lines more on top: its first stored row is raster line 16, not 32, so the paper
    // (raster line 80) is row 64; the Pentagon proper has row 48
    _context->config.frame = 71680;
    _context->config.mem_model = MM_PENTAGON;
    _screenzx->SetVideoMode(M_PENTAGON128K);
    EXPECT_EQ(_screenzx->FirstStoredRasterLine(), 32);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(80, 24), &x, &y));
    EXPECT_EQ(x, 48);
    EXPECT_EQ(y, 48);
    _screenzx->SetVideoMode(M_P384);
    EXPECT_EQ(_screenzx->FirstStoredRasterLine(), 16);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(80, 24), &x, &y));
    EXPECT_EQ(x, 48);
    EXPECT_EQ(y, 64) << "the paper's first row in the overscan frame";
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(16, 0), &x, &y)) << "the first stored line";
    EXPECT_EQ(y, 0);

    _context->config.mem_model = MM_PROFI;
    _screenzx->SetVideoMode(M_PROFIHR);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(48, 23), &x, &y));
    EXPECT_EQ(x, 46) << "left border at 2 px/T";
    EXPECT_EQ(y, 24);
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(48, 25), &x, &y));
    EXPECT_EQ(x, 52) << "paper at 4 px/T";
    ASSERT_TRUE(_screenzx->TransformTstateToFramebufferCoords(T(48, 152), &x, &y));
    EXPECT_EQ(x, 560) << "right border starts after 48 + 512";
}

/// region <ZX DLSS plane B>

namespace
{
/// Fill the 48K screen (bitmap + attributes) with deterministic noise
void FillScreenNoise(Memory* memory, uint32_t seed)
{
    for (uint16_t addr = 0x4000; addr < 0x5B00; addr++)
    {
        seed = seed * 1103515245u + 12345u;
        memory->DirectWriteToZ80Memory(addr, static_cast<uint8_t>(seed >> 16));
    }
}

std::vector<uint32_t> FramebufferCopy(ScreenZXCUT* screen)
{
    const auto* fb = reinterpret_cast<const uint32_t*>(screen->_framebuffer.memoryBuffer);
    return std::vector<uint32_t>(fb, fb + screen->_framebuffer.memoryBufferSize / sizeof(uint32_t));
}
}  // namespace

TEST_F(ScreenZX_Test, PlaneB_OffByDefault_NoBuffer)
{
    size_t count = 123;
    EXPECT_FALSE(_screenzx->IsPlaneBEnabled());
    EXPECT_EQ(_screenzx->GetPlaneB(&count), nullptr);
    EXPECT_EQ(count, 0u);
}

TEST_F(ScreenZX_Test, PlaneB_PixelsIdenticalOnAndOff)
{
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->InitFrame();
    FillScreenNoise(_cpu->GetMemory(), 7);
    const uint32_t frameT = _screenzx->_rasterState.maxFrameTiming;

    _screenzx->DrawRange(0, frameT - 1);
    const auto off = FramebufferCopy(_screenzx);

    _screenzx->SetPlaneBEnabled(true);
    _screenzx->DrawRange(0, frameT - 1);
    EXPECT_EQ(FramebufferCopy(_screenzx), off) << "plane B changed the picture";
}

TEST_F(ScreenZX_Test, PlaneB_DescribesEveryDrawnPixel)
{
    _cpu->GetMemory()->DefaultBanksFor48k();
    _screenzx->InitFrame();
    _screenzx->SetPlaneBEnabled(true);
    Memory* memory = _cpu->GetMemory();
    FillScreenNoise(memory, 11);
    const uint32_t frameT = _screenzx->_rasterState.maxFrameTiming;

    // Draw in 16-T catch-ups; change the border every chunk and the attributes
    // once mid-frame (multicolor): plane B must follow what the beam used
    const uint32_t attrSwitchT = (frameT / 2) & ~15u;
    uint8_t border = 0;
    for (uint32_t t = 0; t < frameT; t += 16)
    {
        _screenzx->_borderColor = border;
        border = (border + 1) & 7;
        if (t == attrSwitchT)
            FillScreenNoise(memory, 99);
        _screenzx->DrawRange(t, std::min(t + 15, frameT - 1));
    }

    size_t count = 0;
    const uint16_t* planeB = _screenzx->GetPlaneB(&count);
    ASSERT_NE(planeB, nullptr);
    const auto fb = FramebufferCopy(_screenzx);
    ASSERT_EQ(count, fb.size());

    size_t screenPixels = 0;
    size_t borderPixels = 0;
    std::set<uint8_t> borderColors;
    for (size_t i = 0; i < count; i++)
    {
        const uint16_t v = planeB[i];
        const uint16_t role = v & Screen::kPlaneBRoleMask;
        const uint8_t attr = v & 0xFF;
        const uint8_t color = (v >> 8) & 0xF;
        if (role == Screen::kPlaneBRoleScreen)
        {
            screenPixels++;
            const bool ink = v & Screen::kPlaneBInk;
            const uint8_t bright = (attr & 0x40) ? 8 : 0;
            ASSERT_EQ(color, ink ? (attr & 7) + bright : ((attr >> 3) & 7) + bright) << "pixel " << i;
            ASSERT_EQ(fb[i], _screenzx->TransformZXSpectrumColorsToRGBA(attr, ink)) << "pixel " << i;
        }
        else if (role == Screen::kPlaneBRoleBorder)
        {
            borderPixels++;
            borderColors.insert(color);
            ASSERT_EQ(attr, 0);
            ASSERT_EQ(fb[i], _screenzx->TransformZXSpectrumColorsToRGBA(color, true)) << "pixel " << i;
        }
    }
    EXPECT_EQ(screenPixels, 256u * 192u) << "every paper pixel is described";
    EXPECT_GT(borderPixels, 0u);
    EXPECT_EQ(borderColors.size(), 8u) << "border changes within the frame are recorded";
}

/// endregion </ZX DLSS plane B>
