#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
#include "emulator/video/videofamily.h"
#include "recordingmanager.h"

/// TS-Conf half-height aspect correction (BUGS.md #6): TS-Conf stores its fat
/// pixels at half height (720x288 for a 360x288-dot raster), and a square-pixel
/// recording must double each stored line to show the true 720x576 picture -
/// the same vertical stretch the live display applies. These tests pin:
/// 1. CLASSIFICATION - StoresHalfHeightLines is true only for TS-Conf modes
/// 2. STRETCH - the row-doubling algorithm (mirrored from
///    RecordingManager::CaptureFrame) reproduces every source pixel twice,
///    byte-for-byte, in the correct rows

namespace
{

std::vector<uint8_t> CreateTestFramebuffer(uint16_t width, uint16_t height)
{
    std::vector<uint8_t> buffer(static_cast<size_t>(width) * height * 4);
    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            buffer[offset + 0] = static_cast<uint8_t>(x & 0xFF);
            buffer[offset + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
            buffer[offset + 2] = static_cast<uint8_t>(y & 0xFF);
            buffer[offset + 3] = static_cast<uint8_t>((y >> 8) & 0xFF);
        }
    }
    return buffer;
}

bool PixelAt(const uint8_t* buffer, uint16_t stride, uint16_t x, uint16_t y, uint16_t expectX, uint16_t expectY)
{
    size_t offset = (static_cast<size_t>(y) * stride + x) * 4;
    return buffer[offset + 0] == static_cast<uint8_t>(expectX & 0xFF) &&
           buffer[offset + 1] == static_cast<uint8_t>((expectX >> 8) & 0xFF) &&
           buffer[offset + 2] == static_cast<uint8_t>(expectY & 0xFF) &&
           buffer[offset + 3] == static_cast<uint8_t>((expectY >> 8) & 0xFF);
}

// Mirrors the stretch block in RecordingManager::CaptureFrame exactly: every
// source row is memcpy'd twice into the doubled-height destination
std::vector<uint8_t> StretchHalfHeightLines(const std::vector<uint8_t>& src, uint16_t width, uint16_t height)
{
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> stretched(rowBytes * height * 2);
    const uint8_t* s = src.data();
    for (uint32_t y = 0; y < height; y++)
    {
        uint8_t* row = stretched.data() + 2 * static_cast<size_t>(y) * rowBytes;
        memcpy(row, s, rowBytes);
        memcpy(row + rowBytes, s, rowBytes);
        s += rowBytes;
    }
    return stretched;
}

} // namespace

class TsConfAspect_Test : public ::testing::Test
{
};

TEST_F(TsConfAspect_Test, OnlyTsConfModesStoreHalfHeightLines)
{
    EXPECT_TRUE(StoresHalfHeightLines(M_TS16));
    EXPECT_TRUE(StoresHalfHeightLines(M_TS256));
    EXPECT_TRUE(StoresHalfHeightLines(M_TSTX));
    EXPECT_TRUE(StoresHalfHeightLines(M_TSZX));

    EXPECT_FALSE(StoresHalfHeightLines(M_NUL));
    EXPECT_FALSE(StoresHalfHeightLines(M_ZX48));
    EXPECT_FALSE(StoresHalfHeightLines(M_ATM16));
    EXPECT_FALSE(StoresHalfHeightLines(M_PROFIHR));
    EXPECT_FALSE(StoresHalfHeightLines(M_P16));
}

