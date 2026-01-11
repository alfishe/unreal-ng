#include "emulator/recording/encoders/gif_encoder.h"

#include <gtest/gtest.h>

#include <filesystem>

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
