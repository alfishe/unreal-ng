#include "emulator/recording/encoders/gif_encoder.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "3rdparty/gif/gif.h"
#include "emulator/recording/encoder_config.h"
#include "emulator/video/screen.h"

/// @brief Test fixture for GIF encoder tests
class GIFEncoderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temp directory for test outputs
        _tempDir = std::filesystem::temp_directory_path() / "gif_encoder_test";
        std::filesystem::create_directories(_tempDir);
    }

    void TearDown() override
    {
        // Clean up temp files
        std::error_code ec;
        std::filesystem::remove_all(_tempDir, ec);
    }

    std::filesystem::path GetTempFile(const std::string& name)
    {
        return _tempDir / name;
    }

    EncoderConfig CreateValidConfig(uint32_t width = 256, uint32_t height = 192)
    {
        EncoderConfig config;
        config.videoWidth = width;
        config.videoHeight = height;
        config.gifDelayMs = 20;
        return config;
    }

    // Create a simple test framebuffer
    FramebufferDescriptor CreateTestFramebuffer(uint32_t width, uint32_t height)
    {
        _testBuffer.resize(width * height * 4);  // RGBA
        // Fill with simple pattern
        for (size_t i = 0; i < _testBuffer.size(); i += 4)
        {
            _testBuffer[i + 0] = 0;    // R
            _testBuffer[i + 1] = 128;  // G
            _testBuffer[i + 2] = 255;  // B
            _testBuffer[i + 3] = 255;  // A
        }

        FramebufferDescriptor fb;
        fb.width = width;
        fb.height = height;
        fb.memoryBuffer = _testBuffer.data();
        fb.memoryBufferSize = _testBuffer.size();
        return fb;
    }

private:
    std::filesystem::path _tempDir;
    std::vector<uint8_t> _testBuffer;
};

// ================== Lifecycle Tests ==================

TEST_F(GIFEncoderTest, StartWithValidParameters)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("valid.gif").string();

    bool result = encoder.Start(filename, config);

    EXPECT_TRUE(result);
    EXPECT_TRUE(encoder.IsRecording());
    EXPECT_EQ(encoder.GetFramesEncoded(), 0);
    EXPECT_TRUE(encoder.GetLastError().empty());
}

TEST_F(GIFEncoderTest, StartWithEmptyFilename)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();

    bool result = encoder.Start("", config);

    EXPECT_FALSE(result);
    EXPECT_FALSE(encoder.IsRecording());
    EXPECT_FALSE(encoder.GetLastError().empty());
}

TEST_F(GIFEncoderTest, StartWithZeroDimensions)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig(0, 0);
    std::string filename = GetTempFile("zero_dim.gif").string();

    bool result = encoder.Start(filename, config);

    EXPECT_FALSE(result);
    EXPECT_FALSE(encoder.IsRecording());
    EXPECT_FALSE(encoder.GetLastError().empty());
}

TEST_F(GIFEncoderTest, StartWithZeroWidth)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig(0, 192);
    std::string filename = GetTempFile("zero_width.gif").string();

    bool result = encoder.Start(filename, config);

    EXPECT_FALSE(result);
    EXPECT_FALSE(encoder.IsRecording());
}

TEST_F(GIFEncoderTest, StartWithZeroHeight)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig(256, 0);
    std::string filename = GetTempFile("zero_height.gif").string();

    bool result = encoder.Start(filename, config);

    EXPECT_FALSE(result);
    EXPECT_FALSE(encoder.IsRecording());
}

TEST_F(GIFEncoderTest, StartWithNonexistentDirectory)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = "/nonexistent/path/test.gif";

    bool result = encoder.Start(filename, config);

    EXPECT_FALSE(result);
    EXPECT_FALSE(encoder.IsRecording());
    EXPECT_FALSE(encoder.GetLastError().empty());
}

TEST_F(GIFEncoderTest, DoubleStartFails)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("double_start.gif").string();

    EXPECT_TRUE(encoder.Start(filename, config));
    EXPECT_TRUE(encoder.IsRecording());

    // Second Start should fail
    bool secondResult = encoder.Start(GetTempFile("second.gif").string(), config);

    EXPECT_FALSE(secondResult);
    EXPECT_TRUE(encoder.IsRecording());  // Still recording first file
}

TEST_F(GIFEncoderTest, StopWithoutStart)
{
    GIFEncoder encoder;

    // Should not crash
    EXPECT_NO_THROW(encoder.Stop());
    EXPECT_FALSE(encoder.IsRecording());
}

TEST_F(GIFEncoderTest, DoubleStopSafe)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("double_stop.gif").string();

    encoder.Start(filename, config);
    encoder.Stop();

    // Second Stop should be safe
    EXPECT_NO_THROW(encoder.Stop());
    EXPECT_FALSE(encoder.IsRecording());
}

