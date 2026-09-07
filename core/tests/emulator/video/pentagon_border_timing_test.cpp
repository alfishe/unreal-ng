#include "pch.h"
#include "stdafx.h"

#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"

/// Pentagon Border Timing Tests
/// Verifies the 1T border delay compensation for Pentagon-class ULAs.
/// See: docs/timing/pentagon-border-timing.md
///
/// INT timing is quantized to 4T due to HALT instruction, but border effects
/// in demos like "Across The Edge" need 1T (2 pixel) precision. SetBorderColor
/// renders one additional T-state with the OLD color before applying the new one.

class PentagonBorderTiming_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    ScreenZXCUT* _screen = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _core = new Core(_context);
        (void)_core->Init();
        _screen = new ScreenZXCUT(_context);
    }

    void TearDown() override
    {
        delete _screen;
        delete _core;
        delete _context;
        _screen = nullptr;
        _core = nullptr;
        _context = nullptr;
    }

    void SetupPentagonMode(VideoModeEnum mode)
    {
        _screen->SetVideoMode(mode);
        _screen->CreateTimingTable();
        _screen->Reset();
    }

    uint32_t SimulateBorderChange(uint32_t atTstate, uint8_t newColor)
    {
        Z80* z80 = _core->GetZ80();
        z80->t = atTstate;

        uint32_t prevTstateBefore = _screen->_prevTstate;
        _screen->SetBorderColor(newColor);
        uint32_t prevTstateAfter = _screen->_prevTstate;

        return prevTstateAfter - prevTstateBefore;
    }
};

/// Test that Pentagon 128K mode adds 1T delay in SetBorderColor
TEST_F(PentagonBorderTiming_Test, Pentagon128K_BorderDelay_1T)
{
    SetupPentagonMode(M_PENTAGON128K);

    // Simulate border change at mid-frame
    uint32_t testTstate = 35000;
    Z80* z80 = _core->GetZ80();
    z80->t = testTstate;

    // First update to establish _prevTstate
    _screen->UpdateScreen();
    uint32_t baselinePrevTstate = _screen->_prevTstate;
    EXPECT_EQ(baselinePrevTstate, testTstate) << "UpdateScreen should set _prevTstate to current t-state";

    // Now change border color - should add 1T delay
    z80->t = testTstate;  // Reset to same t-state
    _screen->_prevTstate = testTstate;
    _screen->SetBorderColor(0x02);  // Red border

    EXPECT_EQ(_screen->_prevTstate, testTstate + 1)
        << "Pentagon SetBorderColor should advance _prevTstate by 1T";
}

/// Test that all Pentagon-class video modes get the 1T delay
TEST_F(PentagonBorderTiming_Test, AllPentagonModes_BorderDelay_1T)
{
    const std::vector<std::pair<VideoModeEnum, const char*>> pentagonModes = {
        {M_PENTAGON128K, "M_PENTAGON128K"},
        {M_PMC, "M_PMC"},
        {M_P16, "M_P16"},
        {M_P384, "M_P384"},
        {M_PHR, "M_PHR"}
    };

    for (const auto& [mode, modeName] : pentagonModes)
    {
        SetupPentagonMode(mode);

        uint32_t testTstate = 40000;
        Z80* z80 = _core->GetZ80();
        z80->t = testTstate;
        _screen->_prevTstate = testTstate;

        _screen->SetBorderColor(0x04);  // Green border

        EXPECT_EQ(_screen->_prevTstate, testTstate + 1)
            << "Mode " << modeName << " should add 1T border delay";
    }
}

/// Test that non-Pentagon modes do NOT get the 1T delay
TEST_F(PentagonBorderTiming_Test, NonPentagonModes_NoBorderDelay)
{
    const std::vector<std::pair<VideoModeEnum, const char*>> nonPentagonModes = {
        {M_ZX48, "M_ZX48"},
        {M_ZX128, "M_ZX128"}
    };

    for (const auto& [mode, modeName] : nonPentagonModes)
    {
        _screen->SetVideoMode(mode);
        _screen->CreateTimingTable();
        _screen->Reset();

        uint32_t testTstate = 35000;
        Z80* z80 = _core->GetZ80();
        z80->t = testTstate;
        _screen->_prevTstate = testTstate;

        _screen->SetBorderColor(0x01);  // Blue border

        EXPECT_EQ(_screen->_prevTstate, testTstate)
            << "Mode " << modeName << " should NOT add border delay";
    }
}

/// Test border delay at frame boundaries
TEST_F(PentagonBorderTiming_Test, Pentagon_BorderDelay_NearFrameEnd)
{
    SetupPentagonMode(M_PENTAGON128K);

    // Test near frame end (71680 T-states per Pentagon frame)
    uint32_t nearEnd = 71678;
    Z80* z80 = _core->GetZ80();
    z80->t = nearEnd;
    _screen->_prevTstate = nearEnd;

    _screen->SetBorderColor(0x06);  // Yellow border

    // Should still add 1T since 71679 < 71680
    EXPECT_EQ(_screen->_prevTstate, nearEnd + 1)
        << "Border delay should work near frame end";
}

