#include "pch.h"
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/atm/screenatm.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "emulator/video/zx/screenzx.h"

/// Screen raster geometry as the debug surfaces see it: horizontal zones in the
/// renderer's line origin (T 0 = first left-border T, blanking closes the
/// line), the mode's own display window and pixel clock, beam descriptions, and
/// the per-machine ULA profile.
class Screen_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;
    ScreenZXCUT* _screen = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _cpu = new Core(_context);
        ASSERT_TRUE(_cpu->Init());
        _screen = new ScreenZXCUT(_context);
    }

    void TearDown() override
    {
        delete _screen;
        delete _cpu;
        delete _context;
    }

    void Use(MEM_MODEL model, VideoModeEnum mode, uint32_t frame = 69888)
    {
        _context->config.mem_model = model;
        _context->config.frame = frame;
        _screen->SetVideoMode(mode);
    }

    static uint32_t T(uint32_t line, uint32_t tInLine) { return line * 224 + tInLine; }
};

TEST_F(Screen_Test, HorizontalZones_UseRendererLineOrigin)
{
    Use(MM_SPECTRUM48, M_ZX48);
    const RasterState& rs = _screen->GetRasterState();
    EXPECT_EQ(rs.leftBorderAreaStart, 0);
    EXPECT_EQ(rs.leftBorderAreaEnd, 23);
    EXPECT_EQ(rs.screenLineAreaStart, 24);
    EXPECT_EQ(rs.screenLineAreaEnd, 151);
    EXPECT_EQ(rs.rightBorderAreaStart, 152);
    EXPECT_EQ(rs.rightBorderAreaEnd, 175);
    EXPECT_EQ(rs.blankLineAreaStart, 176);
    EXPECT_EQ(rs.blankLineAreaEnd, 223);
    EXPECT_EQ(_screen->GetPaperStartTstate(), T(72, 24)) << "the renderer's first paper pixel";
}

TEST_F(Screen_Test, HorizontalZones_128KLineEndsInLongerBlank)
{
    Use(MM_SPECTRUM128, M_ZX128, 70908);
    const RasterState& rs = _screen->GetRasterState();
    EXPECT_EQ(rs.screenLineAreaStart, 24);
    EXPECT_EQ(rs.rightBorderAreaEnd, 175);
    EXPECT_EQ(rs.blankLineAreaEnd, 227);
}

TEST_F(Screen_Test, HorizontalZones_AtmWindowAndPixelClock)
{
    for (VideoModeEnum mode : {M_ATM16, M_ATMHR, M_ATMTX, M_ATMTL})
    {
        SCOPED_TRACE(Screen::GetVideoModeName(mode));
        Use(MM_ATM710, mode);
        const RasterState& rs = _screen->GetRasterState();
        EXPECT_EQ(rs.screenLineAreaStart, ScreenAtm::SCREEN_START_T);
        EXPECT_EQ(rs.screenLineAreaEnd, ScreenAtm::SCREEN_END_T - 1);
        EXPECT_EQ(rs.blankLineAreaStart, 176);
        EXPECT_EQ(rs.paperDotsPerT, mode == M_ATM16 ? 2 : 4);
    }
}

TEST_F(Screen_Test, HorizontalZones_ProfiHiresPaperAtFourDotsPerT)
{
    Use(MM_PROFI, M_PROFIHR);
    const RasterState& rs = _screen->GetRasterState();
    EXPECT_EQ(rs.screenLineAreaStart, 24);
    EXPECT_EQ(rs.screenLineAreaEnd, 151) << "512 px over the 128 T ZX paper window";
    EXPECT_EQ(rs.paperDotsPerT, 4);
    EXPECT_EQ(rs.blankLineAreaStart, 176);
}

TEST_F(Screen_Test, DescribeBeam_ZX48ZonesAndFirstPixel)
{
    Use(MM_SPECTRUM48, M_ZX48);

    BeamPosition b = _screen->DescribeBeam(T(72, 24));
    EXPECT_STREQ(b.zone, "paper");
    EXPECT_EQ(b.paperX, 0u);
    EXPECT_EQ(b.paperXEnd, 1u);
    EXPECT_EQ(b.paperY, 0u);
    EXPECT_EQ(b.beamX, 48u);

    EXPECT_STREQ(_screen->DescribeBeam(T(72, 23)).horizontalZone, "left_border");
    EXPECT_STREQ(_screen->DescribeBeam(T(72, 152)).horizontalZone, "right_border");
    b = _screen->DescribeBeam(T(72, 176));
    EXPECT_STREQ(b.zone, "hblank");
    EXPECT_FALSE(b.inVisibleArea);
    EXPECT_STREQ(_screen->DescribeBeam(T(0, 0)).zone, "vsync");
    EXPECT_STREQ(_screen->DescribeBeam(T(71, 100)).zone, "top_border");
    EXPECT_STREQ(_screen->DescribeBeam(T(264, 100)).zone, "bottom_border");
}