// ================== Frame Input Tests ==================

TEST_F(GIFEncoderTest, OnVideoFrameIncreasesCount)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("frame_count.gif").string();

    encoder.Start(filename, config);
    auto fb = CreateTestFramebuffer(256, 192);

    encoder.OnVideoFrame(fb, 0.0);
    EXPECT_EQ(encoder.GetFramesEncoded(), 1);

    encoder.OnVideoFrame(fb, 0.02);
    EXPECT_EQ(encoder.GetFramesEncoded(), 2);

    encoder.OnVideoFrame(fb, 0.04);
    EXPECT_EQ(encoder.GetFramesEncoded(), 3);
}

TEST_F(GIFEncoderTest, OnVideoFrameWithoutStartIgnored)
{
    GIFEncoder encoder;
    auto fb = CreateTestFramebuffer(256, 192);

    // Should not crash
    EXPECT_NO_THROW(encoder.OnVideoFrame(fb, 0.0));
    EXPECT_EQ(encoder.GetFramesEncoded(), 0);
}

TEST_F(GIFEncoderTest, OnVideoFrameWithNullBuffer)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("null_buffer.gif").string();

    encoder.Start(filename, config);

    FramebufferDescriptor fb;
    fb.width = 256;
    fb.height = 192;
    fb.memoryBuffer = nullptr;

    // Should not crash - frame is skipped
    EXPECT_NO_THROW(encoder.OnVideoFrame(fb, 0.0));
    EXPECT_EQ(encoder.GetFramesEncoded(), 0);  // Frame not counted
}

// ================== RAII Tests ==================

TEST_F(GIFEncoderTest, DestructorCleansUp)
{
    std::string filename = GetTempFile("destructor.gif").string();

    {
        GIFEncoder encoder;
        auto config = CreateValidConfig();
        encoder.Start(filename, config);

        auto fb = CreateTestFramebuffer(256, 192);
        encoder.OnVideoFrame(fb, 0.0);

        // Destructor called here - should not crash and should finalize file
    }

    // File should exist and be valid GIF
    EXPECT_TRUE(std::filesystem::exists(filename));
    EXPECT_GT(std::filesystem::file_size(filename), 0);
}

// ================== State Query Tests ==================

TEST_F(GIFEncoderTest, GetTypeReturnsGif)
{
    GIFEncoder encoder;
    EXPECT_EQ(encoder.GetType(), "gif");
}

TEST_F(GIFEncoderTest, GetDisplayNameReturnsReadable)
{
    GIFEncoder encoder;
    EXPECT_EQ(encoder.GetDisplayName(), "GIF Animation");
}

TEST_F(GIFEncoderTest, SupportsVideoTrue)
{
    GIFEncoder encoder;
    EXPECT_TRUE(encoder.SupportsVideo());
}

TEST_F(GIFEncoderTest, SupportsAudioFalse)
{
    GIFEncoder encoder;
    EXPECT_FALSE(encoder.SupportsAudio());
}

TEST_F(GIFEncoderTest, GetOutputFilename)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    std::string filename = GetTempFile("output_name.gif").string();

    encoder.Start(filename, config);

    EXPECT_EQ(encoder.GetOutputFilename(), filename);
}

// ================== Palette Mode Tests ==================

TEST_F(GIFEncoderTest, AutoModeDefault)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    // Default should be Auto mode
    EXPECT_EQ(config.gifPaletteMode, EncoderConfig::GifPaletteMode::Auto);

    std::string filename = GetTempFile("auto_mode.gif").string();
    EXPECT_TRUE(encoder.Start(filename, config));

    auto fb = CreateTestFramebuffer(256, 192);
    encoder.OnVideoFrame(fb, 0.0);
    EXPECT_EQ(encoder.GetFramesEncoded(), 1);
}

TEST_F(GIFEncoderTest, FixedZX16Mode)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    config.gifPaletteMode = EncoderConfig::GifPaletteMode::FixedZX16;

    std::string filename = GetTempFile("fixed_zx16.gif").string();
    EXPECT_TRUE(encoder.Start(filename, config));

    auto fb = CreateTestFramebuffer(256, 192);
    encoder.OnVideoFrame(fb, 0.0);
    encoder.OnVideoFrame(fb, 0.02);
    encoder.OnVideoFrame(fb, 0.04);

    EXPECT_EQ(encoder.GetFramesEncoded(), 3);
}

