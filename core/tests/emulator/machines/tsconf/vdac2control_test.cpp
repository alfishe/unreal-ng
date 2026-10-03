// TS-Conf VDAC2 card's control (vdac2control.h): what every automation surface
// (WebAPI, MCP, CLI, Lua, Python) calls - the bus capture and the line budget
// metrics, and the reasons it gives without a card.

#ifdef ENABLE_VDAC2

#include "vdac2cardfixture.h"

#include <filesystem>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/vdac2control.h"

using namespace Vdac2Test;

class Vdac2Control_Test : public Vdac2Test::Vdac2CardFixture
{
};

/// Vdac2Control, what every automation surface calls: the capture through
/// it, and the reasons it gives without a card
TEST_F(Vdac2Control_Test, CaptureStartsStopsAndExplainsRefusals)
{
    Boot();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2card_test_control.evr");
    std::string error;
    Vdac2Control::CaptureStatus status;
    EXPECT_TRUE(Vdac2Control::HasCard(_context, &error)) << error;
    EXPECT_FALSE(Vdac2Control::StopCapture(_context, &error)) << "nothing runs yet";
    EXPECT_FALSE(Vdac2Control::StartCapture(_context, "", &error)) << "no path";

    ASSERT_TRUE(Vdac2Control::StartCapture(_context, path, &error)) << error;
    Write(kRamG, {1, 2, 3, 4});
    ASSERT_TRUE(Vdac2Control::GetCaptureStatus(_context, status, &error)) << error;
    EXPECT_TRUE(status.capturing);
    EXPECT_EQ(status.path, path);
    EXPECT_GT(status.exchanges, 0u);
    EXPECT_GT(status.bytesWritten, Vdac2Capture::kHeaderSize) << "counts what is still buffered";
    ASSERT_TRUE(Vdac2Control::StopCapture(_context, &error)) << error;
    ASSERT_TRUE(Vdac2Control::GetCaptureStatus(_context, status, &error));
    EXPECT_FALSE(status.capturing);
    EXPECT_EQ(std::filesystem::file_size(path), status.bytesWritten);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // Without the card: the reason names the configuration
    _context->config.ts_vdac = 0;
    _decoder->reset();
    EXPECT_FALSE(Vdac2Control::StartCapture(_context, path, &error));
    EXPECT_NE(error.find("TS_VDAC2"), std::string::npos) << error;
    EXPECT_FALSE(Vdac2Control::GetCaptureStatus(_context, status, &error));
}

/// The line budget metrics through the control every surface calls
/// (line-budget-metrics.md): a frame not shown is not measured unless
/// measure-always is on; then a list longer than the line period overflows
/// every line, the margin moves the soft budget, and the frame in flight shows
/// the lines passed so far
TEST_F(Vdac2Control_Test, LineBudgetMetrics)
{
    Boot();
    StartSmallScanWithSwapInterrupt();  // HCYCLE 100, PCLK 1: 100 clocks per line, 20 visible lines
    constexpr uint32_t kExtraCommands = 150;
    uint32_t address = kRamDl;
    Write32(address, kDlClear);
    address += 4;
    for (uint32_t i = 0; i < kExtraCommands; ++i, address += 4)
        Write32(address, kDlClearColorRed);
    Write32(address, kDlDisplay);
    Write32(kRegDlswap, kDlswapFrame);

    std::string error;
    Vdac2Control::FrameMetrics m;
    Tick(3 * kSmallFrameTacts);
    Read32(kRegId);  // brings the chip to now
    ASSERT_TRUE(Vdac2Control::GetFrameMetrics(_context, m, false, false, &error)) << error;
    EXPECT_FALSE(m.valid) << "not shown, not measured";
    EXPECT_FALSE(m.measureAlways);
    EXPECT_EQ(m.margin, 10u) << "[VDAC2] LineBudgetMargin default";

    ASSERT_TRUE(Vdac2Control::SetMeasureAlways(_context, true, &error)) << error;
    ASSERT_TRUE(Vdac2Control::SetLineBudgetMargin(_context, 20, &error)) << error;
    EXPECT_FALSE(Vdac2Control::SetLineBudgetMargin(_context, 51, &error));
    Tick(3 * kSmallFrameTacts);
    Read32(kRegId);
    ASSERT_TRUE(Vdac2Control::GetFrameMetrics(_context, m, true, false, &error)) << error;
    EXPECT_TRUE(m.valid);
    EXPECT_EQ(m.lines, 20u);
    EXPECT_EQ(m.hardBudget, 100u);
    EXPECT_EQ(m.softBudget, 80u) << "margin 20 %";
    EXPECT_EQ(m.linesOverHard, 20u) << "every line runs the whole list";
    EXPECT_GT(m.worstClocks, kExtraCommands);
    ASSERT_EQ(m.lineClocks.size(), 20u);
    EXPECT_EQ(m.lineClocks[7], m.worstClocks);

    // Half a frame further: the frame in flight has passed some lines, each measured
    Tick(kSmallFrameTacts / 2);
    ASSERT_TRUE(Vdac2Control::GetFrameMetrics(_context, m, true, true, &error)) << error;
    ASSERT_TRUE(m.inFlightKnown) << "no emulator runs the machine here";
    EXPECT_LE(m.inFlightLinesPassed, 20u);
    ASSERT_EQ(m.inFlightLineClocks.size(), m.inFlightLinesPassed);
    for (int32_t clocks : m.inFlightLineClocks)
        EXPECT_EQ(clocks, static_cast<int32_t>(m.worstClocks));

    ASSERT_TRUE(Vdac2Control::SetMeasureAlways(_context, false, &error));
    _context->config.ts_vdac = 0;
    _decoder->reset();
    EXPECT_FALSE(Vdac2Control::GetFrameMetrics(_context, m, false, false, &error));
    EXPECT_NE(error.find("TS_VDAC2"), std::string::npos) << error;
}

#endif // ENABLE_VDAC2
