#include <gtest/gtest.h>
#include <json/json.h>

#include <memory>
#include <string>

#include "../../../automation/webapi/src/common/recordingjson.h"

/// POST /video/record start body: the optional "audio", "video_bitrate" and "audio_bitrate" fields
/// (ParseRecordingCodecJson, the WebAPI side of RecordingRequest). The MCP capture_media record_start
/// forwards the same fields to this endpoint.

namespace
{
Json::Value Body(const char* json)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    const std::string text(json);
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    reader->parse(text.data(), text.data() + text.size(), &value, &errors);
    return value;
}
}  // namespace

TEST(RecordingJson_Test, NoAudioFieldIsVideoOnly)
{
    const Json::Value body = Body(R"({"action":"start","format":"gif"})");
    RecordingCodecJson codecs;
    std::string error;
    ASSERT_TRUE(ParseRecordingCodecJson(&body, "gif", "out.gif", codecs, error)) << error;
    EXPECT_EQ(codecs.audio, "");
    EXPECT_EQ(codecs.videoBitrate, 0u);
    EXPECT_EQ(codecs.audioBitrate, 0u);

    ASSERT_TRUE(ParseRecordingCodecJson(nullptr, "h264", "out.mp4", codecs, error)) << error;
    EXPECT_EQ(codecs.audio, "");
}

TEST(RecordingJson_Test, AacWithBitratesInMp4)
{
    const Json::Value body = Body(R"({"audio":"AAC","video_bitrate":8000,"audio_bitrate":"192"})");
    RecordingCodecJson codecs;
    std::string error;
    ASSERT_TRUE(ParseRecordingCodecJson(&body, "h264", "out.mp4", codecs, error)) << error;
    EXPECT_EQ(codecs.audio, "aac");
    EXPECT_EQ(codecs.videoBitrate, 8000u);
    EXPECT_EQ(codecs.audioBitrate, 192u);
}

TEST(RecordingJson_Test, AudioTrueMeansAacFalseMeansNone)
{
    RecordingCodecJson codecs;
    std::string error;
    const Json::Value on = Body(R"({"audio":true})");
    ASSERT_TRUE(ParseRecordingCodecJson(&on, "h264", "out.mov", codecs, error)) << error;
    EXPECT_EQ(codecs.audio, "aac");
    const Json::Value off = Body(R"({"audio":false})");
    ASSERT_TRUE(ParseRecordingCodecJson(&off, "gif", "out.gif", codecs, error)) << error;
    EXPECT_EQ(codecs.audio, "");
}

TEST(RecordingJson_Test, GifWithAudioIsRefused)
{
    const Json::Value body = Body(R"({"audio":"aac"})");
    RecordingCodecJson codecs;
    std::string error;
    EXPECT_FALSE(ParseRecordingCodecJson(&body, "gif", "out.gif", codecs, error));
    EXPECT_NE(error.find("GIF has no audio"), std::string::npos) << error;
}

TEST(RecordingJson_Test, MalformedFieldsAreRefused)
{
    RecordingCodecJson codecs;
    std::string error;
    const Json::Value badAudio = Body(R"({"audio":5})");
    EXPECT_FALSE(ParseRecordingCodecJson(&badAudio, "h264", "out.mp4", codecs, error));
    EXPECT_NE(error.find("audio must be"), std::string::npos) << error;

    const Json::Value badBitrate = Body(R"({"audio":"aac","video_bitrate":-1})");
    EXPECT_FALSE(ParseRecordingCodecJson(&badBitrate, "h264", "out.mp4", codecs, error));
    EXPECT_NE(error.find("video_bitrate"), std::string::npos) << error;

    const Json::Value orphanBitrate = Body(R"({"audio_bitrate":128})");
    EXPECT_FALSE(ParseRecordingCodecJson(&orphanBitrate, "h264", "out.mp4", codecs, error));
    EXPECT_NE(error.find("needs an audio codec"), std::string::npos) << error;
}
