#pragma once

#include <json/json.h>

#include <cstdint>
#include <string>

#include "jsonnumber.h"
#include "recordingrequest.h"

/// Codec part of a POST /video/record start body. Defaults = video only, encoder-default bitrates
struct RecordingCodecJson
{
    std::string audio;          ///< normalized audio codec, "" = video only
    uint32_t videoBitrate = 0;  ///< kbps, 0 = encoder default
    uint32_t audioBitrate = 0;  ///< kbps, 0 = encoder default
};

/// Reads "audio", "audio_bitrate" and "video_bitrate" from a start body and checks them against the format and
/// the output file (RecordingRequest rules, shared with the CLI, Lua and Python).
/// @return false with the reason in error (an HTTP 400) when a field is malformed or the combination is refused
inline bool ParseRecordingCodecJson(const Json::Value* body, const std::string& format, const std::string& filename,
                                    RecordingCodecJson& out, std::string& error)
{
    out = RecordingCodecJson();
    if (body && body->isMember("audio") && !(*body)["audio"].isNull())
    {
        const Json::Value& audio = (*body)["audio"];
        if (audio.isBool())
        {
            // "audio": true = the default codec (aac), false = video only
            out.audio = audio.asBool() ? "aac" : "";
        }
        else if (audio.isString())
        {
            out.audio = RecordingRequest::NormalizeAudioCodec(audio.asString());
        }
        else
        {
            error = "audio must be a codec name (aac, mp3, opus, vorbis, flac, pcm_s16le), true or false.";
            return false;
        }
    }

    for (const char* field : {"video_bitrate", "audio_bitrate"})
    {
        if (!body || !body->isMember(field) || (*body)[field].isNull())
            continue;
        uint32_t value = 0;
        if (!ParseJsonUInt((*body)[field], 1000000, value))
        {
            error = std::string(field) + " must be a non-negative integer (kbps).";
            return false;
        }
        (field[0] == 'v' ? out.videoBitrate : out.audioBitrate) = value;
    }

    error = RecordingRequest::ValidateAudio(format, filename, out.audio);
    if (error.empty())
        error = RecordingRequest::ValidateBitrates(out.videoBitrate, out.audioBitrate, out.audio);
    return error.empty();
}
