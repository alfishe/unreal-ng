#include "pch.h"
#include "stdafx.h"

#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "emulator/video/zx/screenzx.h"

/// Scorpion ZS-256 machine timing and raster profile pins (hardware-reference 6):
/// 312 lines x 224T = 69888T Sinclair-matching frame, INT at the ZX48 position
/// (intstart 1794, intlen 32), discrete-logic fetch, contention disabled and
/// 1T border updates. MM_SCORP and MM_PROFSCORP share the profile.
class ScorpionRaster_Test : public ::testing::Test
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
        _screen = nullptr;
        _cpu = nullptr;
        _context = nullptr;
    }

    /// Configure a Scorpion model and drive InitFrame -> InitRaster - the same
    /// per-frame detection path the main loop uses
    void SetupScorpion(MEM_MODEL model)
    {
        CONFIG& config = _context->config;
        config.mem_model = model;
        config.frame = 69888;
        config.t_line = 224;
        config.intstart = 1794;
        config.intlen = 32;

        _screen->InitFrame();
    }
};

/// region <Timing defaults>

/// @brief ApplyModelTimingDefaults must force the Scorpion geometry even when the
///        active INI describes another machine (here: Pentagon values leaked in).
TEST_F(ScorpionRaster_Test, TimingDefaultsScorpion)
{
    CONFIG config = {};
    config.mem_model = MM_SCORP;
    config.frame = 71680;      // Pentagon geometry leaked from a foreign INI
    config.t_line = 224;
    config.intstart = 71635;
    config.intlen = 32;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

    EXPECT_EQ(config.frame, 69888u);   // 224 * 312
    EXPECT_EQ(config.t_line, 224u);
    EXPECT_EQ(config.intstart, 1794u);
    EXPECT_EQ(config.intlen, 32u);
}

TEST_F(ScorpionRaster_Test, TimingDefaultsProfScorpion)
{
    CONFIG config = {};
    config.mem_model = MM_PROFSCORP;
    config.frame = 70908;      // ZX-128K geometry leaked from a foreign INI
    config.t_line = 228;
    config.intstart = 2056;
    config.intlen = 36;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

    EXPECT_EQ(config.frame, 69888u);
    EXPECT_EQ(config.t_line, 224u);
    EXPECT_EQ(config.intstart, 1794u);
    EXPECT_EQ(config.intlen, 32u);
}

/// @brief The INT window must fit inside the frame without wrapping
TEST_F(ScorpionRaster_Test, TimingWindowWithinFrame)
{
    CONFIG config = {};
    config.mem_model = MM_SCORP;

    Config configHelper(_context);
    configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

    EXPECT_LE(config.intstart + config.intlen, config.frame);
    EXPECT_EQ(config.frame / config.t_line, 312u) << "312 lines of 224T";
}

/// endregion </Timing defaults>

/// region <Mode mapping>

TEST_F(ScorpionRaster_Test, InitRasterMapsScorpionToScorpionMode)
{
    SetupScorpion(MM_SCORP);
    EXPECT_EQ(_screen->GetVideoMode(), M_SCORPION);
}

TEST_F(ScorpionRaster_Test, InitRasterMapsProfScorpionToScorpionMode)
{
    SetupScorpion(MM_PROFSCORP);
    EXPECT_EQ(_screen->GetVideoMode(), M_SCORPION);
}

/// endregion </Mode mapping>

/// region <Raster profile>

/// @brief The M_SCORPION descriptor is the 312-line ZX48-class geometry
TEST_F(ScorpionRaster_Test, RasterDescriptorThreeTwelveLineGeometry)
{
    const RasterDescriptor& rd = _screen->rasterDescriptors[M_SCORPION];

    EXPECT_EQ(rd.fullFrameWidth, 352u);
    EXPECT_EQ(rd.fullFrameHeight, 288u);
    EXPECT_EQ(rd.screenWidth, 256u);
    EXPECT_EQ(rd.screenHeight, 192u);
    EXPECT_EQ(rd.screenOffsetLeft, 48u);
    EXPECT_EQ(rd.screenOffsetTop, 48u);
    EXPECT_EQ(rd.pixelsPerLine, 448u);   // 224T per line
    EXPECT_EQ(rd.vSyncLines + rd.vBlankLines + rd.fullFrameHeight, 312u);
}

/// @brief Discrete-logic ULA: 1T border updates, contention off, 4T continuous fetch
TEST_F(ScorpionRaster_Test, RasterStateScorpionUlaProfile)
{
    SetupScorpion(MM_SCORP);

    const RasterState& state = _screen->_rasterState;
    EXPECT_EQ(state.tstatesPerLine, 224u);
    EXPECT_EQ(state.maxFrameTiming, 69888u);   // 224 * (8 + 16 + 288)
    EXPECT_EQ(state.borderUpdateTStates, 1);
    EXPECT_FALSE(state.contentionEnabled);
    EXPECT_EQ(static_cast<UlaFetchType>(state.fetchType), ULA_DISCRETE_LOGIC);
}

/// @brief The standalone contention component receives the same profile
TEST_F(ScorpionRaster_Test, UlaContentionComponentReceivesScorpionProfile)
{
    SetupScorpion(MM_SCORP);

    UlaContention* ula = _context->pUlaContention;
    ASSERT_NE(ula, nullptr);
    EXPECT_FALSE(ula->IsContentionEnabled());
    EXPECT_EQ(ula->GetFetchType(), ULA_DISCRETE_LOGIC);
    EXPECT_EQ(ula->GetRaster().tstatesPerLine, 224u);
}

/// endregion </Raster profile>

/// region <Mode name table>

/// @brief Appending M_SCORPION must not shift any pre-existing mode name: the
///        whole positional table is pinned here (rasterDescriptors and the
///        static_assert on the name array guard the rest).
TEST_F(ScorpionRaster_Test, VideoModeNamesAppendWithoutShift)
{
    static const char* const expected[] = {
        "Null",                 // M_NUL
        "ZX-Spectrum 48k",      // M_ZX48
        "ZX-Spectrum 128k",     // M_ZX128
        "Pentagon 128k",        // M_PENTAGON128K
        "Pentagon multicolor",  // M_PMC
        "Pentagon 16c",         // M_P16
        "Pentagon 384x384",     // M_P384
        "Pentagon HiRes",       // M_PHR
        "Timex ULA+",           // M_TIMEX
        "TSConf 16c",           // M_TS16
        "TSConf 256c",          // M_TS256
        "TSConf Text",          // M_TSTX
        "ATM 16c",              // M_ATM16
        "ATM HiRes",            // M_ATMHR
        "ATM Text",             // M_ATMTX
        "ATM Text Linear",      // M_ATMTL
        "Profi",                // M_PROFI
        "GMX",                  // M_GMX
        "Border only",          // M_BRD
        "Scorpion 256k",        // M_SCORPION
    };
    static_assert(sizeof(expected) / sizeof(expected[0]) == M_MAX, "expected name table must cover every mode");

    for (int mode = M_NUL; mode < M_MAX; mode++)
    {
        EXPECT_EQ(Screen::GetVideoVideoModeName(static_cast<VideoModeEnum>(mode)), expected[mode])
            << "video mode name shifted at mode " << mode;
    }
}

/// endregion </Mode name table>
