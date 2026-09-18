/// @file screencapture_test.cpp
/// @brief Unit tests for video-mode-aware screen capture.
/// Tests FramebufferDescriptor handling and screen dimension lookups.

#include <gtest/gtest.h>
#include <cstring>

#include "emulator/video/screen.h"

/// Tests for screen capture dimensions without full emulator
class ScreenCaptureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create test framebuffer (352x288 RGBA)
        _fbWidth = 352;
        _fbHeight = 288;
        _fbData.resize(_fbWidth * _fbHeight * 4, 0);

        // Fill with test pattern
        for (uint16_t y = 0; y < _fbHeight; y++)
        {
            for (uint16_t x = 0; x < _fbWidth; x++)
            {
                size_t offset = (y * _fbWidth + x) * 4;
                _fbData[offset + 0] = static_cast<uint8_t>(x % 256);
                _fbData[offset + 1] = static_cast<uint8_t>(y % 256);
                _fbData[offset + 2] = static_cast<uint8_t>((x + y) % 256);
                _fbData[offset + 3] = 255;
            }
        }
    }

    FramebufferDescriptor MakeFramebuffer(VideoModeEnum mode)
    {
        FramebufferDescriptor fb;
        fb.videoMode = mode;
        fb.width = _fbWidth;
        fb.height = _fbHeight;
        fb.memoryBuffer = _fbData.data();
        fb.memoryBufferSize = _fbData.size();
        return fb;
    }

    uint16_t _fbWidth;
    uint16_t _fbHeight;
    std::vector<uint8_t> _fbData;
};

TEST_F(ScreenCaptureTest, FramebufferDescriptor_PreservesVideoMode)
{
    auto fb = MakeFramebuffer(M_PENTAGON128K);
    EXPECT_EQ(fb.videoMode, M_PENTAGON128K);
    EXPECT_EQ(fb.width, 352);
    EXPECT_EQ(fb.height, 288);
}

TEST_F(ScreenCaptureTest, FramebufferDescriptor_ATM16Mode)
{
    auto fb = MakeFramebuffer(M_ATM16);
    EXPECT_EQ(fb.videoMode, M_ATM16);
}

TEST_F(ScreenCaptureTest, VideoModeEnum_HasExpectedValues)
{
    // Verify video mode enum values are stable
    EXPECT_EQ(M_NUL, 0);
    EXPECT_EQ(M_ZX48, 1);
    EXPECT_EQ(M_ZX128, 2);
    EXPECT_EQ(M_PENTAGON128K, 3);
    EXPECT_LT(M_ATM16, M_MAX);
    EXPECT_LT(M_ATMHR, M_MAX);
}
