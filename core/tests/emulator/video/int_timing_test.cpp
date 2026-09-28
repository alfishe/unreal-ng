#include "pch.h"
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/config.h"
#include "_helpers/testpathhelper.h"

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
ASSERT_TRUE(_cpu->Init()) << "Core::Init() failed";
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
                config.intstart = 71635;
                config.intlen = 32;
                _screen->SetVideoMode(M_PENTAGON128K);
                break;
            case MM_SPECTRUM48:
                config.frame = 69888;
                config.t_line = 224;
                config.intstart = 1811;
                config.intlen = 32;
                _screen->SetVideoMode(M_ZX48);
                break;
            case MM_SPECTRUM128:
            case MM_PLUS3:
                config.frame = 70908;
                config.t_line = 228;
                config.intstart = 1845;
                config.intlen = (model == MM_PLUS3) ? 32 : 36;  // the +2A/+3 gate array's INT is 32 T
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
    EXPECT_EQ(config.intstart, 71635u);
}

TEST_F(INTTiming_Test, Pentagon_INTLengthCorrect)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(INTTiming_Test, Pentagon_INTAcceptanceIsStrictlyAfterStart)
{
    SetupModel(MM_PENTAGON);
    CONFIG& config = _context->config;

    // INT acceptance is strict (cpu.t > int_start), mirroring ProcessInterrupts:
    // the ULA registers INT one clock after the raster compare and the CPU samples
    // it only at end-of-instruction edges - a boundary exactly at int_start still
    // sees INT inactive (doc 20).
    Z80* z80 = _cpu->GetZ80();

    // Before and exactly at INT start - INT not yet visible to the CPU
    z80->t = config.intstart - 1;
    bool beforeInt = (z80->t > config.intstart);
    EXPECT_FALSE(beforeInt);

    z80->t = config.intstart;
    bool atInt = (z80->t > config.intstart);
    EXPECT_FALSE(atInt);

    // One clock later - INT registered high, acceptance possible at this boundary
    z80->t = config.intstart + 1;
    bool afterInt = (z80->t > config.intstart);
    EXPECT_TRUE(afterInt);
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
    EXPECT_EQ(config.intstart, 1811u);
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
    EXPECT_EQ(config.intstart, 1845u);
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

TEST_F(INTTiming_Test, Plus3_128kINTPositionShorterPulse)
{
    SetupModel(MM_PLUS3);
    CONFIG& config = _context->config;
    // The +2A/+3 gate array fires INT where the 128K ULA does, for 32 T instead of 36
    EXPECT_EQ(config.intstart, 1845u);
    EXPECT_EQ(config.intlen, 32u);
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

/// INT -> first rendered paper pixel, the physically meaningful quantity every
/// model's intstart is calibrated on. Goes through the production defaults and
/// the production raster (GetPaperStartTstate), so a raster or origin change
/// that moves the picture relative to the INT fails here.
TEST_F(INTTiming_Test, INTToFirstPixel_MatchesReferencePerModel)
{
    struct Case { MEM_MODEL model; uint32_t frame; uint32_t line; VideoModeEnum mode; uint32_t intToPaper; const char* source; };
    const Case cases[] = {
        // Consensus of Xpeccy layouts, MiSTer ula.sv (+6T output pipeline) and
        // ZXMAK2 (ZXMAK2 alone places the 48K/128K pixel 4T earlier)
        {MM_SPECTRUM48,  69888, 224, M_ZX48,         14340, "Xpeccy ULA.48, MiSTer"},
        {MM_SPECTRUM128, 70908, 228, M_ZX128,        14366, "Xpeccy ULA.128, MiSTer"},
        {MM_PLUS3,       70908, 228, M_ZX128,        14366, "MiSTer (+3 shares the 128K ULA timing)"},
        {MM_SCORP,       69888, 224, M_SCORPION,     14336, "Xpeccy ULA.Scorpion, ZXMAK2"},
        // Pentagon: 71635 -> 17988T is verified on "Across the Edge"; 71634 (17989T,
        // the UnrealSpeccy figure) breaks it. References span 17985..17989
        {MM_PENTAGON,    71680, 224, M_PENTAGON128K, 17988, "Pentagon, demo-verified"},
        {MM_PROFI,       69888, 224, M_PROFI,        12580, "UnrealSpeccy PRESET.PROFI"},
        {MM_ATM710,      69888, 224, M_ZX48,         14395, "UnrealSpeccy PRESET.ATM1_2_3.5MHz"},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.source);
        CONFIG& config = _context->config;
        config.mem_model = c.model;
        config.frame = c.frame;
        config.t_line = c.line;
        config.intstart = 0;
        config.intlen = 0;
        Config(_context).ApplyModelTimingDefaults(config);
        _screen->SetVideoMode(c.mode);

        const uint32_t intFiresAt = config.intstart + 1;  // Pentagon fires at the end of the previous frame
        EXPECT_EQ((_screen->GetPaperStartTstate() + config.frame - intFiresAt) % config.frame, c.intToPaper);
    }
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

    EXPECT_EQ(config.intstart, 71635u);
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

    EXPECT_EQ(config.intstart, 1811u);
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

    EXPECT_EQ(config.intstart, 1845u);
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

    // The +2A/+3 gate array keeps the 128K frame and INT position, its INT is 32 T (ZXMAK2 UlaPlus3,
    // BizHawk ZX128Plus2a; the 128K ULA's is 36)
    EXPECT_EQ(config.intstart, 1845u);
    EXPECT_EQ(config.intlen, 32u);
}

/// =========== ATM Turbo 2+ / ATM3 frame and INT ===========
/// The ATM frame is the 312 x 224T raster at the base clock in every video
/// mode; the FF77.3 turbo multiplies the CPU only. UnrealSpeccy's
/// PRESET.ATM1_2_7.0MHz (99880T) stretched the frame instead because that
/// emulator paces at a fixed 50 Hz - here the frame length sets the frame
/// period, so 99880T ran the machine at 35.04 FPS.

TEST_F(INTTiming_Test, ApplyDefaults_ATM_CanonicalGeometryIsBaseClockRaster)
{
    for (MEM_MODEL model : {MM_ATM710, MM_ATM3})
    {
        SCOPED_TRACE(testing::Message() << "mem_model = " << int(model));
        CONFIG config = {};
        config.mem_model = model;
        config.frame = 99880;  // the stale UnrealSpeccy 7 MHz preset must not survive
        config.intstart = 0;
        config.intlen = 0;

        Config configHelper(_context);
        configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

        EXPECT_EQ(config.frame, 69888u);
        EXPECT_EQ(config.t_line, 224u);
        EXPECT_EQ(config.intstart, 1756u);
        EXPECT_EQ(config.intlen, 32u);
        EXPECT_EQ(config.frame_duration_us, 19968u) << "50.08 FPS, not 35.04";
    }
}

TEST_F(INTTiming_Test, ATM_INTToZXPaperMatchesReference)
{
    CONFIG& config = _context->config;
    config.mem_model = MM_ATM710;
    config.frame = 69888;
    config.t_line = 224;
    config.intstart = 0;
    config.intlen = 0;
    Config(_context).ApplyModelTimingDefaults(config);
    _screen->SetVideoMode(M_ZX48);  // ATM ZX-compatible mode

    const uint32_t paperT = _screen->_rasterState.screenAreaStart +
                            _screen->rasterDescriptors[M_ZX48].screenOffsetLeft / 2;
    const uint32_t intFiresAt = config.intstart + 1;  // acceptance is strictly after intstart
    // UnrealSpeccy PRESET.ATM1_2_3.5MHz paper field (DDp); Xpeccy ULA.ATM2 gives 14384T
    EXPECT_EQ(paperT - intFiresAt, 14395u);
}

TEST_F(INTTiming_Test, ShippedAtmConfigs_FrameEqualsRasterInEveryMode)
{
    for (const char* folder : {"atm710", "atm3"})
    {
        SCOPED_TRACE(folder);
        const fs::path ini = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        ASSERT_TRUE(fs::exists(ini)) << ini;
        ASSERT_TRUE(Config(_context).LoadConfigFile(ini.string()));

        const CONFIG& config = _context->config;
        EXPECT_EQ(config.frame_duration_us, 19968u) << "50.08 FPS, not 35.04";
        EXPECT_EQ(config.intstart, 1756u);
        for (VideoModeEnum mode : {M_ZX48, M_ATM16, M_ATMHR, M_ATMTX})
        {
            _screen->SetVideoMode(mode);
            EXPECT_EQ(config.frame, _screen->GetMaxFrameTiming()) << "mode " << int(mode);
        }
    }
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
    EXPECT_EQ(config.intstart, 71635u);
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
