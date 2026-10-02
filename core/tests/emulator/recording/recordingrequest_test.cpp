#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "recordingrequest.h"

/// RecordingRequest: the audio codec / container rules every automation surface (WebAPI, CLI, MCP through the
/// WebAPI, Lua, Python) applies to a recording start before it touches the recorder.

TEST(RecordingRequest_Test, NormalizeAudioCodec_NoneMeansVideoOnly)
{
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec(""), "");
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec("none"), "");
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec("OFF"), "");
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec("AAC"), "aac");
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec("pcm"), "pcm_s16le");
    EXPECT_EQ(RecordingRequest::NormalizeAudioCodec("wav"), "pcm_s16le");
}

TEST(RecordingRequest_Test, ContainerOf_ExtensionWinsElseFormatDefault)
{
    EXPECT_EQ(RecordingRequest::ContainerOf("h264", "/tmp/out.MOV"), "mov");
    EXPECT_EQ(RecordingRequest::ContainerOf("h264", "/tmp/dir.v2/out"), "mp4");
    EXPECT_EQ(RecordingRequest::ContainerOf("gif", ""), "gif");
    EXPECT_EQ(RecordingRequest::ContainerOf("vp9", ""), "webm");
    EXPECT_EQ(RecordingRequest::ContainerOf("rawvideo", ""), "avi");
}

TEST(RecordingRequest_Test, ValidateAudio_VideoOnlyIsAlwaysAccepted)
{
    EXPECT_EQ(RecordingRequest::ValidateAudio("gif", "a.gif", ""), "");
    EXPECT_EQ(RecordingRequest::ValidateAudio("h264", "a.mp4", "none"), "");
    EXPECT_EQ(RecordingRequest::ValidateAudio("h264", "a.xyz", ""), "");
}

TEST(RecordingRequest_Test, ValidateAudio_GifHasNoAudioTrack)
{
    const std::string byFormat = RecordingRequest::ValidateAudio("gif", "a.mp4", "aac");
    EXPECT_NE(byFormat.find("GIF has no audio"), std::string::npos) << byFormat;
    const std::string byFile = RecordingRequest::ValidateAudio("h264", "a.gif", "aac");
    EXPECT_NE(byFile.find("GIF has no audio"), std::string::npos) << byFile;
}

TEST(RecordingRequest_Test, ValidateAudio_CodecMustFitTheContainer)
{
    // Native macOS combinations
    EXPECT_EQ(RecordingRequest::ValidateAudio("h264", "a.mp4", "aac"), "");
    EXPECT_EQ(RecordingRequest::ValidateAudio("hevc", "a.mov", "aac"), "");
    // ffmpeg combinations
    EXPECT_EQ(RecordingRequest::ValidateAudio("h264", "a.mkv", "flac"), "");
    EXPECT_EQ(RecordingRequest::ValidateAudio("vp9", "a.webm", "opus"), "");
    EXPECT_EQ(RecordingRequest::ValidateAudio("rawvideo", "a.avi", "pcm"), "");

    const std::string webmAac = RecordingRequest::ValidateAudio("vp9", "a.webm", "aac");
    EXPECT_NE(webmAac.find("does not fit a .webm"), std::string::npos) << webmAac;
    EXPECT_NE(RecordingRequest::ValidateAudio("h264", "a.mp4", "pcm_s16le"), "");
    EXPECT_NE(RecordingRequest::ValidateAudio("h264", "a.mov", "opus"), "");

    const std::string unknownCodec = RecordingRequest::ValidateAudio("h264", "a.mp4", "ogg");
    EXPECT_NE(unknownCodec.find("Unsupported audio codec 'ogg'"), std::string::npos) << unknownCodec;
    const std::string unknownContainer = RecordingRequest::ValidateAudio("h264", "a.xyz", "aac");
    EXPECT_NE(unknownContainer.find(".xyz"), std::string::npos) << unknownContainer;
}

TEST(RecordingRequest_Test, ValidateBitrates_RangesAndAudioBitrateNeedsAudio)
{
    EXPECT_EQ(RecordingRequest::ValidateBitrates(0, 0, ""), "");
    EXPECT_EQ(RecordingRequest::ValidateBitrates(8000, 192, "aac"), "");
    EXPECT_NE(RecordingRequest::ValidateBitrates(50, 0, ""), "");
    EXPECT_NE(RecordingRequest::ValidateBitrates(0, 16, "aac"), "");
    EXPECT_NE(RecordingRequest::ValidateBitrates(0, 1024, "aac"), "");
    const std::string orphan = RecordingRequest::ValidateBitrates(0, 192, "");
    EXPECT_NE(orphan.find("needs an audio codec"), std::string::npos) << orphan;
}

TEST(RecordingRequest_Test, IsNativeCombination_H264HevcAacInMp4Mov)
{
    EXPECT_TRUE(RecordingRequest::IsNativeCombination("h264", "mp4", "aac"));
    EXPECT_TRUE(RecordingRequest::IsNativeCombination("hevc", "mov", ""));
    EXPECT_FALSE(RecordingRequest::IsNativeCombination("h264", "mkv", "aac"));
    EXPECT_FALSE(RecordingRequest::IsNativeCombination("h264", "mp4", "mp3"));
    EXPECT_FALSE(RecordingRequest::IsNativeCombination("vp9", "mp4", "aac"));
}