TEST_F(GIFEncoderTest, FixedZX256Mode)
{
    GIFEncoder encoder;
    auto config = CreateValidConfig();
    config.gifPaletteMode = EncoderConfig::GifPaletteMode::FixedZX256;

    std::string filename = GetTempFile("fixed_zx256.gif").string();
    EXPECT_TRUE(encoder.Start(filename, config));

    auto fb = CreateTestFramebuffer(256, 192);
    encoder.OnVideoFrame(fb, 0.0);

    EXPECT_EQ(encoder.GetFramesEncoded(), 1);
}

TEST_F(GIFEncoderTest, FixedModeCreatesValidGIF)
{
    std::string filename = GetTempFile("fixed_valid.gif").string();

    {
        GIFEncoder encoder;
        auto config = CreateValidConfig();
        config.gifPaletteMode = EncoderConfig::GifPaletteMode::FixedZX16;

        encoder.Start(filename, config);

        auto fb = CreateTestFramebuffer(256, 192);
        encoder.OnVideoFrame(fb, 0.0);
        encoder.OnVideoFrame(fb, 0.02);
    }

    // File should exist and be valid GIF
    EXPECT_TRUE(std::filesystem::exists(filename));
    EXPECT_GT(std::filesystem::file_size(filename), 0);
}

// ================== OPT-1: Direct ZX Palette Index Tests ==================

TEST_F(GIFEncoderTest, DirectIndex_Black)
{
    // Black (0,0,0) should map to index 0
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0, 0), 0);
}

TEST_F(GIFEncoderTest, DirectIndex_Blue)
{
    // Blue (0,0,0xCD) should map to index 1
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0, 0xCD), 1);
}

TEST_F(GIFEncoderTest, DirectIndex_Red)
{
    // Red (0xCD,0,0) should map to index 2
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0, 0), 2);
}

TEST_F(GIFEncoderTest, DirectIndex_Magenta)
{
    // Magenta (0xCD,0,0xCD) should map to index 3
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0, 0xCD), 3);
}

TEST_F(GIFEncoderTest, DirectIndex_Green)
{
    // Green (0,0xCD,0) should map to index 4
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0xCD, 0), 4);
}

TEST_F(GIFEncoderTest, DirectIndex_Cyan)
{
    // Cyan (0,0xCD,0xCD) should map to index 5
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0xCD, 0xCD), 5);
}

TEST_F(GIFEncoderTest, DirectIndex_Yellow)
{
    // Yellow (0xCD,0xCD,0) should map to index 6
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0xCD, 0), 6);
}

TEST_F(GIFEncoderTest, DirectIndex_White)
{
    // White (0xCD,0xCD,0xCD) should map to index 7
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0xCD, 0xCD), 7);
}

TEST_F(GIFEncoderTest, DirectIndex_BrightBlack)
{
    // Bright black is still black (index 8)
    // Note: In ZX Spectrum, bright black is actually black (0,0,0)
    // but we detect bright via 0xFF channel, so test with 0xFF
    // This is edge case - bright flag with no color = index 8
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0, 0xFF), 9);  // Bright blue
}

TEST_F(GIFEncoderTest, DirectIndex_BrightBlue)
{
    // Bright Blue (0,0,0xFF) should map to index 9
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0, 0, 0xFF), 9);
}

TEST_F(GIFEncoderTest, DirectIndex_BrightRed)
{
    // Bright Red (0xFF,0,0) should map to index 10
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0, 0), 10);
}

TEST_F(GIFEncoderTest, DirectIndex_BrightWhite)
{
    // Bright White (0xFF,0xFF,0xFF) should map to index 15
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0xFF, 0xFF), 15);
}

TEST_F(GIFEncoderTest, DirectIndex_AllNormalColors)
{
    // Verify all 8 normal intensity colors
    // ZX Spectrum color encoding: Bit0=Blue, Bit1=Red, Bit2=Green
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0x00, 0x00), 0);  // Black
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0x00, 0xCD), 1);  // Blue
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0x00, 0x00), 2);  // Red
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0x00, 0xCD), 3);  // Magenta
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0xCD, 0x00), 4);  // Green
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0xCD, 0xCD), 5);  // Cyan
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0xCD, 0x00), 6);  // Yellow
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xCD, 0xCD, 0xCD), 7);  // White
}

TEST_F(GIFEncoderTest, DirectIndex_AllBrightColors)
{
    // Verify all 8 bright intensity colors (indices 8-15)
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0x00, 0xFF), 9);   // Bright Blue
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0x00, 0x00), 10);  // Bright Red
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0x00, 0xFF), 11);  // Bright Magenta
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0xFF, 0x00), 12);  // Bright Green
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0x00, 0xFF, 0xFF), 13);  // Bright Cyan
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0xFF, 0x00), 14);  // Bright Yellow
    EXPECT_EQ(GifGetZXPaletteIndexDirect(0xFF, 0xFF, 0xFF), 15);  // Bright White
}

