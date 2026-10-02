#include "recordingrequest.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool OneOf(const std::string& value, std::initializer_list<const char*> options)
{
    for (const char* option : options)
    {
        if (value == option)
            return true;
    }
    return false;
}

/// The audio codecs each container holds; nullptr for an unknown container
const char* AllowedAudio(const std::string& container)
{
    if (container == "mp4")
        return "aac, mp3, opus, flac";
    if (container == "mov")
        return "aac, mp3, pcm_s16le";
    if (container == "mkv")
        return "aac, mp3, opus, vorbis, flac, pcm_s16le";
    if (container == "webm")
        return "opus, vorbis";
    if (container == "avi")
        return "aac, mp3, pcm_s16le";
    return nullptr;
}

bool ContainerHolds(const std::string& container, const std::string& codec)
{
    if (container == "mp4")
        return OneOf(codec, {"aac", "mp3", "opus", "flac"});
    if (container == "mov")
        return OneOf(codec, {"aac", "mp3", "pcm_s16le"});
    if (container == "mkv")
        return OneOf(codec, {"aac", "mp3", "opus", "vorbis", "flac", "pcm_s16le"});
    if (container == "webm")
        return OneOf(codec, {"opus", "vorbis"});
    if (container == "avi")
        return OneOf(codec, {"aac", "mp3", "pcm_s16le"});
    return false;
}
}  // namespace

namespace RecordingRequest
{
std::string NormalizeAudioCodec(const std::string& codec)
{
    const std::string lower = ToLower(codec);
    if (OneOf(lower, {"", "none", "off", "false"}))
        return "";
    if (OneOf(lower, {"pcm", "wav"}))
        return "pcm_s16le";
    return lower;
}

std::string ContainerOf(const std::string& format, const std::string& filename)
{
    const size_t dot = filename.rfind('.');
    const size_t slash = filename.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && dot + 1 < filename.size())
        return ToLower(filename.substr(dot + 1));

    const std::string lower = ToLower(format);
    if (lower == "gif")
        return "gif";
    if (lower == "rawvideo")
        return "avi";
    if (lower == "vp9")
        return "webm";
    if (OneOf(lower, {"mp4", "mov", "mkv", "webm", "avi"}))
        return lower;
    return "mp4";
}

std::string ValidateAudio(const std::string& format, const std::string& filename, const std::string& audioCodec)
{
    const std::string codec = NormalizeAudioCodec(audioCodec);
    if (codec.empty())
        return "";

    if (!OneOf(codec, {"aac", "mp3", "opus", "vorbis", "flac", "pcm_s16le"}))
        return "Unsupported audio codec '" + audioCodec + "'. Use aac, mp3, opus, vorbis, flac or pcm_s16le.";

    const std::string container = ContainerOf(format, filename);
    if (ToLower(format) == "gif" || container == "gif")
        return "GIF has no audio track. Record h264/hevc into .mp4 or .mov to include audio, or leave audio out.";

    const char* allowed = AllowedAudio(container);
    if (!allowed)
        return "Audio is not supported in a ." + container + " file. Use .mp4, .mov, .mkv, .webm or .avi.";

    if (!ContainerHolds(container, codec))
        return "Audio codec '" + codec + "' does not fit a ." + container + " file (" + container + " holds " +
               allowed + ").";

    return "";
}

std::string ValidateBitrates(uint32_t videoKbps, uint32_t audioKbps, const std::string& audioCodec)
{
    if (videoKbps != 0 && (videoKbps < 100 || videoKbps > 200000))
        return "video_bitrate must be 0 (auto) or 100..200000 kbps.";
    if (audioKbps != 0 && (audioKbps < 32 || audioKbps > 512))
        return "audio_bitrate must be 0 (auto) or 32..512 kbps.";
    if (audioKbps != 0 && NormalizeAudioCodec(audioCodec).empty())
        return "audio_bitrate needs an audio codec (e.g. audio: aac).";
    return "";
}

bool IsNativeCombination(const std::string& format, const std::string& container, const std::string& audioCodec)
{
    const std::string video = ToLower(format);
    const std::string audio = NormalizeAudioCodec(audioCodec);
    return (container == "mp4" || container == "mov") && OneOf(video, {"h264", "h265", "hevc"}) &&
           (audio.empty() || audio == "aac");
}
}  // namespace RecordingRequest