/// A recording of a TS-Conf session must show what the user sees: the raw
/// 720x288 framebuffer stretched to 720x576, every dot row kept twice
TEST_F(TsConfAspect_Test, StretchDoublesEveryRowAndPreservesContent)
{
    const uint16_t width = 720;
    const uint16_t height = 288;

    auto src = CreateTestFramebuffer(width, height);
    auto stretched = StretchHalfHeightLines(src, width, height);

    ASSERT_EQ(stretched.size(), static_cast<size_t>(width) * height * 2 * 4);

    for (uint16_t y = 0; y < height; y++)
    {
        // Both output rows for source row y must carry that row's pixels unchanged
        EXPECT_TRUE(PixelAt(stretched.data(), width, 0, 2 * y, 0, y)) << "row " << y << " left, first copy";
        EXPECT_TRUE(PixelAt(stretched.data(), width, 0, 2 * y + 1, 0, y)) << "row " << y << " left, second copy";
        EXPECT_TRUE(PixelAt(stretched.data(), width, width - 1, 2 * y, width - 1, y)) << "row " << y << " right, first copy";
        EXPECT_TRUE(PixelAt(stretched.data(), width, width - 1, 2 * y + 1, width - 1, y)) << "row " << y << " right, second copy";
    }
}

/// The resulting aspect-corrected frame is exactly 2x the source height and
/// the same width - no horizontal change, only the TS-Conf vertical stretch
TEST_F(TsConfAspect_Test, StretchDoesNotTouchWidth)
{
    const uint16_t width = 720;
    const uint16_t height = 288;

    auto src = CreateTestFramebuffer(width, height);
    auto stretched = StretchHalfHeightLines(src, width, height);

    const size_t expectedRowBytes = static_cast<size_t>(width) * 4;
    EXPECT_EQ(stretched.size() / expectedRowBytes, static_cast<size_t>(height) * 2)
        << "width must stay 720; only the row count doubles";
}

/// The real capture chain, not the mirrored algorithm: a sink encoder through
/// StartRecordingWithEncoder receives exactly what a live backend receives
/// from RecordingManager::CaptureFrame. Every TS-Conf video mode must deliver
/// the LIVE framebuffer's reference content, each stored line doubled,
/// byte-for-byte - geometry alone proved not enough (a recording full of
/// memory garbage once passed a dimensions-only check, BUGS.md #6)
class CaptureSinkEncoder : public EncoderBase
{
public:
    bool Start(const std::string&, const EncoderConfig& config) override
    {
        startedConfig = config;
        _recording = true;
        return true;
    }
    void Stop() override { _recording = false; }
    bool IsRecording() const override { return _recording; }
    std::string GetType() const override { return "sink"; }
    std::string GetDisplayName() const override { return "capture sink"; }
    bool SupportsVideo() const override { return true; }
    bool SupportsAudio() const override { return false; }

    void OnVideoFrame(const FramebufferDescriptor& framebuffer, double) override
    {
        lastWidth = framebuffer.width;
        lastHeight = framebuffer.height;
        lastPixels.assign(framebuffer.memoryBuffer, framebuffer.memoryBuffer + framebuffer.memoryBufferSize);
    }

    EncoderConfig startedConfig{};
    uint16_t lastWidth = 0;
    uint16_t lastHeight = 0;
    std::vector<uint8_t> lastPixels;
    bool _recording = false;
};