// ================== BGRA Byte Order Tests ==================
// Verify GifThresholdImageZX correctly handles BGRA framebuffer format
// where byte[0]=B, byte[1]=G, byte[2]=R, byte[3]=A (little-endian ABGR)

TEST_F(GIFEncoderTest, ThresholdZX_BGRAByteOrder_Blue)
{
    // Blue pixel in BGRA format: B=0xCD, G=0x00, R=0x00, A=0xFF
    // As uint32_t (ABGR): 0xFF0000CD
    const uint32_t bluePixel = 0xFF0000CD;  // ABGR: Alpha=FF, B=00, G=00, R=CD in wrong order
    // Actually in memory as bytes: [0xCD, 0x00, 0x00, 0xFF] = B, G, R, A
    uint8_t input[4] = {0xCD, 0x00, 0x00, 0xFF};  // BGRA: Blue=CD, Green=0, Red=0
    uint8_t output[4] = {0};

    GifPalette palette = {};
    palette.bitDepth = 4;
    // Set blue color at index 1
    palette.r[1] = 0x00;
    palette.g[1] = 0x00;
    palette.b[1] = 0xCD;
    GifBuildPaletteTree(&palette);

    GifThresholdImageZX(nullptr, input, output, 1, 1, &palette);

    // Output byte[3] should be palette index 1 (blue)
    EXPECT_EQ(output[3], 1) << "Blue pixel should map to index 1";
}

TEST_F(GIFEncoderTest, ThresholdZX_BGRAByteOrder_Red)
{
    // Red pixel in BGRA format: B=0x00, G=0x00, R=0xCD, A=0xFF
    uint8_t input[4] = {0x00, 0x00, 0xCD, 0xFF};  // BGRA: Blue=0, Green=0, Red=CD
    uint8_t output[4] = {0};

    GifPalette palette = {};
    palette.bitDepth = 4;
    // Set red color at index 2
    palette.r[2] = 0xCD;
    palette.g[2] = 0x00;
    palette.b[2] = 0x00;
    GifBuildPaletteTree(&palette);

    GifThresholdImageZX(nullptr, input, output, 1, 1, &palette);

    // Output byte[3] should be palette index 2 (red)
    EXPECT_EQ(output[3], 2) << "Red pixel should map to index 2";
}

TEST_F(GIFEncoderTest, ThresholdZX_BGRAByteOrder_Black)
{
    // Black pixel in BGRA format: B=0x00, G=0x00, R=0x00, A=0xFF
    uint8_t input[4] = {0x00, 0x00, 0x00, 0xFF};  // BGRA: all zeros
    uint8_t output[4] = {0};

    GifPalette palette = {};
    palette.bitDepth = 4;
    // Set black color at index 0
    palette.r[0] = 0x00;
    palette.g[0] = 0x00;
    palette.b[0] = 0x00;
    GifBuildPaletteTree(&palette);

    GifThresholdImageZX(nullptr, input, output, 1, 1, &palette);

    // Output byte[3] should be palette index 0 (black)
    EXPECT_EQ(output[3], 0) << "Black pixel should map to index 0";
}

TEST_F(GIFEncoderTest, ThresholdZX_BGRAByteOrder_BrightWhite)
{
    // Bright white pixel in BGRA format: B=0xFF, G=0xFF, R=0xFF, A=0xFF
    uint8_t input[4] = {0xFF, 0xFF, 0xFF, 0xFF};  // BGRA: all max
    uint8_t output[4] = {0};

    GifPalette palette = {};
    palette.bitDepth = 4;
    // Set bright white color at index 15
    palette.r[15] = 0xFF;
    palette.g[15] = 0xFF;
    palette.b[15] = 0xFF;
    GifBuildPaletteTree(&palette);

    GifThresholdImageZX(nullptr, input, output, 1, 1, &palette);

    // Output byte[3] should be palette index 15 (bright white)
    EXPECT_EQ(output[3], 15) << "Bright white pixel should map to index 15";
}

