// TS-Conf VDAC2 card's control (vdac2control.h): what every automation surface
// (WebAPI, MCP, CLI, Lua, Python) calls - the bus capture and the line budget
// metrics, and the reasons it gives without a card.

#ifdef ENABLE_VDAC2

#include "vdac2cardfixture.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/executionarbiter.h"
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

namespace
{
/// The `drawn` byte of every frame record of an .evr stream (vdac2-test-corpus.md §4)
std::vector<uint8_t> FrameRecordsDrawn(const std::string& path)
{
    std::vector<uint8_t> file;
    if (std::FILE* f = std::fopen(path.c_str(), "rb"))
    {
        uint8_t chunk[4096];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0)
            file.insert(file.end(), chunk, chunk + n);
        std::fclose(f);
    }
    auto le = [&](size_t at, int bytes) {
        uint64_t v = 0;
        for (int i = 0; i < bytes; i++)
            v |= static_cast<uint64_t>(file[at + i]) << (8 * i);
        return v;
    };
    std::vector<uint8_t> drawn;
    size_t pos = Vdac2Capture::kHeaderSize;
    while (pos < file.size())
    {
        const uint8_t kind = file[pos++];
        while (pos < file.size() && (file[pos++] & 0x80))
            ;  // the clock delta (LEB128)
        switch (kind)
        {
            case Vdac2Capture::kSelect: pos += 1; break;
            case Vdac2Capture::kByte: pos += 2; break;
            case Vdac2Capture::kFrame: drawn.push_back(file[pos + 8]); pos += 21; break;
            case Vdac2Capture::kPowerOn: break;
            case Vdac2Capture::kState:
            {
                pos += 4 + le(pos, 4);
                const uint64_t regions = le(pos, 4);
                pos += 4;
                for (uint64_t r = 0; r < regions; r++)
                {
                    pos += 1 + file[pos];
                    pos += 4 + le(pos, 4);
                }
                break;
            }
            default: return drawn;  // kEnd
        }
    }
    return drawn;
}
} // namespace

/// The frame a capture starts in is recorded as not drawn: the lines above the start were drawn before the saved
/// state (or not at all), so a replay from that state cannot draw them, and comparing its picture would report a
/// mismatch that is not one. The frames after it are drawn and compared
TEST_F(Vdac2Control_Test, CaptureStartedInsideAFrameDoesNotCompareThatFrame)
{
    Boot();
    StartSmallScanWithSwapInterrupt();  // one chip frame = 364.6 raster tacts
    Tick(1000);
    Read32(kRegId);  // the chip caught up: frames are running
    Tick(180);       // half way into a frame
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2control_test_inflight.evr");
    std::string error;
    ASSERT_TRUE(Vdac2Control::StartCapture(_context, path, &error)) << error;
    Tick(1200);
    Read32(kRegId);
    ASSERT_TRUE(Vdac2Control::StopCapture(_context, &error)) << error;

    const std::vector<uint8_t> drawn = FrameRecordsDrawn(path);
    ASSERT_GE(drawn.size(), 2u);
    EXPECT_EQ(drawn[0], 0) << "the frame the capture started in";
    for (size_t i = 1; i < drawn.size(); i++)
        EXPECT_EQ(drawn[i], 1) << "frame record " << i;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

/// A capture starts and ends on the thread of the surface that asks (WebAPI, MCP, CLI, Lua, Python) while the machine
/// runs on its own, and on the VDAC4 an ESP job may host the FT812 on a worker meanwhile. The chip's state is saved and
/// the stream opened and closed with the CPU held (Emulator::CpuHold, the arbiter's command): before, a capture of
/// Metal Slug started that way replayed with the frame count two frames off and hundreds of answers different
TEST(Vdac2ControlMachine_Test, CaptureOnARunningMachineHoldsTheCpu)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL-VDAC4", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->EnableTurboMode();  // only the frames have to move; nothing here looks at pixels
    emulator->GetArbiter().SetDecisionLog(true);
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&]() { return context->emulatorState.frame_counter > 1; }));

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("vdac2control_test_running.evr");
    std::string error;
    ASSERT_TRUE(Vdac2Control::StartCapture(context, path, &error)) << error;
    ASSERT_TRUE(Vdac2Control::StopCapture(context, &error)) << error;

    std::vector<std::string> held;
    for (const execution::Decision& d : emulator->GetArbiter().TakeDecisions())
        if (d.kind == execution::Decision::Kind::CommandStarted)
            held.push_back(d.tag);
    EXPECT_NE(std::find(held.begin(), held.end(), "vdac2 capture start"), held.end());
    EXPECT_NE(std::find(held.begin(), held.end(), "vdac2 capture stop"), held.end());
    EXPECT_EQ(emulator->DriverViolations(), 0u);
    EmulatorTestHelper::CleanupEmulator(emulator);
    std::error_code ec;
    std::filesystem::remove(path, ec);
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