TEST_F(Screen_Test, DescribeBeam_AtmWindowInModePixels)
{
    // Worked example of the video-debug design: ATM16, line 100, T 50
    Use(MM_ATM710, M_ATM16);
    BeamPosition b = _screen->DescribeBeam(T(100, 50));
    EXPECT_TRUE(b.inPaper);
    EXPECT_EQ(b.paperX, 84u);
    EXPECT_EQ(b.paperXEnd, 85u);
    EXPECT_EQ(b.paperY, 32u);
    EXPECT_STREQ(_screen->DescribeBeam(T(68, 7)).horizontalZone, "left_border");
    EXPECT_STREQ(_screen->DescribeBeam(T(68, 168)).horizontalZone, "right_border");

    // Hires: 4 px per T in the same window
    Use(MM_ATM710, M_ATMHR);
    b = _screen->DescribeBeam(T(100, 50));
    EXPECT_EQ(b.paperX, 168u);
    EXPECT_EQ(b.paperXEnd, 171u);
    EXPECT_EQ(b.paperY, 32u);
    EXPECT_EQ(_screen->DescribeBeam(T(267, 167)).paperX, 636u) << "last 4 pixels of the last row";
    EXPECT_EQ(_screen->DescribeBeam(T(267, 167)).paperY, 199u);
}

TEST_F(Screen_Test, DescribeBeam_ProfiHires)
{
    Use(MM_PROFI, M_PROFIHR);
    BeamPosition b = _screen->DescribeBeam(T(48, 24));
    EXPECT_TRUE(b.inPaper) << "240 lines start 24 lines above the ZX paper (line 48)";
    EXPECT_EQ(b.paperX, 0u);
    EXPECT_EQ(b.paperXEnd, 3u);
    EXPECT_EQ(b.paperY, 0u);
    EXPECT_EQ(_screen->DescribeBeam(T(287, 151)).paperX, 508u);
    EXPECT_EQ(_screen->DescribeBeam(T(287, 151)).paperY, 239u);
}

TEST_F(Screen_Test, DescribeBeam_Atm3AlcoKeepsAtmRaster)
{
    // M_P16 carries a Pentagon descriptor (paper at line 80); on ATM3 the
    // ATM 312-line raster applies and the paper starts at line 72
    Use(MM_ATM3, M_P16);
    const BeamPosition b = _screen->DescribeBeam(T(72, 24));
    EXPECT_TRUE(b.inPaper);
    EXPECT_EQ(b.paperY, 0u);
    EXPECT_STREQ(_screen->DescribeBeam(T(3, 0)).zone, "vsync");
    EXPECT_STREQ(_screen->DescribeBeam(T(8, 0)).zone, "vblank") << "ATM raster: 8 vsync lines";
}

TEST_F(Screen_Test, UlaProfile_OnlySinclairMachinesHaveFerrantiContention)
{
    struct Case { MEM_MODEL model; VideoModeEnum mode; bool ferranti; };
    const Case cases[] = {
        {MM_SPECTRUM48, M_ZX48, true},
        {MM_SPECTRUM128, M_ZX128, true},
        {MM_PLUS3, M_ZX128, true},
        {MM_ATM710, M_ZX48, false},   // ATM ZX-compatible mode
        {MM_ATM710, M_ATM16, false},
        {MM_ATM710, M_ATMHR, false},
        {MM_ATM3, M_ATMTL, false},
        {MM_PENTAGON, M_PENTAGON128K, false},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(testing::Message() << "model " << int(c.model) << " mode " << Screen::GetVideoModeName(c.mode));
        Use(c.model, c.mode, c.model == MM_SPECTRUM128 || c.model == MM_PLUS3 ? 70908 : c.model == MM_PENTAGON ? 71680 : 69888);
        const RasterState& rs = _screen->GetRasterState();
        EXPECT_EQ(rs.contentionEnabled, c.ferranti);
        EXPECT_EQ(rs.fetchType, c.ferranti ? ULA_FERRANTI : ULA_DISCRETE_LOGIC);
        EXPECT_EQ(rs.borderUpdateTStates, c.ferranti ? 4 : 1);
        EXPECT_EQ(_context->pUlaContention->IsContentionEnabled(), c.ferranti);
    }
}