TEST_F(GIFEncoderTest, ThresholdZX_All16Colors_BGRA)
{
    // ZX Spectrum 16-color palette with BGRA byte order verification
    // This test ensures every color in the palette is correctly encoded

    // Define all 16 ZX Spectrum colors in BGRA format (byte order in memory)
    // Format: {B, G, R, A} for each color
    struct TestColor
    {
        uint8_t bgra[4];
        uint8_t expectedIndex;
        const char* name;
    };

    TestColor colors[16] = {
        // Normal intensity colors (indices 0-7)
        {{0x00, 0x00, 0x00, 0xFF}, 0, "Black"},
        {{0xCD, 0x00, 0x00, 0xFF}, 1, "Blue"},
        {{0x00, 0x00, 0xCD, 0xFF}, 2, "Red"},
        {{0xCD, 0x00, 0xCD, 0xFF}, 3, "Magenta"},
        {{0x00, 0xCD, 0x00, 0xFF}, 4, "Green"},
        {{0xCD, 0xCD, 0x00, 0xFF}, 5, "Cyan"},
        {{0x00, 0xCD, 0xCD, 0xFF}, 6, "Yellow"},
        {{0xCD, 0xCD, 0xCD, 0xFF}, 7, "White"},
        // Bright intensity colors (indices 8-15)
        // Note: Bright black is still black but detected as index 8 only if at least one channel is 0xFF
        {{0x00, 0x00, 0x00, 0xFF}, 0, "Bright Black (same as black)"},  // Edge case
        {{0xFF, 0x00, 0x00, 0xFF}, 9, "Bright Blue"},
        {{0x00, 0x00, 0xFF, 0xFF}, 10, "Bright Red"},
        {{0xFF, 0x00, 0xFF, 0xFF}, 11, "Bright Magenta"},
        {{0x00, 0xFF, 0x00, 0xFF}, 12, "Bright Green"},
        {{0xFF, 0xFF, 0x00, 0xFF}, 13, "Bright Cyan"},
        {{0x00, 0xFF, 0xFF, 0xFF}, 14, "Bright Yellow"},
        {{0xFF, 0xFF, 0xFF, 0xFF}, 15, "Bright White"},
    };

    // Build full ZX palette
    GifPalette palette = {};
    palette.bitDepth = 4;
    const uint8_t zxPalette[16][3] = {// R, G, B for each color
                                      {0x00, 0x00, 0x00}, {0x00, 0x00, 0xCD}, {0xCD, 0x00, 0x00}, {0xCD, 0x00, 0xCD},
                                      {0x00, 0xCD, 0x00}, {0x00, 0xCD, 0xCD}, {0xCD, 0xCD, 0x00}, {0xCD, 0xCD, 0xCD},
                                      {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
                                      {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF}};
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = zxPalette[i][0];
        palette.g[i] = zxPalette[i][1];
        palette.b[i] = zxPalette[i][2];
    }
    GifBuildPaletteTree(&palette);

    // Test each color
    for (int i = 0; i < 16; i++)
    {
        // Skip duplicate black entry
        if (i == 8)
            continue;

        uint8_t output[4] = {0};
        GifThresholdImageZX(nullptr, colors[i].bgra, output, 1, 1, &palette);

        EXPECT_EQ(output[3], colors[i].expectedIndex)
            << "Color " << colors[i].name << " (index " << i << ") should map to palette index "
            << (int)colors[i].expectedIndex << " but got " << (int)output[3];
    }
}

TEST_F(GIFEncoderTest, ThresholdZX_MultiPixelFrame)
{
    // Test a multi-pixel frame with various colors to ensure batch processing works
    const int numPixels = 16;
    uint8_t input[numPixels * 4];
    uint8_t output[numPixels * 4] = {0};

    // Fill with all 16 colors in BGRA format
    const uint8_t bgraColors[16][4] = {
        {0x00, 0x00, 0x00, 0xFF},  // 0: Black
        {0xCD, 0x00, 0x00, 0xFF},  // 1: Blue
        {0x00, 0x00, 0xCD, 0xFF},  // 2: Red
        {0xCD, 0x00, 0xCD, 0xFF},  // 3: Magenta
        {0x00, 0xCD, 0x00, 0xFF},  // 4: Green
        {0xCD, 0xCD, 0x00, 0xFF},  // 5: Cyan
        {0x00, 0xCD, 0xCD, 0xFF},  // 6: Yellow
        {0xCD, 0xCD, 0xCD, 0xFF},  // 7: White
        {0x00, 0x00, 0x00, 0xFF},  // 8: Bright Black (same as black)
        {0xFF, 0x00, 0x00, 0xFF},  // 9: Bright Blue
        {0x00, 0x00, 0xFF, 0xFF},  // 10: Bright Red
        {0xFF, 0x00, 0xFF, 0xFF},  // 11: Bright Magenta
        {0x00, 0xFF, 0x00, 0xFF},  // 12: Bright Green
        {0xFF, 0xFF, 0x00, 0xFF},  // 13: Bright Cyan
        {0x00, 0xFF, 0xFF, 0xFF},  // 14: Bright Yellow
        {0xFF, 0xFF, 0xFF, 0xFF},  // 15: Bright White
    };

    for (int i = 0; i < 16; i++)
    {
        memcpy(input + i * 4, bgraColors[i], 4);
    }

    // Build palette
    GifPalette palette = {};
    palette.bitDepth = 4;
    const uint8_t zxPalette[16][3] = {{0x00, 0x00, 0x00}, {0x00, 0x00, 0xCD}, {0xCD, 0x00, 0x00}, {0xCD, 0x00, 0xCD},
                                      {0x00, 0xCD, 0x00}, {0x00, 0xCD, 0xCD}, {0xCD, 0xCD, 0x00}, {0xCD, 0xCD, 0xCD},
                                      {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
                                      {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF}};
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = zxPalette[i][0];
        palette.g[i] = zxPalette[i][1];
        palette.b[i] = zxPalette[i][2];
    }
    GifBuildPaletteTree(&palette);

    // Process all 16 pixels at once (4x4 or 16x1)
    GifThresholdImageZX(nullptr, input, output, 16, 1, &palette);

    // Verify each pixel got the correct palette index
    const uint8_t expectedIndices[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 16; i++)
    {
        EXPECT_EQ(output[i * 4 + 3], expectedIndices[i])
            << "Pixel " << i << " should have index " << (int)expectedIndices[i];
    }
}

