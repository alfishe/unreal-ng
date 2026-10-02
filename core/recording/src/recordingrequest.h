#pragma once

#include <cstdint>
#include <string>

/// @brief Rules for a recording start request, shared by every automation surface (WebAPI, CLI, MCP via the
/// WebAPI, Lua, Python) so they accept and refuse the same requests with the same message.
///
/// A request names a video format (gif, h264, h265/hevc, vp9, rawvideo), an output file, and optionally an audio
/// codec. No audio codec means a video-only file - the default on every surface. The audio codec must fit the
/// container the file is written as (its extension):
///
/// | container | audio codecs                                   |
/// |-----------|------------------------------------------------|
/// | gif       | none (GIF has no audio track)                  |
/// | mp4       | aac, mp3, opus, flac                           |
/// | mov       | aac, mp3, pcm_s16le                            |
/// | mkv       | aac, mp3, opus, vorbis, flac, pcm_s16le        |
/// | webm      | opus, vorbis                                   |
/// | avi       | aac, mp3, pcm_s16le                            |
///
/// The native macOS encoder (VideoToolbox + AVAssetWriter) covers h264/hevc + aac in mp4/mov. Every other
/// combination is written by ffmpeg (StartRecording reports a missing ffmpeg).
namespace RecordingRequest
{
/// Lower case; "", "none", "off" and "false" mean no audio (returns ""); "pcm" and "wav" become "pcm_s16le"
std::string NormalizeAudioCodec(const std::string& codec);

/// The container the file is written as: the filename's extension in lower case, or the format's default
/// container when the filename has none (gif -> gif, rawvideo -> avi, vp9 -> webm, other video codecs -> mp4)
std::string ContainerOf(const std::string& format, const std::string& filename);

/// "" when the (normalized) audio codec fits the container, otherwise the reason it is refused.
/// An empty audio codec (video only) is always accepted
std::string ValidateAudio(const std::string& format, const std::string& filename, const std::string& audioCodec);

/// "" when the bitrates are in range, otherwise the reason. 0 = the encoder default.
/// Video: 0 or 100..200000 kbps. Audio: 0 or 32..512 kbps; an audio bitrate needs an audio codec
std::string ValidateBitrates(uint32_t videoKbps, uint32_t audioKbps, const std::string& audioCodec);

/// True when the native macOS encoder can write this request (h264/hevc + aac or no audio, in mp4/mov)
bool IsNativeCombination(const std::string& format, const std::string& container, const std::string& audioCodec);
}  // namespace RecordingRequest
