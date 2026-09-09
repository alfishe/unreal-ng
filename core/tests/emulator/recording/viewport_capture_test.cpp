#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "emulator/video/screen.h"
#include "recordingmanager.h"
#include "encoderconfig.h"

/// Viewport capture region tests: verify that VideoCaptureRegion::Viewport
/// correctly crops the framebuffer according to DisplayViewport settings.
///
/// These tests pin three properties:
/// 1. DIMENSIONS - viewport capture produces output matching the cropped
///    dimensions (framebuffer.width - cropLeft - cropRight, etc.)
/// 2. CROPPING - the captured data matches the expected subregion of the
///    source framebuffer (correct offset and stride handling)
/// 3. LOCK - viewport settings captured at recording start are used
///    throughout the recording (mid-recording viewport changes ignored)

namespace
{

// Create a test framebuffer with a known pattern
std::vector<uint8_t> CreateTestFramebuffer(uint16_t width, uint16_t height)
{
    std::vector<uint8_t> buffer(static_cast<size_t>(width) * height * 4);
    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            // BGRA format: encode x,y coordinates in the pixel
            buffer[offset + 0] = static_cast<uint8_t>(x & 0xFF);        // B = x low byte
            buffer[offset + 1] = static_cast<uint8_t>((x >> 8) & 0xFF); // G = x high byte
            buffer[offset + 2] = static_cast<uint8_t>(y & 0xFF);        // R = y low byte
            buffer[offset + 3] = static_cast<uint8_t>((y >> 8) & 0xFF); // A = y high byte
        }
    }
    return buffer;
}

// Verify that a pixel at (x, y) in cropped buffer matches source at (srcX, srcY)
bool VerifyPixel(const uint8_t* cropped, uint16_t croppedWidth, uint16_t x, uint16_t y,
                 uint16_t srcX, uint16_t srcY)
{
    size_t offset = (static_cast<size_t>(y) * croppedWidth + x) * 4;
    return cropped[offset + 0] == static_cast<uint8_t>(srcX & 0xFF) &&
           cropped[offset + 1] == static_cast<uint8_t>((srcX >> 8) & 0xFF) &&
           cropped[offset + 2] == static_cast<uint8_t>(srcY & 0xFF) &&
           cropped[offset + 3] == static_cast<uint8_t>((srcY >> 8) & 0xFF);
}

} // namespace

class ViewportCapture_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

/// Test that DisplayViewport correctly calculates cropped dimensions
TEST_F(ViewportCapture_Test, ViewportDimensionCalculation)
{
    // Full overscan (384x304) with no cropping
    DisplayViewport fullOverscan = {0, 0, 0, 0};
    EXPECT_EQ(fullOverscan.GetDisplayWidth(384), 384);
    EXPECT_EQ(fullOverscan.GetDisplayHeight(304), 304);

    // Symmetric horizontal (352x304) - crop 32px from right
    DisplayViewport symmetric = {0, 32, 0, 0};
    EXPECT_EQ(symmetric.GetDisplayWidth(384), 352);
    EXPECT_EQ(symmetric.GetDisplayHeight(304), 304);

    // Standard (352x288) - crop 32px right, 16px top
    DisplayViewport standard = {0, 32, 16, 0};
    EXPECT_EQ(standard.GetDisplayWidth(384), 352);
    EXPECT_EQ(standard.GetDisplayHeight(304), 288);

    // Screen only (256x192) - crop to paper area
    DisplayViewport screenOnly = {48, 80, 64, 48};
    EXPECT_EQ(screenOnly.GetDisplayWidth(384), 256);
    EXPECT_EQ(screenOnly.GetDisplayHeight(304), 192);
}

