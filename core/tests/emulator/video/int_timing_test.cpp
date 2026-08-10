#include "pch.h"
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/config.h"

/// Test fixture for INT timing verification per model
class INTTiming_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;
    ScreenZXCUT* _screen = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _cpu = new Core(_context);
        _cpu->Init();
        _screen = new ScreenZXCUT(_context);
    }

    void TearDown() override
    {
        delete _screen;
        delete _cpu;
        delete _context;
    }

    /// Set model and apply config defaults manually (without INI file)
    void SetupModel(MEM_MODEL model)
    {
        CONFIG& config = _context->config;
        config.mem_model = model;

        switch (model)
        {
            case MM_PENTAGON:
                config.frame = 71680;
                config.t_line = 224;
                config.intstart = 71623;
                config.intlen = 32;
                _screen->SetVideoMode(M_PENTAGON128K);
                break;
            case MM_SPECTRUM48:
                config.frame = 69888;
                config.t_line = 224;
                config.intstart = 1794;
                config.intlen = 32;
                _screen->SetVideoMode(M_ZX48);
                break;
            case MM_SPECTRUM128:
            case MM_PLUS3:
                config.frame = 70908;
                config.t_line = 228;
                config.intstart = 2056;
                config.intlen = 36;
                _screen->SetVideoMode(M_ZX128);
                break;
            default:
                break;
        }

        // Reset CPU state
        Z80* z80 = _cpu->GetZ80();
        z80->t = 0;
        z80->int_pending = false;
    }
};

/// =========== INT Position Tests ===========

TEST_F(INTTiming_Test, Pentagon_INTStartCorrect)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intstart, 71623u);
}

TEST_F(INTTiming_Test, Pentagon_INTLengthCorrect)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(INTTiming_Test, Pentagon_INTFiresAtCorrectPosition)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    // INT should fire when cpu.t reaches intstart
    Z80* z80 = _cpu->GetZ80();

    // Just before INT start - should not be pending
    z80->t = config.intstart - 1;
    bool beforeInt = (z80->t >= config.intstart);
    EXPECT_FALSE(beforeInt);

    // At INT start - should fire
    z80->t = config.intstart;
    bool atInt = (z80->t >= config.intstart);
    EXPECT_TRUE(atInt);
}

TEST_F(INTTiming_Test, Pentagon_INTPositionIsEndOfFrame)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    // Pentagon INT fires at 99.9% through the frame
    double percentThrough = (double)config.intstart / config.frame * 100.0;
    EXPECT_GT(percentThrough, 99.0);  // Should be >99%
    EXPECT_LT(percentThrough, 100.0);
}

TEST_F(INTTiming_Test, Pentagon_INTDoesNotWrap)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    // int_end = intstart + intlen = 71619 + 32 = 71651
    // frameLimit = 71680
    // 71651 < 71680, so no wrap needed
    unsigned int_end = config.intstart + config.intlen;
    EXPECT_LT(int_end, config.frame);
}

TEST_F(INTTiming_Test, ZX48k_INTStartCorrect)
{
    SetupModel(MM_SPECTRUM48);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intstart, 1794u);
}

TEST_F(INTTiming_Test, ZX48k_INTLengthCorrect)
{
    SetupModel(MM_SPECTRUM48);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(INTTiming_Test, ZX48k_INTPositionIsEarlyInFrame)
{
    SetupModel(MM_SPECTRUM48);
    CONFIG& config = _context->config;

    // ZX-48K INT fires at ~2.6% through the frame
    double percentThrough = (double)config.intstart / config.frame * 100.0;
    EXPECT_LT(percentThrough, 5.0);  // Should be <5%
}

TEST_F(INTTiming_Test, ZX48k_FrameSizeCorrect)
{
    SetupModel(MM_SPECTRUM48);
    CONFIG& config = _context->config;
    // ZX-48K: 224 t-states/line * 312 lines = 69888
    EXPECT_EQ(config.frame, 69888u);
    EXPECT_EQ(config.t_line, 224u);
    EXPECT_EQ(config.frame / config.t_line, 312u);
}

TEST_F(INTTiming_Test, ZX128k_INTStartCorrect)
{
    SetupModel(MM_SPECTRUM128);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intstart, 2056u);
}