/// Test border delay does not exceed frame boundary
TEST_F(PentagonBorderTiming_Test, Pentagon_BorderDelay_AtFrameEnd)
{
    SetupPentagonMode(M_PENTAGON128K);

    // Test at exact frame end
    uint32_t frameEnd = _screen->_rasterState.maxFrameTiming - 1;
    Z80* z80 = _core->GetZ80();
    z80->t = frameEnd;
    _screen->_prevTstate = frameEnd;

    _screen->SetBorderColor(0x05);  // Cyan border

    // Should NOT add delay as it would exceed frame
    EXPECT_EQ(_screen->_prevTstate, frameEnd)
        << "Border delay should not exceed frame boundary";
}

/// Test multiple consecutive border changes
TEST_F(PentagonBorderTiming_Test, Pentagon_ConsecutiveBorderChanges)
{
    SetupPentagonMode(M_PENTAGON128K);

    Z80* z80 = _core->GetZ80();
    uint32_t startTstate = 30000;

    // First border change
    z80->t = startTstate;
    _screen->_prevTstate = startTstate;
    _screen->SetBorderColor(0x01);
    EXPECT_EQ(_screen->_prevTstate, startTstate + 1);

    // Second border change at T+10
    z80->t = startTstate + 10;
    _screen->SetBorderColor(0x02);
    // Should render T+2 to T+10, then add 1T delay
    EXPECT_EQ(_screen->_prevTstate, startTstate + 11);

    // Third border change at T+20
    z80->t = startTstate + 20;
    _screen->SetBorderColor(0x03);
    EXPECT_EQ(_screen->_prevTstate, startTstate + 21);
}

/// Test border timing with Pentagon 128K memory configuration
TEST_F(PentagonBorderTiming_Test, Pentagon128K_MemoryConfig)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.ramsize = 128;

    SetupPentagonMode(M_PENTAGON128K);

    uint32_t testTstate = 45000;
    Z80* z80 = _core->GetZ80();
    z80->t = testTstate;
    _screen->_prevTstate = testTstate;

    _screen->SetBorderColor(0x07);

    EXPECT_EQ(_screen->_prevTstate, testTstate + 1)
        << "Pentagon 128K config should have 1T border delay";
}

/// Test border timing with Pentagon 512K memory configuration
TEST_F(PentagonBorderTiming_Test, Pentagon512K_MemoryConfig)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.ramsize = 512;

    SetupPentagonMode(M_PENTAGON128K);

    uint32_t testTstate = 45000;
    Z80* z80 = _core->GetZ80();
    z80->t = testTstate;
    _screen->_prevTstate = testTstate;

    _screen->SetBorderColor(0x07);

    EXPECT_EQ(_screen->_prevTstate, testTstate + 1)
        << "Pentagon 512K config should have 1T border delay";
}

/// Test border timing with Pentagon 1024K memory configuration
TEST_F(PentagonBorderTiming_Test, Pentagon1024K_MemoryConfig)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.ramsize = 1024;

    SetupPentagonMode(M_PENTAGON128K);

    uint32_t testTstate = 45000;
    Z80* z80 = _core->GetZ80();
    z80->t = testTstate;
    _screen->_prevTstate = testTstate;

    _screen->SetBorderColor(0x07);

    EXPECT_EQ(_screen->_prevTstate, testTstate + 1)
        << "Pentagon 1024K config should have 1T border delay";
}

/// Test that border color is correctly masked to 3 bits
TEST_F(PentagonBorderTiming_Test, BorderColor_MaskedTo3Bits)
{
    SetupPentagonMode(M_PENTAGON128K);

    Z80* z80 = _core->GetZ80();
    z80->t = 50000;
    _screen->_prevTstate = 50000;

    // Set border with high bits set (should be masked)
    _screen->SetBorderColor(0xFF);  // All bits set

    EXPECT_EQ(_screen->GetBorderColor(), 0x07)
        << "Border color should be masked to 3 bits (0-7)";
}

/// Verify Pentagon frame timing constants
TEST_F(PentagonBorderTiming_Test, Pentagon_FrameTimingConstants)
{
    // From docs/timing/pentagon-border-timing.md:
    // Pentagon frame = 71680 T-states (224 T/line * 320 lines)
    // Paper starts at line 80, pixel 48 = T 17944
    // INT should fire at ~71635-71636 for 17989T INT-to-paper distance

    SetupPentagonMode(M_PENTAGON128K);

    // Verify frame duration
    EXPECT_EQ(_screen->_rasterState.maxFrameTiming, 71680u)
        << "Pentagon frame should be 71680 T-states";

    // Verify T-states per line
    EXPECT_EQ(_screen->_rasterState.tstatesPerLine, 224u)
        << "Pentagon should have 224 T-states per line";

    // Verify pixels per T-state (Pentagon renders 2 pixels per T-state)
    EXPECT_EQ(_screen->_rasterState.pixelsPerTState, 2u)
        << "Pentagon should render 2 pixels per T-state";
}
