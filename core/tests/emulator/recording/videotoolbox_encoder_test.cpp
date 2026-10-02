#include "stdafx.h"
#include "pch.h"

#ifdef __APPLE__

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

namespace
{
uint32_t Be32(const std::vector<uint8_t>& d, size_t at)
{
    return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | d[at + 3];
}

std::string BoxType(const std::vector<uint8_t>& d, size_t at)
{
    return std::string(reinterpret_cast<const char*>(&d[at + 4]), 4);
}

/// Calls visit(type, payloadBegin, boxEnd) for each box in [begin, end)
template <typename Visit>
void ForEachBox(const std::vector<uint8_t>& d, size_t begin, size_t end, Visit visit)
{
    for (size_t at = begin; at + 8 <= end;)
    {
        // size 1: a 64-bit size follows the type (AVAssetWriter's mdat); size 0: up to the end
        uint64_t size = Be32(d, at);
        size_t header = 8;
        if (size == 1 && at + 16 <= end)
        {
            size = (uint64_t(Be32(d, at + 8)) << 32) | Be32(d, at + 12);
            header = 16;
        }
        else if (size == 0)
        {
            size = end - at;
        }
        if (size < header || at + size > end)
            return;
        visit(BoxType(d, at), at + header, static_cast<size_t>(at + size));
        at += static_cast<size_t>(size);
    }
}

/// The first edit's media time of the sound track ('soun' handler) of an MP4, -1 when there is none
/// (moov / trak / mdia / hdlr names the track, moov / trak / edts / elst holds the edit)
int64_t SoundTrackEditMediaTime(const std::vector<uint8_t>& d)
{
    int64_t found = -1;
    ForEachBox(d, 0, d.size(), [&](const std::string& type, size_t begin, size_t end) {
        if (type != "moov")
            return;
        ForEachBox(d, begin, end, [&](const std::string& trakType, size_t trakBegin, size_t trakEnd) {
            if (trakType != "trak")
                return;
            bool sound = false;
            int64_t mediaTime = -1;
            ForEachBox(d, trakBegin, trakEnd, [&](const std::string& child, size_t childBegin, size_t childEnd) {
                if (child == "mdia")
                    ForEachBox(d, childBegin, childEnd, [&](const std::string& m, size_t mBegin, size_t) {
                        if (m == "hdlr")
                            sound = BoxType(d, mBegin + 4) == "soun";  // version/flags, pre_defined, handler
                    });
                if (child == "edts")
                    ForEachBox(d, childBegin, childEnd, [&](const std::string& e, size_t eBegin, size_t) {
                        if (e != "elst" || Be32(d, eBegin + 4) == 0)
                            return;
                        mediaTime = d[eBegin] == 1 ? int64_t((uint64_t(Be32(d, eBegin + 16)) << 32) | Be32(d, eBegin + 20))
                                                   : int64_t(int32_t(Be32(d, eBegin + 12)));
                    });
            });
            if (sound)
                found = mediaTime;
        });
    });
    return found;
}
}  // namespace

/// The AAC track starts with the picture: AVAssetWriter trims the encoder's 2112 priming frames (44 ms at
/// 48 kHz) with an edit list only when the audio input is not real-time. With a real-time input the priming
/// stayed in the track as sound, so every recording played its sound 44 ms (over two frames) late.
/// ~0.3 s: a real AVAssetWriter session with an AAC track
TEST(VideoToolboxEncoder_Test, AacPrimingIsTrimmed_SoundStartsWithThePicture)
{
    constexpr uint16_t kSize = 64;
    constexpr uint32_t kRate = 44100;
    EncoderConfig config;
    config.videoWidth = kSize;
    config.videoHeight = kSize;
    config.scaleFactor = 1;
    config.videoCodec = "h264";
    config.container = "mp4";
    config.audioCodec = "aac";
    config.audioChannels = 2;
    config.audioSampleRate = kRate;

    const std::string file = TestPathHelper::GetUniqueTestScratchPath("vt-aac-priming") + ".mp4";
    VideoToolboxEncoder encoder;
    ASSERT_TRUE(encoder.Start(file, config)) << encoder.GetLastError();

    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4, 0x80);
    FramebufferDescriptor frame;
    frame.width = kSize;
    frame.height = kSize;
    frame.memoryBuffer = pixels.data();
    frame.memoryBufferSize = pixels.size();

    constexpr size_t kFrameSamples = kRate / 50;  // 20 ms
    std::vector<int16_t> audio(kFrameSamples * 2);
    for (size_t f = 0; f < 50; f++)
    {
        for (size_t i = 0; i < kFrameSamples; i++)
            audio[2 * i] = audio[2 * i + 1] = static_cast<int16_t>(8000 * std::sin(0.1 * double(f * kFrameSamples + i)));
        encoder.OnAudioSamples(audio.data(), audio.size(), double(f * kFrameSamples) / kRate);
        encoder.OnVideoFrame(frame, f * 0.02);
    }
    EXPECT_EQ(encoder.GetAudioSamplesEncoded(), 50 * kFrameSamples * 2) << "no audio chunk dropped";
    encoder.Stop();

    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty()) << file;
    const int64_t mediaTime = SoundTrackEditMediaTime(bytes);
    EXPECT_GT(mediaTime, 0) << "the sound track's edit list skips the AAC priming (media time " << mediaTime << ")";

    std::error_code ec;
    std::filesystem::remove(file, ec);
}

#endif  // __APPLE__
