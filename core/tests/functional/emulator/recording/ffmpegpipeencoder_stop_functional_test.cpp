/// @file ffmpegpipeencoder_stop_functional_test.cpp
/// @brief FFmpegPipeEncoder::Stop right after Start, while ffmpeg is still starting: the recording is finished, not
/// killed. Functional (the real clock: Stop's 1 s deadline against an ffmpeg that takes 2 s to start):
/// UNREAL_FUNCTIONAL_TESTS=1, filter *_Functional.* (core/tests/README.md, The functional suite)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "common/subprocess.h"
#include "emulator/video/screen.h"
#include "encoders/ffmpeg_pipe_encoder.h"
#include "ffmpeg_probe.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 48;
constexpr int kFrames = 4;

void SendPicture(FFmpegPipeEncoder& encoder, std::vector<uint8_t>& picture)
{
    FramebufferDescriptor frame;
    frame.videoMode = M_ZX48;
    frame.width = static_cast<uint16_t>(kWidth);
    frame.height = static_cast<uint16_t>(kHeight);
    frame.memoryBuffer = picture.data();
    frame.memoryBufferSize = picture.size();
    encoder.OnVideoFrame(frame, 0.0);
    std::vector<int16_t> sound(882 * 2, 1000);  // one 50 Hz frame of 44.1 kHz stereo
    encoder.OnAudioSamples(sound.data(), sound.size(), 0.0);
}

/// The number of video frames in a file (ffmpeg decodes them to raw gray frames)
size_t CountFrames(const std::string& file, uint32_t width, uint32_t height)
{
    Subprocess ffmpeg;
    if (!ffmpeg.spawn(FFmpegProbe::findFFmpeg(), {"-v", "error", "-i", file, "-map", "0:v", "-fps_mode", "passthrough",
                                                  "-f", "rawvideo", "-pix_fmt", "gray", "-"}))
        return 0;
    ffmpeg.closeStdin();
    const std::string out = ffmpeg.readAllStdout();
    ffmpeg.waitForFinished(30000);
    return out.size() / (static_cast<size_t>(width) * height);
}

class FFmpegPipeEncoderStop_Functional : public ::testing::Test
{
protected:
    void SetUp() override
    {
        SKIP_UNLESS_FUNCTIONAL();
#ifdef _WIN32
        GTEST_SKIP() << "the slow-starting ffmpeg is a shell script";
#else
        _ffmpeg = FFmpegProbe::findFFmpeg();
        if (_ffmpeg.empty())
            GTEST_SKIP() << "ffmpeg not available on this machine";
        // ffmpeg that sleeps 2 s before a recording (the probes, -encoders and the like, run at once): longer than
        // Stop's 1 s, as a loaded machine or a GPU encoder's opening makes it
        _wrapper = TestPathHelper::GetUniqueTestScratchPath("slow-ffmpeg") + ".sh";
        std::ofstream script(_wrapper);
        script << "#!/bin/sh\ncase \"$*\" in *rawvideo*) sleep 2 ;; esac\nexec \"" << _ffmpeg << "\" \"$@\"\n";
        script.close();
        ASSERT_EQ(chmod(_wrapper.c_str(), 0755), 0);
#endif
    }

    void TearDown() override
    {
        std::error_code ec;
        if (!_wrapper.empty())
            std::filesystem::remove(_wrapper, ec);
        if (!_file.empty())
            std::filesystem::remove(_file, ec);
    }

    /// Start, four frames, Stop at once; the file has the four frames
    void RecordAndStop(bool fixedOutput)
    {
        _file = TestPathHelper::GetUniqueTestScratchPath("ffmpeg-stop") + (fixedOutput ? ".mkv" : ".mp4");
        EncoderConfig config;
        config.videoWidth = fixedOutput ? 320 : kWidth;
        config.videoHeight = fixedOutput ? 240 : kHeight;
        config.videoCodec = "h264";
        config.fixedOutput = fixedOutput;
        config.useHardwareAccel = false;
        config.audioSampleRate = 44100;
        config.audioChannels = 2;
        config.ffmpegPath = _wrapper;

        FFmpegPipeEncoder encoder;
        encoder.setBlocking(false);  // the frames queue while ffmpeg starts, as in the emulator
        ASSERT_TRUE(encoder.Start(_file, config)) << encoder.GetLastError();
        std::vector<uint8_t> picture(static_cast<size_t>(kWidth) * kHeight * 4, 0xFF);
        for (int i = 0; i < kFrames; i++)
            SendPicture(encoder, picture);
        encoder.Stop();
        ASSERT_TRUE(encoder.GetLastError().empty()) << encoder.GetLastError() << "\n" << encoder.getDiagnostics();
        EXPECT_EQ(CountFrames(_file, config.videoWidth, config.videoHeight), static_cast<size_t>(kFrames))
            << "a starting ffmpeg was killed: " << encoder.getDiagnostics();
    }

    std::string _ffmpeg;
    std::string _wrapper;
    std::string _file;
};
}  // namespace

TEST_F(FFmpegPipeEncoderStop_Functional, StopWhileFFmpegStarts_FinishesTheFile)
{
    RecordAndStop(false);
}

TEST_F(FFmpegPipeEncoderStop_Functional, StopWhileTheSegmentsFFmpegStarts_FinishesTheFile)
{
    RecordAndStop(true);
}
