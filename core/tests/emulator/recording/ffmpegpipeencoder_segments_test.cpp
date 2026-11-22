#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/subprocess.h"
#include "emulator/video/screen.h"
#include "encoders/ffmpeg_pipe_encoder.h"
#include "ffmpeg_probe.h"

namespace
{
constexpr uint32_t kFrameWidth = 640;
constexpr uint32_t kFrameHeight = 360;

/// A white picture (R,G,B,A) of the given size
std::vector<uint8_t> WhitePicture(uint32_t width, uint32_t height)
{
    return std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0xFF);
}

void SendPicture(FFmpegPipeEncoder& encoder, std::vector<uint8_t>& picture, uint32_t width, uint32_t height)
{
    FramebufferDescriptor frame;
    frame.videoMode = M_ZX48;
    frame.width = static_cast<uint16_t>(width);
    frame.height = static_cast<uint16_t>(height);
    frame.memoryBuffer = picture.data();
    frame.memoryBufferSize = picture.size();
    encoder.OnVideoFrame(frame, 0.0);
    std::vector<int16_t> sound(882 * 2, 1000);  // one 50 Hz frame of 44.1 kHz stereo
    encoder.OnAudioSamples(sound.data(), sound.size(), 0.0);
}

/// Everything ffmpeg writes to stdout for these arguments
std::string RunFFmpeg(const std::vector<std::string>& args)
{
    Subprocess ffmpeg;
    if (!ffmpeg.spawn(FFmpegProbe::findFFmpeg(), args))
        return {};
    ffmpeg.closeStdin();
    std::string out = ffmpeg.readAllStdout();
    ffmpeg.waitForFinished(30000);
    return out;
}

/// The Y of a pixel in decoded yuv420p frames (limited range as coded: black 16, white 235)
uint8_t LumaAt(const std::string& frames, size_t frame, uint32_t x, uint32_t y)
{
    const size_t frameBytes = static_cast<size_t>(kFrameWidth) * kFrameHeight * 3 / 2;
    return static_cast<uint8_t>(frames[frame * frameBytes + static_cast<size_t>(y) * kFrameWidth + x]);
}
}  // namespace

/// A fixed frame built by ffmpeg from the source picture: a picture that changes size starts a new segment, and the
/// file is the segments one after another with the sound of the whole recording. 64x48 into 640x360 is 7.5x (the
/// remap tables), 64x24 is 10x (whole factor): picture and bars land where FrameScaler puts them
TEST(FFmpegPipeEncoder_Test, FixedFrame_PictureSizeChangesBecomeSegmentsOfOneFile)
{
    if (FFmpegProbe::findFFmpeg().empty())
        GTEST_SKIP() << "ffmpeg not available on this machine";

    const std::string file = TestPathHelper::GetUniqueTestScratchPath("ffmpeg-segments") + ".mkv";
    EncoderConfig config;
    config.videoWidth = kFrameWidth;
    config.videoHeight = kFrameHeight;
    config.videoCodec = "h264";
    config.fixedOutput = true;
    config.useHardwareAccel = false;
    config.audioSampleRate = 44100;
    config.audioChannels = 2;

    FFmpegPipeEncoder encoder;
    encoder.setBlocking(true);
    ASSERT_TRUE(encoder.Start(file, config)) << encoder.GetLastError();
    EXPECT_TRUE(encoder.TakesSourcePicture());

    std::vector<uint8_t> tall = WhitePicture(64, 48);
    std::vector<uint8_t> wide = WhitePicture(64, 24);
    for (int i = 0; i < 4; i++)
        SendPicture(encoder, tall, 64, 48);
    for (int i = 0; i < 4; i++)
        SendPicture(encoder, wide, 64, 24);
    for (int i = 0; i < 4; i++)
        SendPicture(encoder, tall, 64, 48);
    encoder.Stop();
    ASSERT_TRUE(encoder.GetLastError().empty()) << encoder.GetLastError() << "\n" << encoder.getDiagnostics();

    // Every frame as coded (yuv420p, no range conversion): 12 frames of 640x360. Passthrough: the AAC priming starts
    // the file 21 ms before the picture, and a constant-rate output would repeat the first frame to fill it (ffmpeg 8)
    const std::string luma =
        RunFFmpeg({"-v", "error", "-i", file, "-map", "0:v", "-fps_mode", "passthrough", "-f", "rawvideo", "-"});
    ASSERT_EQ(luma.size(), static_cast<size_t>(12) * kFrameWidth * kFrameHeight * 3 / 2);
    for (size_t frame : {0u, 3u, 8u, 11u})
    {
        // 64x48: 480x360 at x 80 - white in the middle, black bars left and right
        EXPECT_NEAR(LumaAt(luma, frame, 320, 20), 235, 3) << "frame " << frame;
        EXPECT_NEAR(LumaAt(luma, frame, 20, 180), 16, 3) << "frame " << frame;
    }
    for (size_t frame : {4u, 7u})
    {
        // 64x24: 640x240 at y 60 - bars above and below
        EXPECT_NEAR(LumaAt(luma, frame, 320, 20), 16, 3) << "frame " << frame;
        EXPECT_NEAR(LumaAt(luma, frame, 320, 180), 235, 3) << "frame " << frame;
    }

    // The sound of all 12 frames in one track (decoded at the output rate)
    const std::string sound = RunFFmpeg({"-v", "error", "-i", file, "-map", "0:a", "-f", "s16le", "-ac", "2", "-ar", "44100",
                                   "-"});
    EXPECT_NEAR(static_cast<double>(sound.size()) / 4, 12 * 882, 2048) << "samples per channel";

    std::error_code ec;
    std::filesystem::remove(file, ec);
}