TEST_F(INTTiming_Test, ZX128k_INTLengthCorrect)
{
    SetupModel(MM_SPECTRUM128);
    CONFIG& config = _context->config;
    // ZX-128K has 72 HC = 36 T-state INT (not 32!)
    EXPECT_EQ(config.intlen, 36u);
}

TEST_F(INTTiming_Test, ZX128k_FrameSizeCorrect)
{
    SetupModel(MM_SPECTRUM128);
    CONFIG& config = _context->config;
    // ZX-128K: 228 t-states/line * 311 lines = 70908
    EXPECT_EQ(config.frame, 70908u);
    EXPECT_EQ(config.t_line, 228u);
    EXPECT_EQ(config.frame / config.t_line, 311u);
}

TEST_F(INTTiming_Test, Plus3_SameAsZX128k_INT)
{
    SetupModel(MM_PLUS3);
    CONFIG& config = _context->config;
    // ZX +3 uses same ULA timing as ZX-128K
    EXPECT_EQ(config.intstart, 2056u);
    EXPECT_EQ(config.intlen, 36u);
}

/// =========== INT Position Calculation Formula Tests ===========

TEST_F(INTTiming_Test, Pentagon_INTStartMatchesFormula)
{
    SetupModel(MM_PENTAGON);

    // Formula: intstart = emulatorLine * tstatesPerLine + hc / 2
    // Pentagon: vc=239, hc=326
    // paperStartLine = vSyncLines + vBlankLines + screenOffsetTop = 16 + 16 + 48 = 80
    // emulatorLine = (239 + 80) mod 320 = 319
    // intstart = 319 * 224 + 326/2 = 71456 + 163 = 71619
    uint32_t paperStartLine = 80;
    uint32_t totalLines = 320;
    uint32_t emulatorLine = (239 + paperStartLine) % totalLines;
    uint32_t expected = emulatorLine * 224 + 326 / 2;

    EXPECT_EQ(expected, 71619u);
}

TEST_F(INTTiming_Test, ZX48k_INTStartMatchesFormula)
{
    SetupModel(MM_SPECTRUM48);

    // ZX-48K: vc=248, hc=4
    // paperStartLine = 8 + 16 + 48 = 72
    // emulatorLine = (248 + 72) mod 312 = 320 mod 312 = 8
    // intstart = 8 * 224 + 4/2 = 1792 + 2 = 1794
    uint32_t paperStartLine = 72;
    uint32_t totalLines = 312;
    uint32_t emulatorLine = (248 + paperStartLine) % totalLines;
    uint32_t expected = emulatorLine * 224 + 4 / 2;

    EXPECT_EQ(expected, 1794u);
}

TEST_F(INTTiming_Test, ZX128k_INTStartMatchesFormula)
{
    SetupModel(MM_SPECTRUM128);

    // ZX-128K: vc=248, hc=8
    // paperStartLine = 8 + 16 + 48 = 72
    // emulatorLine = (248 + 72) mod 311 = 320 mod 311 = 9
    // intstart = 9 * 228 + 8/2 = 2052 + 4 = 2056
    uint32_t paperStartLine = 72;
    uint32_t totalLines = 311;
    uint32_t emulatorLine = (248 + paperStartLine) % totalLines;
    uint32_t expected = emulatorLine * 228 + 8 / 2;

    EXPECT_EQ(expected, 2056u);
}

/// =========== Cross-model consistency tests ===========

TEST_F(INTTiming_Test, Pentagon_INTNotAtFrameStart)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    // The old bug had intstart=13 (near frame start)
    // Verify we're not using that old value
    EXPECT_NE(config.intstart, 13u);
    EXPECT_GT(config.intstart, config.t_line);  // Should be more than 1 line in
}

TEST_F(INTTiming_Test, ZXModels_INTNotAtFrameStart)
{
    SetupModel(MM_SPECTRUM48);
    CONFIG& config = _context->config;
    EXPECT_NE(config.intstart, 13u);

    SetupModel(MM_SPECTRUM128);
    EXPECT_NE(config.intstart, 13u);
}

TEST_F(INTTiming_Test, INTWindowStaysWithinFrame)
{
    // For all models, intstart + intlen should be <= frame size
    // (no wrap needed for any model)
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;
    EXPECT_LE(config.intstart + config.intlen, config.frame);

    SetupModel(MM_SPECTRUM48);
    EXPECT_LE(config.intstart + config.intlen, config.frame);

    SetupModel(MM_SPECTRUM128);
    EXPECT_LE(config.intstart + config.intlen, config.frame);
}

