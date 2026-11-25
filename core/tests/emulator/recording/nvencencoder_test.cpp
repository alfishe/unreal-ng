#include "stdafx.h"
#include "pch.h"

#ifdef _WIN32

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/encoderconfig.h"
#include "emulator/video/screen.h"
#include "platform/windows/nvenc_encoder.h"
#include "platform/windows/nvenc_probe.h"

/// NvencEncoder (the Windows native recorder) keeps every frame it is given.
///
/// NVENC in sync mode holds up to lookaheadDepth + frameIntervalP frames before their output exists. The encoder had
/// 8 buffer pairs and read the oldest output when the ring was full: with qualityPreset 5+ (lookahead 8, two
/// B-frames) that output did not exist yet, nvEncLockBitstream failed, the frame was lost and its input buffer was
/// refilled while the lookahead still used it. Every recording lost its first 8 frames with the IDR (SPS / PPS):
/// 192 of 200 frames with decode errors, a 12-frame recording finished without moov.
///
/// Now the ring is lookahead + frameIntervalP + 2 deep and an output is read only once it is older than the
/// encoder's delay. The test needs an NVIDIA GPU; it is skipped without one.
namespace
{
uint32_t Be32(const std::vector<uint8_t>& d, size_t at)
{
    return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | d[at + 3];
}

/// Calls visit(type, payloadBegin, boxEnd) for each box in [begin, end)
template <typename Visit>
void ForEachBox(const std::vector<uint8_t>& d, size_t begin, size_t end, Visit visit)
{
    for (size_t at = begin; at + 8 <= end;)
    {
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
        visit(std::string(reinterpret_cast<const char*>(&d[at + 4]), 4), at + header, static_cast<size_t>(at + size));
        at += static_cast<size_t>(size);
    }
}

struct VideoTrack
{
    bool hasMoov = false;
    int64_t samples = -1;    // stsz sample count of the 'vide' track
    int64_t firstSync = -1;  // first stss entry (1-based sample number), 0 when the box is absent: all samples sync
};

/// moov / trak (hdlr 'vide') / mdia / minf / stbl / stsz + stss
VideoTrack ReadVideoTrack(const std::vector<uint8_t>& d)
{
    VideoTrack track;
    ForEachBox(d, 0, d.size(), [&](const std::string& type, size_t begin, size_t end) {
        if (type != "moov")
            return;
        track.hasMoov = true;
        ForEachBox(d, begin, end, [&](const std::string& trakType, size_t trakBegin, size_t trakEnd) {
            if (trakType != "trak")
                return;
            ForEachBox(d, trakBegin, trakEnd, [&](const std::string& mdiaType, size_t mdiaBegin, size_t mdiaEnd) {
                if (mdiaType != "mdia")
                    return;
                bool video = false;
                int64_t samples = -1;
                int64_t firstSync = 0;
                ForEachBox(d, mdiaBegin, mdiaEnd, [&](const std::string& t, size_t b, size_t e) {
                    // hdlr: version/flags, pre_defined, handler_type
                    if (t == "hdlr" && b + 12 <= e)
                        video = std::string(reinterpret_cast<const char*>(&d[b + 8]), 4) == "vide";
                    if (t != "minf")
                        return;
                    ForEachBox(d, b, e, [&](const std::string& minfType, size_t minfBegin, size_t minfEnd) {
                        if (minfType != "stbl")
                            return;
                        ForEachBox(d, minfBegin, minfEnd, [&](const std::string& s, size_t sb, size_t se) {
                            // stsz: version/flags, sample_size, sample_count
                            if (s == "stsz" && sb + 12 <= se)
                                samples = Be32(d, sb + 8);
                            // stss: version/flags, entry_count, entries
                            if (s == "stss" && sb + 12 <= se && Be32(d, sb + 4) > 0)
                                firstSync = Be32(d, sb + 8);
                        });
                    });
                });
                if (video)
                {
                    track.samples = samples;
                    track.firstSync = firstSync;
                }
            });
        });
    });
    return track;
}

/// Records `frames` moving frames (a bar crossing a gradient, so P- and B-frames carry real changes) and checks the
/// MP4 holds all of them, the first one a sync sample
void RecordAndCheck(const std::string& codec, int qualityPreset, int frames)
{
    const NvencProbe::NvencInfo info = NvencProbe::getInfo();
    if (!info.available)
        GTEST_SKIP() << "no NVENC on this machine";
    if (codec == "h265" && !info.supportsHEVC)
        GTEST_SKIP() << "the GPU has no HEVC encoder";

    constexpr uint16_t kWidth = 352;
    constexpr uint16_t kHeight = 288;

    EncoderConfig config;
    config.videoWidth = kWidth;
    config.videoHeight = kHeight;
    config.scaleFactor = 1;
    config.frameRate = 50.0f;
    config.videoCodec = codec;
    config.container = "mp4";
    config.qualityPreset = qualityPreset;

    const std::string file = TestPathHelper::GetUniqueTestScratchPath("nvenc-" + codec) + ".mp4";
    {
        NvencEncoder encoder;
        if (!encoder.Start(file, config))
            GTEST_SKIP() << "NVENC session refused (" << encoder.GetLastError() << ")";

        std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
        FramebufferDescriptor frame;
        frame.videoMode = M_ZX48;
        frame.width = kWidth;
        frame.height = kHeight;
        frame.memoryBuffer = pixels.data();
        frame.memoryBufferSize = pixels.size();

        for (int i = 0; i < frames; i++)
        {
            for (uint32_t y = 0; y < kHeight; y++)
            {
                for (uint32_t x = 0; x < kWidth; x++)
                {
                    uint8_t* p = &pixels[(static_cast<size_t>(y) * kWidth + x) * 4];
                    const bool bar = ((x + static_cast<uint32_t>(i) * 7) % kWidth) < 32;
                    p[0] = bar ? 0xFF : static_cast<uint8_t>(x);
                    p[1] = bar ? 0xFF : static_cast<uint8_t>(y);
                    p[2] = static_cast<uint8_t>(i * 5);
                    p[3] = 0xFF;
                }
            }
            encoder.OnVideoFrame(frame, i / 50.0);
        }
        EXPECT_EQ(encoder.GetFramesEncoded(), static_cast<uint64_t>(frames)) << encoder.GetLastError();
        encoder.Stop();
        EXPECT_EQ(encoder.GetLastError(), "") << "no output lost";
    }

    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const VideoTrack track = ReadVideoTrack(bytes);
    EXPECT_TRUE(track.hasMoov) << "the MP4 is finished (moov written)";
    EXPECT_EQ(track.samples, frames) << "every submitted frame is in the file";
    EXPECT_LE(track.firstSync, 1) << "the first sample is the IDR";

    std::error_code ec;
    std::filesystem::remove(file, ec);
}
}  // namespace

// ~0.3 s each: a real NVENC session start and finish
TEST(NvencEncoder_Test, ShortRecordingH264_AllFramesAndMoov) { RecordAndCheck("h264", 5, 12); }
TEST(NvencEncoder_Test, ShortRecordingH265_AllFramesAndMoov) { RecordAndCheck("h265", 5, 12); }
TEST(NvencEncoder_Test, LookaheadH264_AllFramesKept) { RecordAndCheck("h264", 10, 200); }
TEST(NvencEncoder_Test, LookaheadH265_AllFramesKept) { RecordAndCheck("h265", 10, 200); }
TEST(NvencEncoder_Test, NoBFramesH264_AllFramesKept) { RecordAndCheck("h264", 2, 60); }

#endif  // _WIN32