namespace
{
    /// A (x, y)-coded pattern: every pixel identifies its own place, so any
    /// wrong row, wrong stride or foreign memory shows up as a byte mismatch
    void WriteReferencePattern(const FramebufferDescriptor& fb)
    {
        ASSERT_NE(fb.memoryBuffer, nullptr);
        ASSERT_GE(fb.memoryBufferSize, static_cast<size_t>(fb.width) * fb.height * 4);
        for (uint16_t y = 0; y < fb.height; y++)
        {
            uint8_t* row = fb.memoryBuffer + static_cast<size_t>(y) * fb.width * 4;
            for (uint16_t x = 0; x < fb.width; x++)
            {
                row[x * 4 + 0] = static_cast<uint8_t>(x & 0xFF);
                row[x * 4 + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
                row[x * 4 + 2] = static_cast<uint8_t>(y & 0xFF);
                row[x * 4 + 3] = 0xA5;
            }
        }
    }

    bool RowEquals(const std::vector<uint8_t>& captured, uint16_t width, uint32_t row, const FramebufferDescriptor& source, uint16_t sourceRow)
    {
        return std::memcmp(captured.data() + static_cast<size_t>(row) * width * 4,
                           source.memoryBuffer + static_cast<size_t>(sourceRow) * source.width * 4,
                           static_cast<size_t>(width) * 4) == 0;
    }
}  // namespace

TEST_F(TsConfAspect_Test, EveryTsConfModeRecordsTheLiveFramebufferDoubledAndByteExact)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pScreen, nullptr);
    ASSERT_NE(context->pRecordingManager, nullptr);

    auto sink = std::make_unique<CaptureSinkEncoder>();
    CaptureSinkEncoder* sinkPtr = sink.get();
    context->pRecordingManager->SetVideoResolution(720, 288);
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("tsconf-capture-sink") + ".mp4", std::move(sink)))
        << "the sink encoder starts";

    // V_CONFIG[1:0] walks every TS-Conf video mode (ScreenTSConf::ModeOf)
    const VideoModeEnum modes[4] = {M_TSZX, M_TS16, M_TS256, M_TSTX};
    for (uint8_t v = 0; v < 4; v++)
    {
        SCOPED_TRACE("V_CONFIG=" + std::to_string(v));
        context->pPortDecoder->DecodePortOut(0x00AF, v, 0);
        emulator->RunNFrames(2);

        const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
        ASSERT_EQ(fb.videoMode, modes[v]) << "the mode switch took";
        ASSERT_EQ(fb.width, 720);
        ASSERT_EQ(fb.height, 288) << "one framebuffer geometry for every TS mode";

        WriteReferencePattern(fb);
        context->pRecordingManager->CaptureFrame(fb);

        ASSERT_EQ(sinkPtr->lastWidth, 720) << "the width is never touched";
        ASSERT_EQ(sinkPtr->lastHeight, 576) << "every stored line doubles";
        ASSERT_EQ(sinkPtr->lastPixels.size(), static_cast<size_t>(720) * 576 * 4);
        for (uint32_t y = 0; y < 288; y++)
        {
            ASSERT_TRUE(RowEquals(sinkPtr->lastPixels, 720, 2 * y, fb, y))
                << "even row " << y << " is the source line, byte-for-byte";
            ASSERT_TRUE(RowEquals(sinkPtr->lastPixels, 720, 2 * y + 1, fb, y))
                << "odd row " << y << " repeats the source line";
        }
    }

    context->pRecordingManager->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(TsConfAspect_Test, NonTsConfFramebufferPassesThroughUnchanged)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pScreen, nullptr);

    auto sink = std::make_unique<CaptureSinkEncoder>();
    CaptureSinkEncoder* sinkPtr = sink.get();
    context->pRecordingManager->SetVideoResolution(context->pScreen->GetFramebufferDescriptor().width,
                                                    context->pScreen->GetFramebufferDescriptor().height);
    ASSERT_TRUE(context->pRecordingManager->StartRecordingWithEncoder(
        TestPathHelper::GetUniqueTestScratchPath("pentagon-capture-sink") + ".mp4", std::move(sink)));

    emulator->RunNFrames(2);
    const FramebufferDescriptor fb = context->pScreen->GetFramebufferDescriptor();
    WriteReferencePattern(fb);
    context->pRecordingManager->CaptureFrame(fb);

    // Square-pixel machines need no correction: the frame reaches the encoder
    // exactly as stored
    ASSERT_EQ(sinkPtr->lastWidth, fb.width);
    ASSERT_EQ(sinkPtr->lastHeight, fb.height);
    ASSERT_EQ(sinkPtr->lastPixels.size(), fb.memoryBufferSize);
    ASSERT_EQ(std::memcmp(sinkPtr->lastPixels.data(), fb.memoryBuffer, fb.memoryBufferSize), 0)
        << "the Pentagon framebuffer is not stretched";

    context->pRecordingManager->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