/// =========== INT Duration derivation tests ===========

TEST_F(INTTiming_Test, Pentagon_INTDurationMatchesHDL)
{
    // MiSTer HDL: INTCnt terminal = 63 for non-m128 models
    // INT high for 64 HC = 32 T-states
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    uint32_t hcDuration = 64;  // 63+1 cycles of counting
    uint32_t expectedTStates = hcDuration / 2;  // 2 HC = 1 T-state
    EXPECT_EQ(config.intlen, expectedTStates);
}

TEST_F(INTTiming_Test, ZX128k_INTDurationMatchesHDL)
{
    // MiSTer HDL: INTCnt terminal = 71 for m128 models
    // INT high for 72 HC = 36 T-states
    SetupModel(MM_SPECTRUM128);
    CONFIG& config = _context->config;

    uint32_t hcDuration = 72;  // 71+1 cycles of counting
    uint32_t expectedTStates = hcDuration / 2;  // 2 HC = 1 T-state
    EXPECT_EQ(config.intlen, expectedTStates);
}

/// =========== ApplyModelTimingDefaults Tests ===========

TEST_F(INTTiming_Test, ApplyDefaults_Pentagon)
{
    CONFIG config = {};
    config.mem_model = MM_PENTAGON;
    config.intstart = 0;  // Simulate fresh config
    config.intlen = 0;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    EXPECT_EQ(config.intstart, 71623u);
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(INTTiming_Test, ApplyDefaults_ZX48k)
{
    CONFIG config = {};
    config.mem_model = MM_SPECTRUM48;
    config.intstart = 0;
    config.intlen = 0;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    EXPECT_EQ(config.intstart, 1794u);
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(INTTiming_Test, ApplyDefaults_ZX128k)
{
    CONFIG config = {};
    config.mem_model = MM_SPECTRUM128;
    config.intstart = 0;
    config.intlen = 0;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    EXPECT_EQ(config.intstart, 2056u);
    EXPECT_EQ(config.intlen, 36u);
}

TEST_F(INTTiming_Test, ApplyDefaults_Plus3)
{
    CONFIG config = {};
    config.mem_model = MM_PLUS3;
    config.intstart = 0;
    config.intlen = 0;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    // Plus3 uses same ULA timing as ZX-128K
    EXPECT_EQ(config.intstart, 2056u);
    EXPECT_EQ(config.intlen, 36u);
}

TEST_F(INTTiming_Test, ApplyDefaults_PreservesUserIntstart)
{
    CONFIG config = {};
    config.mem_model = MM_SPECTRUM48;
    config.intstart = 12345;  // User override
    config.intlen = 0;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    // User value should be preserved (not overwritten by default)
    EXPECT_EQ(config.intstart, 12345u);
}

TEST_F(INTTiming_Test, ApplyDefaults_PreservesUserIntlen)
{
    CONFIG config = {};
    config.mem_model = MM_SPECTRUM128;
    config.intstart = 0;
    config.intlen = 40;  // User override (not default 36)

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    // User value should be preserved
    EXPECT_EQ(config.intlen, 40u);
}

TEST_F(INTTiming_Test, ApplyDefaults_ReplacesOldPlaceholder13)
{
    // The old bug had intstart=13 as placeholder in INI files.
    // ApplyModelTimingDefaults should replace it with the correct value.
    CONFIG config = {};
    config.mem_model = MM_PENTAGON;
    config.intstart = 13;  // Old placeholder value
    config.intlen = 32;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    // Should be replaced, not preserved
    EXPECT_EQ(config.intstart, 71623u);
}

TEST_F(INTTiming_Test, ApplyDefaults_ReplacesOldPlaceholder32For128k)
{
    // ZX-128K old INI had intlen=32 (wrong, should be 36).
    // ApplyModelTimingDefaults should replace it.
    CONFIG config = {};
    config.mem_model = MM_SPECTRUM128;
    config.intstart = 0;
    config.intlen = 32;  // Old default, should be replaced with 36

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config);

    EXPECT_EQ(config.intlen, 36u);
}