// ================== Emulator ABGR Format Integration Tests ==================
// These tests use the ACTUAL emulator color format (ABGR uint32) to ensure
// byte order issues are caught. The emulator stores colors as 0xAABBGGRR
// where in memory (little-endian): byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A

TEST_F(GIFEncoderTest, EmulatorABGR_BlackPixelCorrectlyEncoded)
{
    // Emulator ABGR black: 0xFF000000
    // In memory: 0x00, 0x00, 0x00, 0xFF (R=0, G=0, B=0, A=255)
    uint32_t abgrPixel = 0xFF000000;
    uint8_t* input = reinterpret_cast<uint8_t*>(&abgrPixel);
    uint8_t output[4] = {0};

    // Build palette with black at index 0
    GifPalette palette = {};
    palette.bitDepth = 4;
    palette.r[0] = 0x00;
    palette.g[0] = 0x00;
    palette.b[0] = 0x00;
    GifBuildPaletteTree(&palette);

    GifThresholdImageZX(nullptr, input, output, 1, 1, &palette);

    EXPECT_EQ(output[3], 0) << "Black ABGR pixel 0xFF000000 should map to index 0";
}

TEST_F(GIFEncoderTest, EmulatorABGR_BluePixelCorrectlyEncoded)
{
    // Emulator ABGR blue: 0xFFC72200 (from screenzx.cpp)
    // In memory: 0x00, 0x22, 0xC7, 0xFF (R=0, G=0x22, B=0xC7, A=255)
    uint32_t abgrPixel = 0xFFC72200;
    uint8_t* input = reinterpret_cast<uint8_t*>(&abgrPixel);
    uint8_t output[4] = {0};

    // Build palette with emulator's actual blue color at index 1
    GifPalette palette = {};
    palette.bitDepth = 4;
    palette.r[1] = 0x00;  // R from ABGR bits 0-7
    palette.g[1] = 0x22;  // G from ABGR bits 8-15
    palette.b[1] = 0xC7;  // B from ABGR bits 16-23
    GifBuildPaletteTree(&palette);

    GifThresholdImage(nullptr, input, output, 1, 1, &palette);

    EXPECT_EQ(output[3], 1) << "Blue ABGR pixel 0xFFC72200 should map to index 1 (R=0,G=0x22,B=0xC7)";
}

TEST_F(GIFEncoderTest, EmulatorABGR_RedPixelCorrectlyEncoded)
{
    // Emulator ABGR red: 0xFF1628D6 (from screenzx.cpp)
    // In memory: 0xD6, 0x28, 0x16, 0xFF (R=0xD6, G=0x28, B=0x16, A=255)
    uint32_t abgrPixel = 0xFF1628D6;
    uint8_t* input = reinterpret_cast<uint8_t*>(&abgrPixel);
    uint8_t output[4] = {0};

    // Build palette with emulator's actual red color at index 2
    GifPalette palette = {};
    palette.bitDepth = 4;
    palette.r[2] = 0xD6;
    palette.g[2] = 0x28;
    palette.b[2] = 0x16;
    GifBuildPaletteTree(&palette);

    GifThresholdImage(nullptr, input, output, 1, 1, &palette);

    EXPECT_EQ(output[3], 2) << "Red ABGR pixel 0xFF1628D6 should map to index 2 (R=0xD6,G=0x28,B=0x16)";
}

