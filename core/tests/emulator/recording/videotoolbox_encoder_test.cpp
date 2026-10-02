#include "stdafx.h"
#include "pch.h"

#ifdef __APPLE__

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/encoderconfig.h"
#include "emulator/video/screen.h"
#include "platform/macos/videotoolbox_encoder.h"

/// VideoToolboxEncoder (the macOS native recorder) against a frame timestamp
/// that does not grow.
///
/// AVAssetWriter fails the whole file on such a frame. A failed writer still
/// reports its input ready but has no pixel buffer pool any more; the encoder
/// then made a CVPixelBuffer of the SOURCE size and wrote the scaled frame
/// into it - 4x its size at scale 2, a heap overflow with the frame's pixels
/// (the Sprinter crash: 0xFF000000 opaque-black pixels over the heap objects
/// next to it, docs/inprogress/2026-09-28-sprinter/crash-fb-overflow.md).
/// The Sprinter met it on a frame length switch (codes #2C / #2D) while
/// recording, whose timestamps stepped backwards.
///
/// Now such a frame is dropped and the recording goes on; a failed writer
/// never gets a frame written. Under ASan the old code fails here at the
/// first frame after the backwards one.
/// ~0.4 s: a real AVAssetWriter session (hardware H.264 start and finish)
TEST(VideoToolboxEncoder_Test, TimestampThatDoesNotGrow_DroppedRecordingGoesOn)
{
    // The Sprinter's recorded frame: 736 x 288 stored, lines doubled, at 2x
    constexpr uint16_t kWidth = 736;
    constexpr uint16_t kHeight = 576;

    EncoderConfig config;
    config.videoWidth = kWidth;
    config.videoHeight = kHeight;
    config.scaleFactor = 2;
    config.videoCodec = "h264";
    config.container = "mp4";
    config.audioChannels = 0;  // video only

    const std::string file = TestPathHelper::GetUniqueTestScratchPath("vt-timestamp") + ".mp4";
    VideoToolboxEncoder encoder;
    ASSERT_TRUE(encoder.Start(file, config)) << encoder.GetLastError();

    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i + 0] = static_cast<uint8_t>(i >> 4);
        pixels[i + 1] = 0x40;
        pixels[i + 2] = 0x80;
        pixels[i + 3] = 0xFF;
    }
    FramebufferDescriptor frame;
    frame.videoMode = M_SPRINTER;
    frame.width = kWidth;
    frame.height = kHeight;
    frame.memoryBuffer = pixels.data();
    frame.memoryBufferSize = pixels.size();

    // Frames 0-4 of 320 lines (20.48 ms each, the last at 0.08192 s); then the old
    // count x duration after a switch to 312 lines: frame 4 again at 4 x 19.968 ms
    double t = 0.0;
    double last = 0.0;
    for (int i = 0; i < 5; i++, t += 0.02048)
    {
        encoder.OnVideoFrame(frame, t);
        last = t;
    }
    encoder.OnVideoFrame(frame, 4 * 0.019968);  // 0.079872 < 0.08192: backwards
    encoder.OnVideoFrame(frame, last);          // the last timestamp again
    for (int i = 0; i < 5; i++, t += 0.02048)
        encoder.OnVideoFrame(frame, t);

    EXPECT_EQ(encoder.GetFramesEncoded(), 10u) << "both odd frames dropped, every other frame encoded: "
                                               << encoder.GetLastError();
    encoder.Stop();

    std::error_code ec;
    EXPECT_GT(std::filesystem::file_size(file, ec), 0u) << "the file is finished";
    std::filesystem::remove(file, ec);
}

#endif  // __APPLE__