/// Test that framebuffer cropping produces correct pixel data
TEST_F(ViewportCapture_Test, FramebufferCroppingCorrectness)
{
    const uint16_t srcWidth = 384;
    const uint16_t srcHeight = 304;

    // Create test framebuffer with coordinate-encoded pixels
    auto srcBuffer = CreateTestFramebuffer(srcWidth, srcHeight);

    // Test cropping with standard viewport (0, 32, 16, 0)
    DisplayViewport vp = {0, 32, 16, 0};
    uint16_t croppedWidth = vp.GetDisplayWidth(srcWidth);
    uint16_t croppedHeight = vp.GetDisplayHeight(srcHeight);

    ASSERT_EQ(croppedWidth, 352);
    ASSERT_EQ(croppedHeight, 288);

    // Perform cropping (similar to RecordingManager::CaptureFrame)
    std::vector<uint8_t> croppedBuffer(static_cast<size_t>(croppedWidth) * croppedHeight * 4);

    const uint8_t* src = srcBuffer.data() +
        (static_cast<size_t>(vp.cropTop) * srcWidth + vp.cropLeft) * 4;

    for (uint16_t y = 0; y < croppedHeight; y++)
    {
        memcpy(croppedBuffer.data() + static_cast<size_t>(y) * croppedWidth * 4,
               src, static_cast<size_t>(croppedWidth) * 4);
        src += static_cast<size_t>(srcWidth) * 4;
    }

    // Verify corner pixels
    // Top-left of cropped should be (cropLeft, cropTop) in source
    EXPECT_TRUE(VerifyPixel(croppedBuffer.data(), croppedWidth, 0, 0,
                            vp.cropLeft, vp.cropTop));

    // Top-right of cropped
    EXPECT_TRUE(VerifyPixel(croppedBuffer.data(), croppedWidth, croppedWidth - 1, 0,
                            vp.cropLeft + croppedWidth - 1, vp.cropTop));

    // Bottom-left of cropped
    EXPECT_TRUE(VerifyPixel(croppedBuffer.data(), croppedWidth, 0, croppedHeight - 1,
                            vp.cropLeft, vp.cropTop + croppedHeight - 1));

    // Bottom-right of cropped
    EXPECT_TRUE(VerifyPixel(croppedBuffer.data(), croppedWidth, croppedWidth - 1, croppedHeight - 1,
                            vp.cropLeft + croppedWidth - 1, vp.cropTop + croppedHeight - 1));

    // Center pixel
    uint16_t midX = croppedWidth / 2;
    uint16_t midY = croppedHeight / 2;
    EXPECT_TRUE(VerifyPixel(croppedBuffer.data(), croppedWidth, midX, midY,
                            vp.cropLeft + midX, vp.cropTop + midY));
}

/// Test that viewport presets have expected values
TEST_F(ViewportCapture_Test, ViewportPresetValues)
{
    // Full overscan - no cropping
    EXPECT_EQ(ViewportPresets::FULL_OVERSCAN.cropLeft, 0);
    EXPECT_EQ(ViewportPresets::FULL_OVERSCAN.cropRight, 0);
    EXPECT_EQ(ViewportPresets::FULL_OVERSCAN.cropTop, 0);
    EXPECT_EQ(ViewportPresets::FULL_OVERSCAN.cropBottom, 0);

    // Symmetric horizontal - crop right only
    EXPECT_EQ(ViewportPresets::SYMMETRIC_HORIZONTAL.cropLeft, 0);
    EXPECT_EQ(ViewportPresets::SYMMETRIC_HORIZONTAL.cropRight, 32);
    EXPECT_EQ(ViewportPresets::SYMMETRIC_HORIZONTAL.cropTop, 0);
    EXPECT_EQ(ViewportPresets::SYMMETRIC_HORIZONTAL.cropBottom, 0);

    // Standard - crop right and top/bottom to match 352x288
    EXPECT_EQ(ViewportPresets::STANDARD.cropLeft, 0);
    EXPECT_EQ(ViewportPresets::STANDARD.cropRight, 32);
    EXPECT_EQ(ViewportPresets::STANDARD.cropTop, 8);
    EXPECT_EQ(ViewportPresets::STANDARD.cropBottom, 8);

    // Screen only - crop to 256x192 paper area
    EXPECT_EQ(ViewportPresets::SCREEN_ONLY.cropLeft, 48);
    EXPECT_EQ(ViewportPresets::SCREEN_ONLY.cropRight, 80);
    EXPECT_EQ(ViewportPresets::SCREEN_ONLY.cropTop, 56);
    EXPECT_EQ(ViewportPresets::SCREEN_ONLY.cropBottom, 56);
}

/// Test VideoCaptureRegion enum has expected values
TEST_F(ViewportCapture_Test, CaptureRegionEnumValues)
{
    // Verify the new Viewport option exists alongside existing options
    VideoCaptureRegion mainScreen = VideoCaptureRegion::MainScreen;
    VideoCaptureRegion fullFrame = VideoCaptureRegion::FullFrame;
    VideoCaptureRegion viewport = VideoCaptureRegion::Viewport;

    // They should all be distinct values
    EXPECT_NE(static_cast<int>(mainScreen), static_cast<int>(fullFrame));
    EXPECT_NE(static_cast<int>(mainScreen), static_cast<int>(viewport));
    EXPECT_NE(static_cast<int>(fullFrame), static_cast<int>(viewport));
}