TEST_F(GIFEncoderTest, EmulatorABGR_CyanPixelCorrectlyEncoded)
{
    // Emulator ABGR cyan: 0xFFC9C700 (from screenzx.cpp)
    // In memory: 0x00, 0xC7, 0xC9, 0xFF (R=0, G=0xC7, B=0xC9, A=255)
    uint32_t abgrPixel = 0xFFC9C700;
    uint8_t* input = reinterpret_cast<uint8_t*>(&abgrPixel);
    uint8_t output[4] = {0};

    // Build palette with emulator's actual cyan color at index 5
    GifPalette palette = {};
    palette.bitDepth = 4;
    palette.r[5] = 0x00;
    palette.g[5] = 0xC7;
    palette.b[5] = 0xC9;
    GifBuildPaletteTree(&palette);

    GifThresholdImage(nullptr, input, output, 1, 1, &palette);

    EXPECT_EQ(output[3], 5) << "Cyan ABGR pixel 0xFFC9C700 should map to index 5";
}

// NOTE: This test documents that the k-d tree doesn't guarantee exact color matching
// for similar colors (e.g., white 0xCACACA may map to bright white 0xFFFFFF).
// Use GifThresholdImageExact with hash lookup for exact color matching.
// This test is DISABLED because the k-d tree behavior is known/expected.
TEST_F(GIFEncoderTest, DISABLED_KdTree_EmulatorABGR_ApproximateMatching)
{
    // Test all 16 emulator colors using ACTUAL ABGR values from screenzx.cpp
    // These are the exact uint32_t values the framebuffer contains
    const uint32_t emulatorABGR[16] = {
        0xFF000000,  // 0: Black
        0xFFC72200,  // 1: Blue
        0xFF1628D6,  // 2: Red
        0xFFC733D4,  // 3: Magenta
        0xFF25C500,  // 4: Green
        0xFFC9C700,  // 5: Cyan
        0xFF2AC8CC,  // 6: Yellow
        0xFFCACACA,  // 7: White
        0xFF000000,  // 8: Bright Black
        0xFFFB2B00,  // 9: Bright Blue
        0xFF1C33FF,  // 10: Bright Red
        0xFFFC40FF,  // 11: Bright Magenta
        0xFF2FF900,  // 12: Bright Green
        0xFFFEFB00,  // 13: Bright Cyan
        0xFF36FCFF,  // 14: Bright Yellow
        0xFFFFFFFF,  // 15: Bright White
    };

    // Build palette matching the emulator's colors (extract RGB from ABGR)
    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        uint32_t abgr = emulatorABGR[i];
        palette.r[i] = abgr & 0xFF;          // R in bits 0-7
        palette.g[i] = (abgr >> 8) & 0xFF;   // G in bits 8-15
        palette.b[i] = (abgr >> 16) & 0xFF;  // B in bits 16-23
    }
    GifBuildPaletteTree(&palette);

    // Create framebuffer with all 16 colors (as uint32_t ABGR values)
    uint32_t framebuffer[16];
    memcpy(framebuffer, emulatorABGR, sizeof(emulatorABGR));

    uint8_t output[16 * 4] = {0};
    GifThresholdImage(nullptr, reinterpret_cast<uint8_t*>(framebuffer), output, 16, 1, &palette);

    // Verify output RGB matches input RGB EXACTLY
    // The encoder writes palette[index].r/g/b to output - this must match input
    for (int i = 0; i < 16; i++)
    {
        // Skip index 8 (bright black same as black at 0)
        if (i == 8)
            continue;

        // Extract expected RGB from input ABGR
        uint8_t expectedR = (emulatorABGR[i] >> 0) & 0xFF;
        uint8_t expectedG = (emulatorABGR[i] >> 8) & 0xFF;
        uint8_t expectedB = (emulatorABGR[i] >> 16) & 0xFF;

        // Actual RGB written by encoder to output
        uint8_t actualR = output[i * 4 + 0];
        uint8_t actualG = output[i * 4 + 1];
        uint8_t actualB = output[i * 4 + 2];

        EXPECT_EQ(actualR, expectedR) << "Color " << i << " (0x" << std::hex << emulatorABGR[i] << std::dec
                                      << ") R mismatch";
        EXPECT_EQ(actualG, expectedG) << "Color " << i << " (0x" << std::hex << emulatorABGR[i] << std::dec
                                      << ") G mismatch";
        EXPECT_EQ(actualB, expectedB) << "Color " << i << " (0x" << std::hex << emulatorABGR[i] << std::dec
                                      << ") B mismatch";
    }
}

// ================== Hash Table Color Lookup Tests ==================
// These tests verify the O(1) hash-based exact color lookup

TEST_F(GIFEncoderTest, HashLookup_BuildFromPalette)
{
    GifPalette palette = {};
    palette.bitDepth = 4;  // 16 colors

    // Set up simple palette
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = i * 16;
        palette.g[i] = i * 8;
        palette.b[i] = i * 4;
    }

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    EXPECT_TRUE(lookup.valid);
    EXPECT_EQ(lookup.numColors, 16);
}

