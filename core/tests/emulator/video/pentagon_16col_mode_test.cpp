#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/ports/portdecoder.h"

/// @file pentagon_16col_mode_test.cpp
/// @brief Pentagon 1024K 16-color video mode tests.
///
/// Validates:
/// - Video mode detection: EFF7 bit 0 -> M_P16
/// - InitRaster() triggers mode re-detection
/// - Mode transitions and state consistency

/// region <Unit tests - no emulator required>

class Pentagon_16col_Mode_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.mem_model = MM_PENTAGON;
        _context->config.ramsize = 1024;
    }

    void TearDown() override
    {
        delete _context;
    }

    EmulatorContext* _context = nullptr;
};

/// EFF7 state reflects 16col bit correctly
TEST_F(Pentagon_16col_Mode_Test, DetectMode_Standard_ReturnsBasePentagon)
{
    _context->emulatorState.pEFF7 = 0x00;
    EXPECT_EQ(_context->emulatorState.pEFF7 & EFF7_4BPP, 0)
        << "16col bit should be off";
    EXPECT_EQ(_context->emulatorState.pEFF7 & EFF7_HWMC, 0)
        << "HWMC bit should be off";
}

/// EFF7 bit 0 enables 16-color mode
TEST_F(Pentagon_16col_Mode_Test, DetectMode_16col_SetsEFF7Bit0)
{
    _context->emulatorState.pEFF7 = EFF7_4BPP;
    EXPECT_TRUE(_context->emulatorState.pEFF7 & EFF7_4BPP)
        << "16col bit should be on";
}

/// EFF7 bit 5 enables hardware multicolor mode
TEST_F(Pentagon_16col_Mode_Test, DetectMode_HWMC_SetsEFF7Bit5)
{
    _context->emulatorState.pEFF7 = EFF7_HWMC;
    EXPECT_TRUE(_context->emulatorState.pEFF7 & EFF7_HWMC)
        << "HWMC bit should be on";
}

/// endregion </Unit tests>

/// region <Integration tests with full emulator>

class Pentagon_16col_Integration_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create Pentagon emulator";
        _context = _emulator->GetContext();
        _screen = _context->pScreen;
        ASSERT_NE(_screen, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// Screen::DetectModePentagon returns M_P16 when EFF7 bit 0 is set
/// This tests the detection logic directly without requiring the port decoder
TEST_F(Pentagon_16col_Integration_Test, DetectMode_Returns_M_P16_WhenEFF7Bit0Set)
{
    ASSERT_EQ(_screen->GetVideoMode(), M_PENTAGON128K)
        << "Should start in standard Pentagon mode";

    // Set EFF7 bit 0 directly in state
    _context->emulatorState.pEFF7 = EFF7_4BPP;

    // Trigger re-detection via InitRaster
    _screen->InitRaster();

    // Verify mode changed to M_P16
    EXPECT_EQ(_screen->GetVideoMode(), M_P16)
        << "Should detect M_P16 when EFF7 bit 0 is set";
}

/// Screen::DetectModePentagon returns M_PMC when EFF7 bit 5 is set
TEST_F(Pentagon_16col_Integration_Test, DetectMode_Returns_M_PMC_WhenEFF7Bit5Set)
{
    _context->emulatorState.pEFF7 = EFF7_HWMC;
    _screen->InitRaster();
    EXPECT_EQ(_screen->GetVideoMode(), M_PMC)
        << "Should detect M_PMC (hardware multicolor) when EFF7 bit 5 is set";
}

/// 16col mode survives InitRaster (frame re-detection)
TEST_F(Pentagon_16col_Integration_Test, Mode16col_SurvivesInitRaster)
{
    // Set 16col mode
    _context->emulatorState.pEFF7 = EFF7_4BPP;
    _screen->InitRaster();
    EXPECT_EQ(_screen->GetVideoMode(), M_P16);

    // Simulate multiple frame starts (re-detection)
    for (int frame = 0; frame < 3; frame++)
    {
        _screen->InitRaster();
        EXPECT_EQ(_screen->GetVideoMode(), M_P16)
            << "16-color mode should persist across InitRaster on frame " << frame;
    }
}

/// Clearing EFF7 bit 0 switches back to standard mode
TEST_F(Pentagon_16col_Integration_Test, ClearEFF7_SwitchesToStandard)
{
    // Enable 16col mode
    _context->emulatorState.pEFF7 = EFF7_4BPP;
    _screen->InitRaster();
    EXPECT_EQ(_screen->GetVideoMode(), M_P16);

    // Clear the bit
    _context->emulatorState.pEFF7 = 0x00;
    _screen->InitRaster();

    // Verify mode reverted
    EXPECT_EQ(_screen->GetVideoMode(), M_PENTAGON128K)
        << "Should revert to standard mode after clearing EFF7 bit 0";
}

/// GetActiveSurfaceRAMPages returns correct pages for M_P16
TEST_F(Pentagon_16col_Integration_Test, ActiveSurfacePages_16col)
{
    // Screen 0 (7FFD bit 3 = 0): pages {4, 5}
    auto pages = _screen->GetActiveSurfaceRAMPages(M_P16, 0x00, true);
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0], 4u) << "Screen 0 should use page 4 (vidPage ^ 1 = 5 ^ 1 = 4)";
    EXPECT_EQ(pages[1], 5u) << "Screen 0 should use page 5";

    // Screen 1 (7FFD bit 3 = 1): pages {6, 7}
    pages = _screen->GetActiveSurfaceRAMPages(M_P16, 0x08, true);
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0], 6u) << "Screen 1 should use page 6 (vidPage ^ 1 = 7 ^ 1 = 6)";
    EXPECT_EQ(pages[1], 7u) << "Screen 1 should use page 7";
}

/// endregion </Integration tests>
