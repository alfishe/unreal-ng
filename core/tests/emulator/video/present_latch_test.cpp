#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "common/video/videoutils.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

/// Tests for the tear-free presentation path:
/// - VideoUtils::CopyFrameBuffer (SIMD bulk copy + scalar tail)
/// - Screen::LatchFramebuffer / CopyPresentedFramebuffer (frame-end snapshot
///   consumed by GUI/capture instead of the live framebuffer the emulation
///   thread overwrites concurrently - the source of mid-frame tearing)

/// region <VideoUtils::CopyFrameBuffer>

class VideoUtilsCopy_Test : public ::testing::Test
{
protected:
    // Verify an exact copy for a given size, with guard bytes past the end
    // to catch overruns from the 64-byte SIMD unroll
    void verifyCopy(size_t bytes)
    {
        constexpr uint8_t guard = 0xA5;
        std::vector<uint8_t> src(bytes);
        std::vector<uint8_t> dst(bytes + 64, guard);  // 64 guard bytes past the end

        for (size_t i = 0; i < bytes; i++)
        {
            src[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
        }

        VideoUtils::CopyFrameBuffer(dst.data(), src.data(), bytes);

        ASSERT_EQ(memcmp(dst.data(), src.data(), bytes), 0) << "Copy mismatch for size " << bytes;

        for (size_t i = bytes; i < bytes + 64; i++)
        {
            ASSERT_EQ(dst[i], guard) << "Overrun past end for size " << bytes << " at offset " << i;
        }
    }
};

TEST_F(VideoUtilsCopy_Test, ExactCopy_TailSizes)
{
    // Below one SIMD iteration, exactly one, one over, and unaligned tails
    for (size_t bytes : {size_t(0), size_t(1), size_t(15), size_t(63), size_t(64), size_t(65), size_t(127), size_t(191)})
    {
        verifyCopy(bytes);
    }
}

TEST_F(VideoUtilsCopy_Test, ExactCopy_FramebufferSize)
{
    // Real presentation framebuffer: 352x288 RGBA = 405504 bytes (all raster
    // modes share this geometry). A multiple of 64 - pure SIMD iterations, no
    // scalar tail; the size minus 4 adds a realistic tail on top.
    verifyCopy(352 * 288 * 4);
    verifyCopy(352 * 288 * 4 - 4);
}

/// endregion </VideoUtils::CopyFrameBuffer>

/// region <Screen presentation latch>

class PresentLatch_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _screen = _emulator->GetContext()->pScreen;
        ASSERT_NE(_screen, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(PresentLatch_Test, LatchSnapshotsCompletedFrame)
{
    FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
    ASSERT_NE(fb.memoryBuffer, nullptr);
    ASSERT_GT(fb.memoryBufferSize, 0u);

    // This test pins the TEAR-FREE property of the latch, independent of the
    // A/V-sync present delay (covered by VideoModeChange_Test.PresentQueue_*):
    // run in low-latency mode so each latch is immediately visible
    _screen->SetPresentDelayFrames(0);

    // Render a recognizable "frame" into the live framebuffer and latch it
    memset(fb.memoryBuffer, 0x11, fb.memoryBufferSize);
    _screen->LatchFramebuffer();

    std::vector<uint8_t> out(fb.memoryBufferSize, 0);
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], 0x11);
    EXPECT_EQ(out[fb.memoryBufferSize - 1], 0x11);

    // Emulation thread starts overwriting the live buffer (next frame)...
    memset(fb.memoryBuffer, 0x22, fb.memoryBufferSize / 2);  // half-written = would tear

    // ...but consumers still see the complete latched frame, not the torn one
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], 0x11) << "Presented frame must not show the in-progress render";
    EXPECT_EQ(out[fb.memoryBufferSize - 1], 0x11);

    // After the next frame-end latch, the new frame becomes visible
    memset(fb.memoryBuffer, 0x22, fb.memoryBufferSize);
    _screen->LatchFramebuffer();
    ASSERT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
    EXPECT_EQ(out[0], 0x22);
    EXPECT_EQ(out[fb.memoryBufferSize - 1], 0x22);
}

TEST_F(PresentLatch_Test, CopyRejectsUndersizedDestination)
{
    FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();

    std::vector<uint8_t> tooSmall(fb.memoryBufferSize - 1);
    EXPECT_FALSE(_screen->CopyPresentedFramebuffer(tooSmall.data(), tooSmall.size()));
    EXPECT_FALSE(_screen->CopyPresentedFramebuffer(nullptr, fb.memoryBufferSize));
}

/// endregion </Screen presentation latch>