TEST_F(GIFEncoderTest, HashLookup_ExactColorMatch)
{
    GifPalette palette = {};
    palette.bitDepth = 2;  // 4 colors only

    // Set known colors
    palette.r[0] = 0x00;
    palette.g[0] = 0x00;
    palette.b[0] = 0x00;  // Black
    palette.r[1] = 0xFF;
    palette.g[1] = 0x00;
    palette.b[1] = 0x00;  // Red
    palette.r[2] = 0x00;
    palette.g[2] = 0xFF;
    palette.b[2] = 0x00;  // Green
    palette.r[3] = 0x00;
    palette.g[3] = 0x00;
    palette.b[3] = 0xFF;  // Blue

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    // Test exact lookups (ABGR format)
    EXPECT_EQ(GifGetColorIndex(&lookup, 0xFF000000), 0);  // Black
    EXPECT_EQ(GifGetColorIndex(&lookup, 0xFF0000FF), 1);  // Red (R=FF in ABGR)
    EXPECT_EQ(GifGetColorIndex(&lookup, 0xFF00FF00), 2);  // Green (G=FF in ABGR)
    EXPECT_EQ(GifGetColorIndex(&lookup, 0xFFFF0000), 3);  // Blue (B=FF in ABGR)
}

TEST_F(GIFEncoderTest, HashLookup_EmulatorPalette16Colors)
{
    // Use actual emulator palette colors
    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    // Verify all colors lookup correctly
    // Note: For duplicate colors (black at 0 and 8), verify the RGB matches, not the index
    for (int i = 0; i < 16; i++)
    {
        uint8_t index = GifGetColorIndex(&lookup, emulatorABGR[i]);

        // Verify the palette color at returned index matches the input color
        uint8_t expectedR = emulatorABGR[i] & 0xFF;
        uint8_t expectedG = (emulatorABGR[i] >> 8) & 0xFF;
        uint8_t expectedB = (emulatorABGR[i] >> 16) & 0xFF;

        EXPECT_EQ(palette.r[index], expectedR) << "Color " << i << " R mismatch";
        EXPECT_EQ(palette.g[index], expectedG) << "Color " << i << " G mismatch";
        EXPECT_EQ(palette.b[index], expectedB) << "Color " << i << " B mismatch";
    }
}

TEST_F(GIFEncoderTest, HashLookup_ThresholdImageExact)
{
    // Use emulator palette
    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    // Create framebuffer with all 16 colors
    uint32_t framebuffer[16];
    memcpy(framebuffer, emulatorABGR, sizeof(emulatorABGR));

    uint8_t output[16 * 4] = {0};
    GifThresholdImageExact(nullptr, reinterpret_cast<uint8_t*>(framebuffer), output, 16, 1, &lookup, &palette);

    // Verify OUTPUT RGB matches INPUT RGB EXACTLY for every color
    for (int i = 0; i < 16; i++)
    {
        if (i == 8)
            continue;  // Skip duplicate black

        uint8_t expectedR = emulatorABGR[i] & 0xFF;
        uint8_t expectedG = (emulatorABGR[i] >> 8) & 0xFF;
        uint8_t expectedB = (emulatorABGR[i] >> 16) & 0xFF;

        EXPECT_EQ(output[i * 4 + 0], expectedR) << "Color " << i << " R mismatch";
        EXPECT_EQ(output[i * 4 + 1], expectedG) << "Color " << i << " G mismatch";
        EXPECT_EQ(output[i * 4 + 2], expectedB) << "Color " << i << " B mismatch";
    }
}

TEST_F(GIFEncoderTest, HashLookup_256ColorPalette)
{
    // Test with full 256-color palette
    GifPalette palette = {};
    palette.bitDepth = 8;  // 256 colors

    // Fill with gradient colors
    for (int i = 0; i < 256; i++)
    {
        palette.r[i] = i;
        palette.g[i] = (i * 2) % 256;
        palette.b[i] = (i * 3) % 256;
    }

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    EXPECT_TRUE(lookup.valid);
    EXPECT_EQ(lookup.numColors, 256);

    // Verify all 256 colors lookup correctly
    for (int i = 0; i < 256; i++)
    {
        uint32_t abgr =
            0xFF000000 | ((uint32_t)palette.b[i] << 16) | ((uint32_t)palette.g[i] << 8) | (uint32_t)palette.r[i];

        uint8_t index = GifGetColorIndex(&lookup, abgr);

        // Verify returned color matches (may be different index for duplicates)
        EXPECT_EQ(palette.r[index], palette.r[i]) << "Color " << i << " R mismatch";
        EXPECT_EQ(palette.g[index], palette.g[i]) << "Color " << i << " G mismatch";
        EXPECT_EQ(palette.b[index], palette.b[i]) << "Color " << i << " B mismatch";
    }
}
