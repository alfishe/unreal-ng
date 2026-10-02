/// @file cli-videorecord-options_test.cpp
/// @brief CLI `videorecord start` arguments (cli-videorecord-options.h): the positional format/file, the
/// options, and the audio codec rules shared with the WebAPI, Lua and Python. No CLI socket.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../../automation/cli/src/commands/cli-videorecord-options.h"

TEST(CliVideoRecordOptions_Test, DefaultsAreGifVideoOnly)
{
    CliVideoRecord::StartOptions options;
    std::string error;
    ASSERT_TRUE(CliVideoRecord::ParseStart({"start"}, options, error)) << error;
    EXPECT_EQ(options.format, "gif");
    EXPECT_TRUE(options.filename.empty());
    EXPECT_EQ(options.audio, "");
    EXPECT_EQ(options.scale, 1u);
    EXPECT_FALSE(options.hasAudioRate);
}

TEST(CliVideoRecordOptions_Test, AudioCodecAndBitrates)
{
    CliVideoRecord::StartOptions options;
    std::string error;
    ASSERT_TRUE(CliVideoRecord::ParseStart({"start", "h264", "out.mp4", "--scale", "2", "--audio", "AAC",
                                            "--video-bitrate", "8000", "--audio-bitrate", "192", "--audio-rate",
                                            "48000"},
                                           options, error))
        << error;
    EXPECT_EQ(options.format, "h264");
    EXPECT_EQ(options.filename, "out.mp4");
    EXPECT_EQ(options.scale, 2u);
    EXPECT_EQ(options.audio, "aac");
    EXPECT_EQ(options.videoBitrate, 8000u);
    EXPECT_EQ(options.audioBitrate, 192u);
    EXPECT_TRUE(options.hasAudioRate);
    EXPECT_EQ(options.audioRate, 48000u);
    EXPECT_TRUE(CliVideoRecord::Validate(options, error)) << error;
}

TEST(CliVideoRecordOptions_Test, GifFormatThenGifFileStaysPositional)
{
    // "gif out.gif": the second positional is the file, not a new format
    CliVideoRecord::StartOptions options;
    std::string error;
    ASSERT_TRUE(CliVideoRecord::ParseStart({"start", "gif", "out.gif"}, options, error)) << error;
    EXPECT_EQ(options.format, "gif");
    EXPECT_EQ(options.filename, "out.gif");
}

TEST(CliVideoRecordOptions_Test, GifWithAudioIsRefused)
{
    CliVideoRecord::StartOptions options;
    std::string error;
    ASSERT_TRUE(CliVideoRecord::ParseStart({"start", "gif", "out.gif", "--audio", "aac"}, options, error)) << error;
    EXPECT_FALSE(CliVideoRecord::Validate(options, error));
    EXPECT_NE(error.find("GIF has no audio"), std::string::npos) << error;
}

TEST(CliVideoRecordOptions_Test, DefaultFileOfH264IsMkvWhichHoldsAac)
{
    CliVideoRecord::StartOptions options;
    std::string error;
    ASSERT_TRUE(CliVideoRecord::ParseStart({"start", "h264", "--audio", "aac"}, options, error)) << error;
    EXPECT_EQ(CliVideoRecord::DefaultExtension(options.format), "mkv");
    options.filename = "/tmp/video." + CliVideoRecord::DefaultExtension(options.format);
    EXPECT_TRUE(CliVideoRecord::Validate(options, error)) << error;
}

TEST(CliVideoRecordOptions_Test, BadValuesAreRefused)
{
    CliVideoRecord::StartOptions options;
    std::string error;
    EXPECT_FALSE(CliVideoRecord::ParseStart({"start", "h264", "--video-bitrate", "fast"}, options, error));
    EXPECT_NE(error.find("video-bitrate"), std::string::npos) << error;
    EXPECT_FALSE(CliVideoRecord::ParseStart({"start", "--audio-rate", "22050"}, options, error));
    EXPECT_NE(error.find("Unsupported audio rate"), std::string::npos) << error;
    EXPECT_FALSE(CliVideoRecord::ParseStart({"start", "--audo", "aac"}, options, error));
    EXPECT_NE(error.find("--audo"), std::string::npos) << error;

    ASSERT_TRUE(CliVideoRecord::ParseStart({"start", "vp9", "out.webm", "--audio", "aac"}, options, error)) << error;
    EXPECT_FALSE(CliVideoRecord::Validate(options, error));
    EXPECT_NE(error.find(".webm"), std::string::npos) << error;
}
